// =============================================================================
//  CAN-7USAT  |  SECTION 2 : MOTION & POSITION SENSORS
//  Bennett University – Astronomy Club / Space Tech Division
//  Firmware Version : V3.0  (Competition-Compliant, IN-SPACe 2026)
// =============================================================================
//
//  Sensors covered in this file
//  ┌────┬─────────────────┬──────────────────────────────────────────┬────────┐
//  │ #  │  Sensor         │  Function                                │ Iface  │
//  ├────┼─────────────────┼──────────────────────────────────────────┼────────┤
//  │  6 │ BNO055          │ 9-DoF IMU – orientation, accel, gyro, mag│  I2C   │
//  │  7 │ A3144 Hall      │ Momentum-wheel RPM → GYRO_SPIN_RATE      │ Digital│
//  │  8 │ NavIC UART      │ Primary GNSS (lat, lon, alt, time)        │  UART  │
//  │  9 │ u-blox NEO-M9N  │ GNSS fallback receiver                   │  UART  │
//  └────┴─────────────────┴──────────────────────────────────────────┴────────┘
//
//  Pin map (from Section 9.1 of design doc):
//    I2C SDA=GPIO21  SCL=GPIO22   (shared with Section 1)
//    UART2 NavIC  RX=GPIO16  TX=GPIO17
//    UART1 NEO-M9N  RX=GPIO14  TX=GPIO13   (fallback; share UART2 if using one GNSS)
//    GPIO27  Hall sensor input  (interrupt-driven)
//    GPIO26  Gyro motor PWM  (MOSFET gate – also drives motor in Section 3)
//
//  Libraries required:
//    • Adafruit BNO055             (Adafruit)
//    • Adafruit Unified Sensor     (Adafruit)
//    • TinyGPSPlus                 (mikalhart)  – parses NMEA from both GNSS
// =============================================================================

#include "section2_motion.h"

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <TinyGPSPlus.h>

// ---------------------------------------------------------------------------
//  Pin definitions
// ---------------------------------------------------------------------------
#define PIN_HALL_SENSOR    27   // A3144 digital output (interrupt-capable)
#define PIN_GYRO_MOTOR_PWM 26   // MOSFET gate (ledc PWM → motor speed)

// UART2 – NavIC (primary GNSS)
#define NAVIC_UART_NUM     2
#define NAVIC_BAUD         9600
#define NAVIC_RX_PIN       16
#define NAVIC_TX_PIN       17

// UART1 – u-blox NEO-M9N (fallback GNSS)
#define NEOM9N_UART_NUM    1
#define NEOM9N_BAUD        9600
#define NEOM9N_RX_PIN      14
#define NEOM9N_TX_PIN      13

// ---------------------------------------------------------------------------
//  Motor PWM (LEDC) configuration for momentum wheel
// ---------------------------------------------------------------------------
#define MOTOR_LEDC_CHANNEL  0
#define MOTOR_LEDC_FREQ_HZ  20000   // 20 kHz – above audible range
#define MOTOR_LEDC_RES_BITS 10      // 0-1023 duty range
#define MOTOR_SPIN_DUTY     800     // ~78% duty → ~7000-10000 RPM (tunable)

// ---------------------------------------------------------------------------
//  Hall sensor RPM calculation
// ---------------------------------------------------------------------------
#define HALL_MAGNETS_ON_WHEEL  1    // Number of magnets on the rim
// Wheel circumference doesn't matter for deg/s conversion:
// deg/s = (pulses_per_sec / HALL_MAGNETS_ON_WHEEL) * 360

volatile uint32_t hallPulseCount   = 0;
volatile uint64_t lastHallTime_us  = 0;
static   float    wheelSpinRate_dps = 0.0f;  // current deg/s (updated by ISR calc)
static   uint64_t rpmCalcTime_us   = 0;

// ---------------------------------------------------------------------------
//  Sensor objects
// ---------------------------------------------------------------------------
static Adafruit_BNO055 bno(55, 0x28);   // I2C addr 0x28 (ADR pin LOW)
static TinyGPSPlus     gpsNavIC;
static TinyGPSPlus     gpsNEOM9N;

// UART handles (HardwareSerial)
HardwareSerial SerialNavIC(NAVIC_UART_NUM);
HardwareSerial SerialNEOM9N(NEOM9N_UART_NUM);

// ---------------------------------------------------------------------------
//  Module-level data cache
// ---------------------------------------------------------------------------
static MotionData_t motionData;
static bool bnoOK      = false;
static bool navicOK    = false;   // becomes true once first fix arrives
static bool neoOK      = false;   // fallback
static bool useNavIC   = true;    // prefer NavIC; fall back to NEO-M9N

// ---------------------------------------------------------------------------
//  HALL SENSOR  –  Interrupt Service Routine
//  Fires on FALLING edge of A3144 output (active-low when magnet passes)
// ---------------------------------------------------------------------------
void IRAM_ATTR hallISR(void)
{
    hallPulseCount++;
    lastHallTime_us = esp_timer_get_time();
}

