// =============================================================================
//  CAN-7USAT  |  SECTION 3 : COMMS, CONTROL & STORAGE MODULES
//  Bennett University – Astronomy Club / Space Tech Division
//  Firmware Version : V3.0  (Competition-Compliant, IN-SPACe 2026)
// =============================================================================
//
//  Modules covered in this file
//  ┌────┬──────────────────────┬──────────────────────────────────┬──────────┐
//  │ #  │  Module              │  Function                        │Interface │
//  ├────┼──────────────────────┼──────────────────────────────────┼──────────┤
//  │ 10 │ XBee Pro S2C         │ RF Telemetry downlink to GS      │  UART    │
//  │ 11 │ SG90 Servo           │ Main-chute pin release at 600 m  │  PWM     │
//  │ 12 │ MicroSD SPI          │ Onboard CSV data backup          │  SPI     │
//  │ 13 │ OV2640 Camera        │ Video/JPEG capture (optional)    │ SPI/SCCB │
//  │ 14 │ Resistor Divider ADC │ Battery + bus voltage monitor    │  ADC     │
//  └────┴──────────────────────┴──────────────────────────────────┴──────────┘
//
//  Pin map (from Section 9.1 of design doc):
//    UART0  XBee TX=GPIO1   RX=GPIO3
//    GPIO25 Servo PWM  (LEDC channel 1)
//    SPI    SD  CS=GPIO5  SCK=GPIO18  MISO=GPIO19  MOSI=GPIO23
//    GPIO35 Battery voltage divider → ADC1_CH7  (input-only, no pull-up)
//    OV2640 CS=GPIO15 (ArduCAM SPI mode; SCCB on Wire GPIO21/22)
//
//  Libraries required:
//    • SD          (ESP32 built-in)
//    • Preferences (ESP32 built-in – NVS)
//    • SPI         (ESP32 built-in)
// =============================================================================

#include "section3_comms.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>   // NVS for reset-survival (Rule 6.1-ix)

// ============================================================
//  MODULE #10  –  XBee Pro S2C  (RF Telemetry Downlink)
//  Interface : UART0  |  TX=GPIO1  RX=GPIO3
// ============================================================
#define XBEE_BAUD           9600    // XBee Pro S2C default baud

// ============================================================
//  MODULE #11  –  SG90 Servo  (Main Parachute Pin Release)
//  Interface : LEDC PWM  |  GPIO25
// ============================================================
#define PIN_SERVO           25
#define SERVO_LEDC_CHANNEL  1
#define SERVO_LEDC_FREQ_HZ  50      // 50 Hz – standard RC servo
#define SERVO_LEDC_RES      16      // 16-bit resolution: 0 – 65535
// duty = pulse_µs / 20000 × 65535
#define SERVO_US_LOCKED     1200    // µs – pin IN   → chute LOCKED
#define SERVO_US_RELEASED   1800    // µs – pin OUT  → chute RELEASED

// ============================================================
//  MODULE #12  –  MicroSD SPI  (Onboard CSV Backup Logging)
//  Interface : SPI  |  CS=GPIO5  SCK=GPIO18  MISO=GPIO19  MOSI=GPIO23
// ============================================================
#define SD_CS_PIN           5
#define SD_SCK_PIN          18
#define SD_MISO_PIN         19
#define SD_MOSI_PIN         23
#define SD_SPI_FREQ         4000000  // 4 MHz – conservative for flight vibration

// ============================================================
//  MODULE #14  –  Resistor Divider ADC  (Battery Voltage Monitor)
//  Interface : ADC1_CH7  |  GPIO35 (input-only)
//  Circuit   : 100 kΩ / 100 kΩ divider → V_adc × 2 = V_batt
// ============================================================
#define PIN_BATT_ADC        35
#define BATT_R1_OHMS        100000.0f
#define BATT_R2_OHMS        100000.0f
#define ADC_VREF_BATT_MV    3300.0f
#define ADC_MAX_CNT         4095.0f

// ============================================================
//  MODULE #13  –  OV2640 Camera  (Optional Video/JPEG Capture)
//  Interface : ArduCAM SPI  |  CS=GPIO15  |  SCCB=Wire (GPIO21/22)
// ============================================================
#define OV2640_CS_PIN       15

// ---------------------------------------------------------------------------
//  Internal module state
// ---------------------------------------------------------------------------
static bool        sdOK        = false;
static bool        cameraOK    = false;
static bool        xbeeOK      = false;
static bool        servoLocked = true;   // locked until AEROBREAK_RELEASE
static File        csvFile;              // open handle to Flight_<ID>.csv
static Preferences nvs;                 // ESP32 NVS partition

