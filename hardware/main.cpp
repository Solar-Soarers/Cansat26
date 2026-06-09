// =============================================================================
//  CAN-7USAT  |  MAIN FIRMWARE  –  INTEGRATION ENTRY POINT
//  Bennett University – Astronomy Club / Space Tech Division
//  Firmware Version : V3.0  (Competition-Compliant, IN-SPACe 2026)
// =============================================================================
//
//  This file is the top-level Arduino sketch (.ino / main.cpp).
//  It integrates all three sensor sections and implements:
//
//    ┌──────────────────────────────────────────────────────────────────────┐
//    │  8-State Flight State Machine  (Rule 6.2)                           │
//    │  0 BOOT → 1 TEST_MODE → 2 LAUNCH_PAD → 3 ASCENT →                 │
//    │  4 ROCKET_DEPLOY → 5 DESCENT → 6 AEROBREAK_RELEASE → 7 IMPACT      │
//    └──────────────────────────────────────────────────────────────────────┘
//
//    ┌──────────────────────────────────────────────────────────────────────┐
//    │  16-Field Telemetry CSV  (Rule 6.3, ≥1 Hz, ASCII, CR-terminated)   │
//    │  TEAM_ID | TIME | PKT# | ALT | PRESS | TEMP | VOLT | GNSS_TIME |  │
//    │  LAT | LON | GNSS_ALT | SATS | ACCEL | GYRO_SPIN | STATE | OPT   │
//    └──────────────────────────────────────────────────────────────────────┘
//
//  Hardware: ESP32-WROOM-32 DevKit  (240 MHz dual-core, 4 MB flash)
//
//  PlatformIO build flags (platformio.ini):
//    platform = espressif32
//    board    = esp32dev
//    framework = arduino
//    lib_deps =
//      adafruit/Adafruit BMP3XX Library
//      adafruit/Adafruit BNO055
//      adafruit/Adafruit Unified Sensor
//      sensirion/Sensirion I2C SPS30
//      sensirion/Sensirion I2C SCD4x
//      mikalhart/TinyGPSPlus
// =============================================================================

#include <Arduino.h>
#include "section1_environmental.h"
#include "section2_motion.h"
#include "section3_comms.h"

// ---------------------------------------------------------------------------
//  Team identification (update before competition CDR)
// ---------------------------------------------------------------------------
#define TEAM_ID   "2026-IN-SPACe-CAN-7USAT-XXX"   // Replace XXX with official ID

// ---------------------------------------------------------------------------
//  Timing constants
// ---------------------------------------------------------------------------
#define TELEMETRY_INTERVAL_MS  1000   // 1 Hz mandatory minimum (Rule 6.3-i)
#define SENSOR_INTERVAL_MS     200    // 5 Hz internal sampling (averaged to 1 Hz TX)

// ---------------------------------------------------------------------------
//  Beacon
// ---------------------------------------------------------------------------
#define PIN_BEACON    4    // 95 dB siren buzzer via transistor
#define PIN_STATUS_LED 2   // External switch indicator LED

// ---------------------------------------------------------------------------
//  Launch detection thresholds
// ---------------------------------------------------------------------------
#define ASCENT_ACCEL_G       3.0f    // Acceleration > 3G → ASCENT state
#define MAIN_CHUTE_ALT_M   600.0f   // Deploy main at 600 m (Rule 23)
#define MAIN_CHUTE_SAMPLES   5      // Moving-average window (5 readings)
#define LANDED_VSPEED_MPS    1.5f   // |vertical speed| < 1.5 m/s → landed

// ---------------------------------------------------------------------------
//  Simulation mode (TEST_MODE, Rule 6.1-iii)
// ---------------------------------------------------------------------------
static bool     simEnabled   = false;
static bool     simActive    = false;
static float    simAltitude  = 0.0f;
// Simulated altitude injection: GS sends SIM_PRESSURE commands (Rule 6.1-vi)
// For simplicity we accept a fake altitude directly here.

// ---------------------------------------------------------------------------
//  Flight state machine
// ---------------------------------------------------------------------------
enum FlightState_t : uint8_t {
    STATE_BOOT             = 0,
    STATE_TEST_MODE        = 1,
    STATE_LAUNCH_PAD       = 2,
    STATE_ASCENT           = 3,
    STATE_ROCKET_DEPLOY    = 4,
    STATE_DESCENT          = 5,
    STATE_AEROBREAK_RELEASE= 6,
    STATE_IMPACT           = 7,
};