// ===========================================================================
//  initMotion()
//  Initialises BNO055, Hall sensor ISR, Motor PWM, and both GNSS UARTs.
//  Returns true if BNO055 (mandatory sensor) is detected.
// ===========================================================================
bool initMotion(void)
{
    bool allOK = true;

    // -----------------------------------------------------------------------
    //  6. BNO055  –  9-DoF fused IMU  [MANDATORY: orientation & acceleration]
    // -----------------------------------------------------------------------
    if (!bno.begin()) {
        Serial.println("[MOT] BNO055 NOT FOUND – check wiring/address!");
        allOK = false;
        bnoOK = false;
    } else {
        bno.setExtCrystalUse(true);       // better timing accuracy
        // Use NDOF fusion mode: absolute orientation + calibrated accel/gyro/mag
        // (BNO055 default after begin() is NDOF)
        bnoOK = true;
        Serial.println("[MOT] BNO055  OK");
    }

    // -----------------------------------------------------------------------
    //  7. A3144 Hall Sensor  –  Momentum-wheel RPM
    // -----------------------------------------------------------------------
    pinMode(PIN_HALL_SENSOR, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_HALL_SENSOR), hallISR, FALLING);
    rpmCalcTime_us = esp_timer_get_time();
    Serial.println("[MOT] A3144 Hall ISR attached on GPIO27");

    // -----------------------------------------------------------------------
    //  Gyro Motor PWM  –  LEDC setup (spin-up begins at ROCKET_DEPLOY state)
    // -----------------------------------------------------------------------
    ledcSetup(MOTOR_LEDC_CHANNEL, MOTOR_LEDC_FREQ_HZ, MOTOR_LEDC_RES_BITS);
    ledcAttachPin(PIN_GYRO_MOTOR_PWM, MOTOR_LEDC_CHANNEL);
    ledcWrite(MOTOR_LEDC_CHANNEL, 0);   // Motor OFF until commanded
    Serial.println("[MOT] Motor PWM (LEDC ch0) configured on GPIO26");

    // -----------------------------------------------------------------------
    //  8. NavIC Primary GNSS  –  UART2
    // -----------------------------------------------------------------------
    SerialNavIC.begin(NAVIC_BAUD, SERIAL_8N1, NAVIC_RX_PIN, NAVIC_TX_PIN);
    Serial.println("[MOT] NavIC UART2 started (RX=16, TX=17, 9600 baud)");

    // -----------------------------------------------------------------------
    //  9. u-blox NEO-M9N  –  UART1 (fallback)
    // -----------------------------------------------------------------------
    SerialNEOM9N.begin(NEOM9N_BAUD, SERIAL_8N1, NEOM9N_RX_PIN, NEOM9N_TX_PIN);
    Serial.println("[MOT] NEO-M9N UART1 started (RX=14, TX=13, 9600 baud) [fallback]");

    memset(&motionData, 0, sizeof(MotionData_t));
    return allOK;
}

// ===========================================================================
//  updateHallRPM()
//  Called every 1-second tick.  Calculates wheel spin rate in deg/s
//  from accumulated pulse count, then resets counter.
// ===========================================================================
static void updateHallRPM(void)
{
    uint64_t now_us = esp_timer_get_time();
    float dt_s = (now_us - rpmCalcTime_us) / 1e6f;
    if (dt_s < 0.1f) return;   // wait for meaningful window

    noInterrupts();
    uint32_t pulses = hallPulseCount;
    hallPulseCount  = 0;
    interrupts();
    rpmCalcTime_us  = now_us;

    // pulses per second → revolutions per second → deg/s
    float rps = (float)pulses / (dt_s * (float)HALL_MAGNETS_ON_WHEEL);
    wheelSpinRate_dps = rps * 360.0f;
    motionData.gyroSpinRate_dps = wheelSpinRate_dps;
}

// ===========================================================================
//  updateGNSS()
//  Feed incoming UART bytes into TinyGPSPlus parsers.
//  Tries NavIC first; if no fix for >30 s, switches to NEO-M9N fallback.
// ===========================================================================
static void updateGNSS(void)
{
    // Feed NavIC UART bytes into parser
    while (SerialNavIC.available()) {
        gpsNavIC.encode(SerialNavIC.read());
    }

    // Feed NEO-M9N UART bytes into parser (always running as backup)
    while (SerialNEOM9N.available()) {
        gpsNEOM9N.encode(SerialNEOM9N.read());
    }

    // Select active GNSS source
    TinyGPSPlus* activeGPS = useNavIC ? &gpsNavIC : &gpsNEOM9N;

    // Auto-switch to fallback if NavIC has no valid fix after 30 s of boot
    if (useNavIC && !gpsNavIC.location.isValid() &&
        (esp_timer_get_time() / 1000000ULL) > 30) {
        if (gpsNEOM9N.location.isValid()) {
            useNavIC = false;
            activeGPS = &gpsNEOM9N;
            Serial.println("[MOT] GNSS: Switching to NEO-M9N fallback");
        }
    }

    // Copy fix data into motionData
    if (activeGPS->location.isValid()) {
        motionData.latitude_deg  = activeGPS->location.lat();
        motionData.longitude_deg = activeGPS->location.lng();
        motionData.gnssAlt_m     = activeGPS->altitude.meters();
        motionData.gnssTime_s    = activeGPS->time.value();   // HHMMSSCC
        motionData.gnss_sats     = activeGPS->satellites.value();
        motionData.gnssFixValid  = true;
    } else {
        motionData.gnssFixValid = false;
    }
    motionData.usingNavIC = useNavIC;
}