static CommsData_t commsData;            // public state snapshot

// ---------------------------------------------------------------------------
//  Detect ESP32 Arduino Core version to choose the correct LEDC API.
//  Core v2 uses ledcSetup / ledcAttachPin / ledcWrite.
//  Core v3 uses ledcAttach / ledcWriteDuty.
//  We use the v2 API here (works on both via compat shim in Core v3).
// ---------------------------------------------------------------------------

// ===========================================================================
//  Servo helper — convert pulse width (µs) to 16-bit LEDC duty count
// ===========================================================================
static inline uint32_t usToLedcDuty(uint32_t pulse_us)
{
    // period = 20 000 µs  →  duty = pulse_us * 65535 / 20000
    return (uint32_t)(((uint64_t)pulse_us * 65535UL) / 20000UL);
}

// ===========================================================================
//  initComms()
//  Initialises XBee UART, SG90 Servo, MicroSD, OV2640 stub, battery ADC,
//  and opens/creates the NVS partition for reset-survival.
//
//  Returns false if MicroSD init fails (backup logging is mandatory,
//  Rule 5.1-xi), so the caller can warn the crew.
// ===========================================================================
bool initComms(const char* teamID)
{
    // Zero the state struct FIRST before setting any individual fields
    memset(&commsData, 0, sizeof(CommsData_t));

    bool allOK = true;

    // ╔══════════════════════════════════════════════════════════╗
    // ║  MODULE #10  –  XBee Pro S2C                            ║
    // ║  Function  : RF Telemetry downlink to Ground Station     ║
    // ║  Interface : UART0  TX=GPIO1  RX=GPIO3  @ 9600 baud     ║
    // ║  Rule      : TX gated; only after ARM+START_TX  (6.1-ii)║
    // ╚══════════════════════════════════════════════════════════╝
    // Guard: only init if UART not already started (avoids RX FIFO flush)
    if (!Serial) {
        Serial.begin(XBEE_BAUD);
        delay(100);
    }
    xbeeOK              = true;
    commsData.txEnabled = false;
    Serial.println("[COM][M10-XBee] UART0 at 9600 baud — TX DISABLED until ARM+START_TX");

    // ╔══════════════════════════════════════════════════════════╗
    // ║  MODULE #11  –  SG90 Servo                              ║
    // ║  Function  : Retention-pin release → main parachute     ║
    // ║  Interface : LEDC PWM  GPIO25  @  50 Hz                 ║
    // ║  Starts    : LOCKED (1200 µs); Released at 600 m        ║
    // ╚══════════════════════════════════════════════════════════╝
    ledcSetup(SERVO_LEDC_CHANNEL, SERVO_LEDC_FREQ_HZ, SERVO_LEDC_RES);
    ledcAttachPin(PIN_SERVO, SERVO_LEDC_CHANNEL);
    ledcWrite(SERVO_LEDC_CHANNEL, usToLedcDuty(SERVO_US_LOCKED)); // start locked
    servoLocked             = true;
    commsData.servoReleased = false;
    Serial.println("[COM][M11-Servo] SG90 LOCKED on GPIO25 (1200µs)");

    // ╔══════════════════════════════════════════════════════════╗
    // ║  MODULE #12  –  MicroSD SPI                             ║
    // ║  Function  : Onboard CSV backup logging (Rule 5.1-xi)   ║
    // ║  Interface : SPI  CS=GPIO5 SCK=GPIO18 MISO=19 MOSI=23  ║
    // ║  File      : /Flight_<TEAM_ID>.csv  (append on reset)   ║
    // ╚══════════════════════════════════════════════════════════╝
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ)) {
        Serial.println("[COM][M12-SD] NOT FOUND – check card / wiring!");
        sdOK  = false;
        allOK = false;
    } else {
        sdOK              = true;
        commsData.sdReady = true;

        // Compute free space safely in uint64_t before float cast
        uint64_t totalBytes = SD.totalBytes();
        uint64_t usedBytes  = SD.usedBytes();
        uint64_t freeBytes  = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0ULL;
        Serial.printf("[COM][M12-SD] OK – %.0f MB free\n",
                      (float)freeBytes / (1024.0f * 1024.0f));

        // Filename: /Flight_<TEAM_ID>.csv  (Rule 6.3-ii)
        char fname[64];
        snprintf(fname, sizeof(fname), "/Flight_%s.csv", teamID);

        // Append mode: reset does NOT destroy previous data
        csvFile = SD.open(fname, FILE_APPEND);
        if (!csvFile) csvFile = SD.open(fname, FILE_WRITE); // create if new

        if (csvFile) {
            if (csvFile.size() == 0) {   // brand-new file → write header
                csvFile.println(
                    "TEAM_ID,TIME_STAMPING,PACKET_COUNT,ALTITUDE,PRESSURE,"
                    "TEMP,VOLTAGE,GNSS_TIME,GNSS_LATITUDE,GNSS_LONGITUDE,"
                    "GNSS_ALTITUDE,GNSS_SATS,ACCELEROMETER_DATA,"
                    "GYRO_SPIN_RATE,FLIGHT_SOFTWARE_STATE,OPTIONAL_DATA"
                );
                csvFile.flush();
                Serial.printf("[COM][M12-SD] New CSV: %s\n", fname);
            } else {
                Serial.printf("[COM][M12-SD] Appending to: %s (%u bytes)\n",
                              fname, (unsigned)csvFile.size());
            }
        } else {
            Serial.println("[COM][M12-SD] ERROR: failed to open/create CSV!");
            allOK = false;
        }
    }

    // ╔══════════════════════════════════════════════════════════╗
    // ║  MODULE #13  –  OV2640 Camera                           ║
    // ║  Function  : JPEG video capture separation→touchdown     ║
    // ║  Interface : ArduCAM SPI  CS=GPIO15  SCCB=Wire 0x30    ║
    // ║  Status    : Optional bonus; stub active, lib needed     ║
    // ╚══════════════════════════════════════════════════════════╝
    pinMode(OV2640_CS_PIN, OUTPUT);
    digitalWrite(OV2640_CS_PIN, HIGH);   // de-select SPI (active-low)
    cameraOK               = false;      // set true when ArduCAM lib is linked
    commsData.cameraActive = false;
    Serial.println("[COM][M13-CAM] OV2640 CS de-selected GPIO15 (ArduCAM lib needed)");

    // ╔══════════════════════════════════════════════════════════╗
    // ║  MODULE #14  –  Resistor Divider ADC                    ║
    // ║  Function  : Battery + bus voltage monitor (0.01 V res) ║
    // ║  Interface : ADC1_CH7  GPIO35  |  100kΩ / 100kΩ divider║
    // ║  Formula   : V_batt = V_adc × (R1+R2)/R2  = V_adc × 2  ║
    // ╚══════════════════════════════════════════════════════════╝
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);  // 0–3.3 V full-scale
    Serial.println("[COM][M14-ADC] Battery ADC active GPIO35 (100k/100k divider)");

    // -----------------------------------------------------------------------
    //  NVS — open "cansat" namespace for reset-survival storage
    // -----------------------------------------------------------------------
    if (!nvs.begin("cansat", false)) {
        Serial.println("[COM] WARNING: NVS init failed – reset recovery disabled");
        // Non-fatal; continue without NVS
    } else {
        Serial.println("[COM] NVS partition open (cansat namespace)");
    }

    return allOK;
}