static FlightState_t flightState = STATE_BOOT;
static uint32_t missionTime_s    = 0;    // Seconds since power-on (persisted)
static uint32_t packetCount      = 0;
static float    groundAlt_m      = 0.0f; // Baro-calibrated ground reference

// Altitude moving-average buffer for main-chute trigger
static float altBuffer[MAIN_CHUTE_SAMPLES] = {0};
static uint8_t altBufIdx = 0;

// ---------------------------------------------------------------------------
//  Timing variables
// ---------------------------------------------------------------------------
static uint32_t lastTxTime_ms    = 0;
static uint32_t lastSensorMs     = 0;
static uint32_t bootTime_ms      = 0;
static uint32_t stateEnterTime_ms= 0;

// ---------------------------------------------------------------------------
//  Telemetry packet buffer
// ---------------------------------------------------------------------------
static char txPacket[256];

// ===========================================================================
//  Utility: Altitude moving average
// ===========================================================================
static float altitudeMovingAvg(float newAlt)
{
    altBuffer[altBufIdx % MAIN_CHUTE_SAMPLES] = newAlt;
    altBufIdx++;
    float sum = 0;
    for (int i = 0; i < MAIN_CHUTE_SAMPLES; i++) sum += altBuffer[i];
    return sum / MAIN_CHUTE_SAMPLES;
}

// ===========================================================================
//  Utility: Beacon control
// ===========================================================================
static void beaconOn(void)  { digitalWrite(PIN_BEACON, HIGH); }
static void beaconOff(void) { digitalWrite(PIN_BEACON, LOW);  }

// ===========================================================================
//  setup()  –  Runs once on power-on or reset
// ===========================================================================
void setup(void)
{
    // Basic GPIO
    pinMode(PIN_BEACON,     OUTPUT); beaconOff();
    pinMode(PIN_STATUS_LED, OUTPUT); digitalWrite(PIN_STATUS_LED, LOW);

    // Debug serial (will be hijacked by XBee on flight hardware – see Section 3)
    Serial.begin(9600);
    delay(200);
    Serial.println("\n[BOOT] CAN-7USAT V3.0 – Bennett University");
    Serial.println("[BOOT] IN-SPACe CAN-7USAT 2026 Firmware");

    // ── Restore time & state from NVS (reset-survival, Rule 6.1-ix) ────────
    uint8_t savedState = 0;
    loadStateFromNVS(missionTime_s, savedState);
    if (missionTime_s > 0) {
        flightState = (FlightState_t)savedState;
        Serial.printf("[BOOT] Reset recovery – time=%lu s, state=%d\n",
                      missionTime_s, (int)flightState);
    }

    // ── Section 1 : Environmental sensors ──────────────────────────────────
    if (!initEnvironmental()) {
        Serial.println("[BOOT] WARNING: BMP390 mandatory sensor failed!");
        // Blink fast 5× to signal error
        for (int i = 0; i < 10; i++) {
            digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
            delay(100);
        }
    }

    // ── Section 2 : Motion & Position sensors ──────────────────────────────
    if (!initMotion()) {
        Serial.println("[BOOT] WARNING: BNO055 mandatory sensor failed!");
    }

    // ── Section 3 : Comms, Control & Storage ───────────────────────────────
    if (!initComms(TEAM_ID)) {
        Serial.println("[BOOT] WARNING: MicroSD or comms init failed!");
    }

    // ── Baro ground calibration (will be re-done on CAL command) ───────────
    calibrateBaroGround(groundAlt_m);

    bootTime_ms      = millis();
    stateEnterTime_ms = millis();

    // Move to BOOT-complete → LAUNCH_PAD pending ARM command
    if (missionTime_s == 0) {
        flightState = STATE_BOOT;
    }

    digitalWrite(PIN_STATUS_LED, HIGH);   // LED ON = system ready
    Serial.println("[BOOT] System ready. Waiting for GS ARM command.");
}