// ===========================================================================
//  updateMotion()
//  Master update – call every cycle from main loop (≥1 Hz).
// ===========================================================================
void updateMotion(void)
{
    // BNO055 – read Euler angles, quaternion, linear accel, angular velocity
    if (bnoOK) {
        sensors_event_t orientEv, accelEv, gyroEv;
        bno.getEvent(&orientEv, Adafruit_BNO055::VECTOR_EULER);
        bno.getEvent(&accelEv,  Adafruit_BNO055::VECTOR_LINEARACCEL);
        bno.getEvent(&gyroEv,   Adafruit_BNO055::VECTOR_GYROSCOPE);

        motionData.euler_heading_deg = orientEv.orientation.x;  // 0-360
        motionData.euler_roll_deg    = orientEv.orientation.y;
        motionData.euler_pitch_deg   = orientEv.orientation.z;

        motionData.accel_x_ms2 = accelEv.acceleration.x;
        motionData.accel_y_ms2 = accelEv.acceleration.y;
        motionData.accel_z_ms2 = accelEv.acceleration.z;
        motionData.accel_total_g =
            sqrtf(motionData.accel_x_ms2 * motionData.accel_x_ms2 +
                  motionData.accel_y_ms2 * motionData.accel_y_ms2 +
                  motionData.accel_z_ms2 * motionData.accel_z_ms2) / 9.81f;

        motionData.gyro_x_dps = gyroEv.gyro.x;
        motionData.gyro_y_dps = gyroEv.gyro.y;
        motionData.gyro_z_dps = gyroEv.gyro.z;

        // Calibration status (0=uncal, 3=fully calibrated)
        uint8_t sys, gyro, accel, mag;
        bno.getCalibration(&sys, &gyro, &accel, &mag);
        motionData.bno_cal_sys  = sys;
        motionData.bno_cal_gyro = gyro;
    }

    updateHallRPM();
    updateGNSS();
}

// ===========================================================================
//  setMotorDuty()
//  Set momentum-wheel motor PWM duty (0 = stop, 1023 = full).
//  Called from main.cpp state machine (spin-up on ROCKET_DEPLOY, stop on IMPACT).
// ===========================================================================
void setMotorDuty(uint16_t duty)
{
    if (duty > (1U << MOTOR_LEDC_RES_BITS) - 1)
        duty = (1U << MOTOR_LEDC_RES_BITS) - 1;
    ledcWrite(MOTOR_LEDC_CHANNEL, duty);
}

// ===========================================================================
//  spinUpMomentumWheel()  /  stopMomentumWheel()  —  convenience wrappers
// ===========================================================================
void spinUpMomentumWheel(void)  { setMotorDuty(MOTOR_SPIN_DUTY); }
void stopMomentumWheel(void)    { setMotorDuty(0); }

// ===========================================================================
//  getMotionData()
// ===========================================================================
const MotionData_t& getMotionData(void)
{
    return motionData;
}

// ===========================================================================
//  calibrateIMU()
//  Zero gyroscope bias + accel calibration via BNO055 CALIB command.
//  Called from main.cpp when GS sends CAL RF command (LAUNCH_PAD state).
// ===========================================================================
void calibrateIMU(void)
{
    if (!bnoOK) return;
    Serial.println("[MOT] BNO055 re-calibrating – keep unit still for 3 s...");
    // BNO055 performs self-calibration automatically in NDOF mode.
    // This function resets the user offset registers to allow fresh calibration.
    adafruit_bno055_offsets_t blank;
    memset(&blank, 0, sizeof(blank));
    bno.setSensorOffsets(blank);
    Serial.println("[MOT] BNO055 offsets cleared – auto-calibration running");
}

// ===========================================================================
//  printMotionData()  —  Debug helper
// ===========================================================================
void printMotionData(void)
{
    Serial.printf("[MOT] Hdg=%.1f Roll=%.1f Pitch=%.1f | Ax=%.2f Ay=%.2f Az=%.2f G | "
                  "WheelRPM=%.0f dps | Lat=%.6f Lon=%.6f Alt=%.1fm Sats=%d %s\n",
                  motionData.euler_heading_deg,
                  motionData.euler_roll_deg,
                  motionData.euler_pitch_deg,
                  motionData.accel_x_ms2,
                  motionData.accel_y_ms2,
                  motionData.accel_z_ms2,
                  motionData.gyroSpinRate_dps,
                  motionData.latitude_deg,
                  motionData.longitude_deg,
                  motionData.gnssAlt_m,
                  motionData.gnss_sats,
                  motionData.usingNavIC ? "(NavIC)" : "(NEO-M9N)");
}