// ===========================================================================
//  MODULE #14  –  Resistor Divider ADC
//  updateBatteryVoltage()
//  Reads GPIO35 ADC, applies divider ratio → actual 18650 voltage.
//  V_batt = V_adc × (100k + 100k) / 100k = V_adc × 2
// ===========================================================================
void updateBatteryVoltage(void)
{
    int   raw   = analogRead(PIN_BATT_ADC);  // MODULE #14 – Resistor Divider ADC
    float v_adc = (raw / ADC_MAX_CNT) * (ADC_VREF_BATT_MV / 1000.0f);  // V at GPIO35
    commsData.battery_V = v_adc * (BATT_R1_OHMS + BATT_R2_OHMS) / BATT_R2_OHMS;
}

// ===========================================================================
//  buildTelemetryPacket()
//  Formats the official 16-field ASCII CSV packet (Rule 6.3).
//  Packet is CR-terminated ('\r') as required by the competition spec.
//  Field 13 (ACCELEROMETER_DATA) encodes X/Y/Z separated by '/'
//  for compact single-field transport.
// ===========================================================================
void buildTelemetryPacket(
    const char* teamID,
    uint32_t    time_s,
    uint32_t    packetCount,
    float       altitude_m,
    float       pressure_Pa,
    float       temp_C,
    float       voltage_V,
    uint32_t    gnssTime,
    double      lat,
    double      lon,
    float       gnssAlt_m,
    uint8_t     sats,
    float       accel_x,
    float       accel_y,
    float       accel_z,
    float       gyroSpinRate_dps,
    uint8_t     flightState,
    float       uvIndex,
    char*       outBuf,
    size_t      bufLen)
{
    snprintf(outBuf, bufLen,
             "%s,%lu,%lu,%.1f,%.1f,%.1f,%.2f,%lu,%.4f,%.4f,%.1f,%d,"
             "%.2f/%.2f/%.2f,%.1f,%d,%.2f\r",
             teamID,
             (unsigned long)time_s,
             (unsigned long)packetCount,
             altitude_m,
             pressure_Pa,
             temp_C,
             voltage_V,
             (unsigned long)gnssTime,
             lat,
             lon,
             gnssAlt_m,
             (int)sats,
             accel_x, accel_y, accel_z,    // field 13: Ax/Ay/Az triplet
             gyroSpinRate_dps,              // field 14: momentum-wheel deg/s
             (int)flightState,              // field 15: state 0-7
             uvIndex);                      // field 16: UV index (OPTIONAL_DATA)
}