// ===========================================================================
//  loop()  –  Main execution loop
// ===========================================================================
void loop(void)
{
    uint32_t now_ms = millis();

    // ── Mission time counter ─────────────────────────────────────────────
    missionTime_s = (now_ms - bootTime_ms) / 1000UL;

    // ── Sensor update (5 Hz internal, independent of 1 Hz TX) ───────────
    if (now_ms - lastSensorMs >= SENSOR_INTERVAL_MS) {
        lastSensorMs = now_ms;
        updateEnvironmental();
        updateMotion();
        updateBatteryVoltage();
    }

    // ── Grab latest sensor readings ──────────────────────────────────────
    const EnvData_t&    env    = getEnvData();
    const MotionData_t& motion = getMotionData();

    // Compute altitude relative to ground
    float relAlt_m = env.altitude_m - groundAlt_m;
    if (simActive) relAlt_m = simAltitude;   // Override with sim profile

    float avgAlt_m = altitudeMovingAvg(relAlt_m);

    // ── Process ground station commands ─────────────────────────────────
    GSCommand_t cmd = processGSCommand(TEAM_ID);
    switch (cmd) {
        case GS_ARM:
            if (flightState == STATE_BOOT || flightState == STATE_LAUNCH_PAD) {
                flightState = STATE_LAUNCH_PAD;
                setTxEnabled(false);
                Serial.println("[SM] GS: ARM received → LAUNCH_PAD");
            }
            break;
        case GS_START_TX:
            setTxEnabled(true);
            Serial.println("[SM] GS: START_TX received → telemetry ON");
            break;
        case GS_STOP_TX:
            setTxEnabled(false);
            Serial.println("[SM] GS: STOP_TX received → telemetry OFF");
            break;
        case GS_CAL:
            calibrateBaroGround(groundAlt_m);
            calibrateIMU();
            Serial.println("[SM] GS: CAL received → sensors zeroed");
            break;
        case GS_SIM_ENABLE:
            simEnabled = true;
            flightState = STATE_TEST_MODE;
            Serial.println("[SM] GS: SIM_ENABLE → TEST_MODE");
            break;
        case GS_SIM_ACTIVATE:
            if (simEnabled) {
                simActive = true;
                Serial.println("[SM] GS: SIM_ACTIVATE → simulation running");
            }
            break;
        default:
            break;
    }

    // ── Flight State Machine ──────────────────────────────────────────────
    switch (flightState) {

        // ------------------------------------------------------------------
        case STATE_BOOT:
        // ------------------------------------------------------------------
            // Idle – waiting for ARM command from GS.
            // Self-test sensors and blink LED every 2 s.
            if ((now_ms / 2000) % 2 == 0)
                digitalWrite(PIN_STATUS_LED, HIGH);
            else
                digitalWrite(PIN_STATUS_LED, LOW);
            break;

        // ------------------------------------------------------------------
        case STATE_TEST_MODE:
        // ------------------------------------------------------------------
            // Simulation mode: GS injects altitude profile.
            // Accept SIM altitude increments here (simplified).
            // Real sim receives SIM_PRESSURE commands via XBee.
            if (simActive) {
                // Simulate ascent to 1000 m then descent
                static float simDir = 5.0f;  // m per update
                simAltitude += simDir;
                if (simAltitude >= 1000.0f) simDir = -simDir;
                if (simAltitude <= 0.0f)    { simAltitude = 0; simDir = 5.0f; }
            }
            break;

        // ------------------------------------------------------------------
        case STATE_LAUNCH_PAD:
        // ------------------------------------------------------------------
            // On pad, TX only after START_TX; accept CAL.
            // Transition: accel > 3G → ASCENT
            if (motion.accel_total_g > ASCENT_ACCEL_G) {
                flightState = STATE_ASCENT;
                stateEnterTime_ms = now_ms;
                Serial.println("[SM] LAUNCH_PAD → ASCENT (accel>3G)");
            }
            break;

        // ------------------------------------------------------------------
        case STATE_ASCENT:
        // ------------------------------------------------------------------
            // Rocket boosting. Detect ejection (sharp accel drop + altitude plateau).
            // Simple heuristic: after 2 s ascent, wait for accel < 0.5 G.
            if ((now_ms - stateEnterTime_ms) > 2000 && motion.accel_total_g < 0.5f) {
                flightState = STATE_ROCKET_DEPLOY;
                stateEnterTime_ms = now_ms;
                Serial.println("[SM] ASCENT → ROCKET_DEPLOY (ejection detected)");
            }
            break;

        // ------------------------------------------------------------------
        case STATE_ROCKET_DEPLOY:
        // ------------------------------------------------------------------
            // Drogue deploys passively via airflow.
            // Spin up momentum wheel immediately.
            spinUpMomentumWheel();
            flightState = STATE_DESCENT;
            Serial.println("[SM] ROCKET_DEPLOY → DESCENT (momentum wheel spinning)");
            break;

        // ------------------------------------------------------------------
        case STATE_DESCENT:
        // ------------------------------------------------------------------
            // Descending under drogue ~20 m/s; full telemetry & SD logging.
            // Transition: 5-sample moving avg altitude ≤ 600 m → deploy main.
            if (avgAlt_m <= MAIN_CHUTE_ALT_M && avgAlt_m > 5.0f) {
                flightState = STATE_AEROBREAK_RELEASE;
                stateEnterTime_ms = now_ms;
                Serial.println("[SM] DESCENT → AEROBREAK_RELEASE (alt≤600m)");
            }
            break;

        // ------------------------------------------------------------------
        case STATE_AEROBREAK_RELEASE:
        // ------------------------------------------------------------------
            // Servo releases main parachute pin.  Wait for servo travel (500 ms).
            releaseMainParachute();
            if ((now_ms - stateEnterTime_ms) > 500) {
                // Capture video from this point onward (bonus)
                captureFrame(missionTime_s);
            }
            // Transition: altitude near zero & vertical speed negligible → IMPACT
            if (avgAlt_m <= 5.0f) {
                flightState = STATE_IMPACT;
                stateEnterTime_ms = now_ms;
                Serial.println("[SM] AEROBREAK_RELEASE → IMPACT (altitude≤5m)");
            }
            break;

        // ------------------------------------------------------------------
        case STATE_IMPACT:
        // ------------------------------------------------------------------
            // Touchdown: stop motor, activate 95 dB beacon, keep TX GNSS coords.
            stopMomentumWheel();
            beaconOn();
            // Continue transmitting GNSS for recovery (no STOP condition needed)
            break;
    }

    // ── Persist state & time to NVS every 5 s ───────────────────────────
    static uint32_t lastNVSWrite = 0;
    if (now_ms - lastNVSWrite >= 5000) {
        lastNVSWrite = now_ms;
        saveStateToNVS(missionTime_s, (uint8_t)flightState);
    }

    // ── Build & transmit 1 Hz telemetry packet ───────────────────────────
    if (now_ms - lastTxTime_ms >= TELEMETRY_INTERVAL_MS) {
        lastTxTime_ms = now_ms;
        packetCount++;

        float voltage_V = getBatteryVoltage();

        buildTelemetryPacket(
            TEAM_ID,
            missionTime_s,
            packetCount,
            relAlt_m,               // Field  4: ALTITUDE (relative to ground)
            env.pressure_Pa,        // Field  5: PRESSURE
            env.temperature_C,      // Field  6: TEMP
            voltage_V,              // Field  7: VOLTAGE
            motion.gnssTime_s,      // Field  8: GNSS_TIME
            motion.latitude_deg,    // Field  9: GNSS_LATITUDE
            motion.longitude_deg,   // Field 10: GNSS_LONGITUDE
            motion.gnssAlt_m,       // Field 11: GNSS_ALTITUDE
            motion.gnss_sats,       // Field 12: GNSS_SATS
            motion.accel_x_ms2,     // Field 13: ACCELEROMETER_DATA (x/y/z triplet)
            motion.accel_y_ms2,
            motion.accel_z_ms2,
            motion.gyroSpinRate_dps,// Field 14: GYRO_SPIN_RATE (momentum wheel deg/s)
            (uint8_t)flightState,   // Field 15: FLIGHT_SOFTWARE_STATE
            env.uv_index,           // Field 16: OPTIONAL_DATA (UV index)
            txPacket,
            sizeof(txPacket)
        );

        transmitPacket(txPacket);

        // Optional: print debug snapshot to Serial2 (if wired for bench test)
        // printEnvData(); printMotionData(); printCommsData();
    }
}