// ===========================================================================
//  transmitPacket()
//  MODULE #10 (XBee)  → RF downlink if TX enabled
//  MODULE #12 (SD)    → always write to CSV regardless of TX state
// ===========================================================================
void transmitPacket(const char* packet)
{
    // MODULE #10 – XBee Pro S2C: RF downlink after ARM+START_TX (Rule 6.1-ii)
    if (commsData.txEnabled && xbeeOK) {
        Serial.print(packet);   // UART0 → XBee → Ground Station
    }

    // MODULE #12 – MicroSD: unconditional backup (Rule 5.1-xi)
    if (sdOK && csvFile) {
        csvFile.print(packet);
        csvFile.flush();   // flush every packet – crash safety
    }
}

// ===========================================================================
//  processGSCommand()
//  Polls UART0 for a complete CR/LF-terminated GS command.
//  Returns the parsed GS command enum value or GS_NONE.
//
//  Expected format:  CAN-7USAT-CMD,<TEAM_ID>,<COMMAND>\r\n
//  Valid commands:   ARM | START_TX | STOP_TX | CAL | SIM_ENABLE | SIM_ACTIVATE
// ===========================================================================
GSCommand_t processGSCommand(const char* teamID)
{
    if (!Serial.available()) return GS_NONE;

    static char    rxBuf[128];
    static uint8_t rxIdx = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\r' || c == '\n') {
            if (rxIdx == 0) continue;   // skip empty lines
            rxBuf[rxIdx] = '\0';
            rxIdx = 0;

            // Build expected prefix
            char prefix[56];
            snprintf(prefix, sizeof(prefix), "CAN-7USAT-CMD,%s,", teamID);
            size_t prefixLen = strlen(prefix);

            if (strncmp(rxBuf, prefix, prefixLen) == 0) {
                const char* cmd = rxBuf + prefixLen;

                if      (strcmp(cmd, "ARM")          == 0) return GS_ARM;
                else if (strcmp(cmd, "START_TX")     == 0) {
                    commsData.txEnabled = true;
                    return GS_START_TX;
                }
                else if (strcmp(cmd, "STOP_TX")      == 0) {
                    commsData.txEnabled = false;
                    return GS_STOP_TX;
                }
                else if (strcmp(cmd, "CAL")          == 0) return GS_CAL;
                else if (strcmp(cmd, "SIM_ENABLE")   == 0) return GS_SIM_ENABLE;
                else if (strcmp(cmd, "SIM_ACTIVATE") == 0) return GS_SIM_ACTIVATE;
                else {
                    Serial.printf("[COM] Unknown GS command: '%s'\n", cmd);
                }
            }
        } else {
            // Buffer incoming character; silently drop if buffer full
            if (rxIdx < (uint8_t)(sizeof(rxBuf) - 1)) {
                rxBuf[rxIdx++] = c;
            } else {
                // Overflow: reset buffer and discard corrupt frame
                rxIdx = 0;
                Serial.println("[COM] RX buffer overflow – frame discarded");
            }
        }
    }
    return GS_NONE;
}

// ===========================================================================
//  MODULE #11  –  SG90 Servo
//  releaseMainParachute()
//  Moves servo to RELEASED position (1800 µs) → retention pin exits bay →
//  compression spring pushes main chute canopy into airstream.
//  Called once at AEROBREAK_RELEASE state (alt ≤ 600 m).
// ===========================================================================
void releaseMainParachute(void)
{
    if (!servoLocked) return;   // idempotent – only actuate once
    ledcWrite(SERVO_LEDC_CHANNEL, usToLedcDuty(SERVO_US_RELEASED)); // MODULE #11
    servoLocked             = false;
    commsData.servoReleased = true;
    Serial.println("[COM][M11-Servo] >>> MAIN PARACHUTE RELEASED <<< (1800µs, GPIO25)");

// ===========================================================================
//  MODULE #11  –  SG90 Servo
//  lockServo()  —  Return to LOCKED (1200 µs) for bench testing / re-arm
// ===========================================================================
void lockServo(void)
{
    ledcWrite(SERVO_LEDC_CHANNEL, usToLedcDuty(SERVO_US_LOCKED)); // MODULE #11
    servoLocked             = true;
    commsData.servoReleased = false;
    Serial.println("[COM][M11-Servo] Re-locked (1200µs) – test/re-arm");

// ===========================================================================
//  saveStateToNVS()  /  loadStateFromNVS()
//  Persist mission time and flight state to ESP32 NVS on every telemetry tick.
//  loadStateFromNVS() is called once in setup() to restore after any reset.
//  (Implements Rule 6.1-ix and 6.2-ii: mission clock must never restart.)
// ===========================================================================
void saveStateToNVS(uint32_t time_s, uint8_t flightState)
{
    nvs.putUInt ("time_s",    time_s);
    nvs.putUChar("flt_state", flightState);
}

void loadStateFromNVS(uint32_t& time_s, uint8_t& flightState)
{
    time_s      = nvs.getUInt ("time_s",    0);
    flightState = nvs.getUChar("flt_state", 0);
    if (time_s > 0 || flightState > 0) {
        Serial.printf("[COM] Reset recovery – restored time=%lu s, state=%d\n",
                      (unsigned long)time_s, (int)flightState);
    }
}

// ===========================================================================
//  MODULE #13  –  OV2640 Camera
//  captureFrame()  —  Trigger one JPEG snapshot → write to MicroSD (MODULE #12)
//  File: /img_<timestamp_s>.jpg
//  Requires ArduCAM or esp_camera library; stub provided below.
// ===========================================================================
void captureFrame(uint32_t timestamp)
{
    // MODULE #13 – OV2640 must be online; MODULE #12 – SD must be mounted
    if (!cameraOK || !sdOK) return;

    // ── ArduCAM SPI-mode stub ──────────────────────────────────────────────
    // Uncomment and adapt when the ArduCAM library is added to platformio.ini
    // myCAM.flush_fifo();
    // myCAM.clear_fifo_flag();
    // myCAM.start_capture();
    // while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) { delay(1); }
    // uint32_t fifoLen = myCAM.read_fifo_length();
    // char imgPath[32];
    // snprintf(imgPath, sizeof(imgPath), "/img_%lu.jpg", (unsigned long)timestamp);
    // File imgFile = SD.open(imgPath, FILE_WRITE);  // MODULE #12 – MicroSD
    // myCAM.CS_LOW();
    // myCAM.set_fifo_burst();
    // for (uint32_t i = 0; i < fifoLen; i++) imgFile.write(SPI.transfer(0xFF));
    // myCAM.CS_HIGH();
    // imgFile.close();
    // commsData.cameraActive = true;

    Serial.printf("[COM][M13-CAM] captureFrame t=%lu – link ArduCAM lib to enable\n",
                  (unsigned long)timestamp);
}

// ===========================================================================
//  Public accessors
// ===========================================================================
const CommsData_t& getCommsData(void)   { return commsData; }
void               setTxEnabled(bool en){ commsData.txEnabled = en; }

float getBatteryVoltage(void)
{
    updateBatteryVoltage();
    return commsData.battery_V;
}

// ===========================================================================
//  printCommsData()  —  Debug / bench helper
// ===========================================================================
void printCommsData(void)
{
    Serial.printf(
        "[COM] Batt=%.2fV  TX=%s  SD=%s  Servo=%s  Cam=%s\n",
        commsData.battery_V,
        commsData.txEnabled     ? "ON"       : "OFF",
        sdOK                    ? "OK"       : "FAIL",
        servoLocked             ? "LOCKED"   : "RELEASED",
        cameraOK                ? "ACTIVE"   : "N/A");
}
