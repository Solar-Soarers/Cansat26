// main.cpp — Flight firmware for CAN-7USAT BU (ESP32, PlatformIO, Arduino)
// Implements FreeRTOS tasks, state machine, telemetry, SD logging, NVS persistence.

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <ESP32Servo.h>

// FreeRTOS primitives
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// Pin map (do not change)
#define PIN_I2C_SDA       21
#define PIN_I2C_SCL       22
#define PIN_GNSS_RX       16
#define PIN_GNSS_TX       17
#define PIN_XBEE_TX        1
#define PIN_XBEE_RX        3
#define PIN_SD_CS          5
#define PIN_SD_SCK        18
#define PIN_SD_MISO       19
#define PIN_SD_MOSI       23
#define PIN_SERVO         25
#define PIN_GYRO_PWM      26
#define PIN_HALL          27
#define PIN_UV            34
#define PIN_VBAT          35
#define PIN_BEACON         4
#define PIN_STATUS_LED     2

// Constants
#define TEAM_ID               "2026-IN-SPACe-CAN-7USAT-BU"
#define TELEMETRY_HZ          1
#define ALTITUDE_MAIN_DEPLOY  600.0f
#define ALTITUDE_IMPACT       5.0f
#define ACCEL_LAUNCH_THRESH   30.0f
#define ACCEL_EJECTION_THRESH 5.0f
#define GYRO_TARGET_PWM       180
#define SERVO_LOCKED_DEG      0
#define SERVO_RELEASE_DEG     90
#define ALT_MOVING_AVG_N      5
#define NVS_PERSIST_INTERVAL  10
#define VBAT_DIVIDER_RATIO    2.0f
#define UV_MV_PER_INDEX       100.0f
#define SD_FILENAME           "/Flight_2026-IN-SPACe-CAN-7USAT-BU.csv"
#define CSV_HEADER            "TEAM_ID,TIME_STAMPING,PACKET_COUNT,ALTITUDE,PRESSURE,TEMP,VOLTAGE,GNSS_TIME,GNSS_LATITUDE,GNSS_LONGITUDE,GNSS_ALTITUDE,GNSS_SATS,ACCELEROMETER_DATA,GYRO_SPIN_RATE,FLIGHT_SOFTWARE_STATE,OPTIONAL_DATA\r\n"

// Debug guard
#define DEBUG_VERBOSE false

// SensorData struct and external declaration (provided by sensor team)
struct SensorData {
    float     altitude_m;
    float     pressure_pa;
    float     temp_c;

    float     pitch_deg;
    float     roll_deg;
    float     yaw_deg;
    float     accel_x;
    float     accel_y;
    float     accel_z;
    bool      imu_calibrated;

    float     gnss_lat;
    float     gnss_lon;
    float     gnss_alt_m;
    uint8_t   gnss_sats;
    uint32_t  gnss_time_s;
    bool      gnss_fix;

    float     pm1_0;
    float     pm2_5;
    float     pm4_0;
    float     pm10;
    float     co2_ppm;
    float     humidity_pct;
    float     temp_scd_c;
    float     no2_ppm;
    float     co_ppm;
    float     aqi_raw;

    float     uv_index;

    SemaphoreHandle_t mutex;
};

extern SensorData g_sensors;

// Flight state enum
enum class FlightState : uint8_t {
    BOOT = 0,
    TEST_MODE = 1,
    LAUNCH_PAD = 2,
    ASCENT = 3,
    ROCKET_DEPLOY = 4,
    DESCENT = 5,
    AEROBREAK_RELEASE = 6,
    IMPACT = 7
};

// FreeRTOS objects and globals
static EventGroupHandle_t sensorEventGroup;
static const EventBits_t SENSOR_REFRESH_BIT = (1 << 0);
static QueueHandle_t sdQueue;
struct TelemetryMsg { char buf[256]; };
static Preferences nvspref;
static Servo mainServo;
static TimerHandle_t servoRetractTimer;
static TimerHandle_t gyroTimer; // 1000ms timer to compute gyro rate

// Persistent state
static uint32_t packetCount = 0;
static uint32_t missionTimeSec = 0;
static FlightState currentState = FlightState::BOOT;
static FlightState prevState = FlightState::BOOT;
static float altitudeGroundOffset = 0.0f;

// Transmit control
static bool txEnabled = false;

// SD/LED status
static bool sdReady = false;
static int sdWriteFails = 0;

// Gyro hall counter
volatile uint32_t hallPulseCount = 0;
static float gyro_spin_rate_degs = 0.0f; // computed every 1000ms by timer

// Local ADC readings
static float batteryVoltage = 0.0f;
static float uvIndexLocal = 0.0f;
static float vbatRolling[4] = {0};
static uint8_t vbatIdx = 0;

// Simulation controls
static bool simModeActive = false;
static float g_sim_altitude = 0.0f;

// Forward declarations
void sensorTask(void* pv);
void controlTask(void* pv);
void telemetryTask(void* pv);
void sdTask(void* pv);
void servoRetractTimerCb(TimerHandle_t xTimer);
void gyroTimerCb(TimerHandle_t xTimer);
void hallISR();
void servoRelease();
String formatTelemetryCSV(uint32_t timestampSec, uint32_t pktNo, float effectiveAltitude);
void evaluateStateMachine(float effectiveAltitude);
float readEffectiveAltitude();
void persistNVSImmediate();

// ISR increments the hall pulse count on falling edge
void IRAM_ATTR hallISR() {
    portENTER_CRITICAL_ISR(NULL);
    hallPulseCount++;
    portEXIT_CRITICAL_ISR(NULL);
}

// Timer callback to retract servo after 1s
void servoRetractTimerCb(TimerHandle_t xTimer) {
    mainServo.write(SERVO_LOCKED_DEG);
    mainServo.detach();
}

// Timer callback runs every 1000ms to compute gyro spin rate and reset counter
void gyroTimerCb(TimerHandle_t xTimer) {
    portENTER_CRITICAL(NULL);
    uint32_t pulses = hallPulseCount;
    hallPulseCount = 0;
    portEXIT_CRITICAL(NULL);
    gyro_spin_rate_degs = pulses * 360.0f; // pulses per second * 360 = deg/s
}

// One-line comment: trigger the servo release non-blocking using a one-shot timer.
void servoRelease() {
    mainServo.attach(PIN_SERVO);
    mainServo.write(SERVO_RELEASE_DEG);
    xTimerStart(servoRetractTimer, 0);
}

// One-line comment: compute an effective altitude that respects sim mode.
float readEffectiveAltitude() {
    if (simModeActive) return g_sim_altitude;
    float alt = 0.0f;
    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, (TickType_t)10);
    alt = g_sensors.altitude_m - altitudeGroundOffset;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);
    return alt;
}

// One-line comment: format telemetry CSV exactly as specified and return as String.
String formatTelemetryCSV(uint32_t timestampSec, uint32_t pktNo, float effectiveAltitude) {
    char buf[256];
    // Acquire minimal sensor snapshot
    float pressure = 0.0f, temp = 0.0f, gnss_lat = 0.0f, gnss_lon = 0.0f, gnss_alt = 0.0f;
    uint8_t gnss_sats = 0;
    uint32_t gnss_time = 0;
    float ax=0, ay=0, az=0, pitch=0, roll=0;
    float pm25=0.0f, co2=0.0f, aqi=0.0f;

    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
    pressure = g_sensors.pressure_pa;
    temp = g_sensors.temp_c;
    gnss_lat = g_sensors.gnss_lat;
    gnss_lon = g_sensors.gnss_lon;
    gnss_alt = g_sensors.gnss_alt_m;
    gnss_sats = g_sensors.gnss_sats;
    gnss_time = g_sensors.gnss_time_s;
    ax = g_sensors.accel_x; ay = g_sensors.accel_y; az = g_sensors.accel_z;
    pitch = g_sensors.pitch_deg; roll = g_sensors.roll_deg;
    pm25 = g_sensors.pm2_5; co2 = g_sensors.co2_ppm; aqi = g_sensors.aqi_raw;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

    // Accelerometer formatted field
    char accelField[64];
    snprintf(accelField, sizeof(accelField), "%.2f/%.2f/%.2f/%.2f/%.2f", ax, ay, az, pitch, roll);

    // Optional data
    char optionalField[80];
    snprintf(optionalField, sizeof(optionalField), "UV=%.2f/PM2.5=%.1f/CO2=%.0f/AQI=%.2f", uvIndexLocal, pm25, co2, aqi);

    // Compose final CSV line
    snprintf(buf, sizeof(buf), "%s,%u,%u,%.1f,%u,%.1f,%.2f,%u,%.4f,%.4f,%.1f,%u,%s,%.1f,%u,%s\r\n",
             TEAM_ID,
             (unsigned)timestampSec,
             (unsigned)pktNo,
             effectiveAltitude,
             (unsigned)pressure,
             temp,
             batteryVoltage,
             (unsigned)gnss_time,
             gnss_lat,
             gnss_lon,
             gnss_alt,
             (unsigned)gnss_sats,
             accelField,
             gyro_spin_rate_degs,
             (unsigned)currentState,
             optionalField);

    return String(buf);
}

// One-line comment: write persistent values to NVS immediately.
void persistNVSImmediate() {
    nvspref.putUInt("pkt_count", packetCount);
    nvspref.putUInt("mission_t", missionTimeSec);
    nvspref.putUInt("flt_state", (uint32_t)currentState);
    nvspref.putFloat("alt_offset", altitudeGroundOffset);
}

// One-line comment: evaluate and perform state transitions based on sensor inputs.
void evaluateStateMachine(float effectiveAltitude) {
    prevState = currentState;

    float accelZ = 0.0f;
    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, (TickType_t)10);
    accelZ = g_sensors.accel_z;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

    static bool seenHighAccel = false;
    static int launchDetectCount = 0;
    static int impactAltCount = 0;

    switch (currentState) {
        case FlightState::BOOT:
            break;
        case FlightState::TEST_MODE:
            break;
        case FlightState::LAUNCH_PAD:
            if (accelZ > ACCEL_LAUNCH_THRESH) {
                launchDetectCount++;
                if (launchDetectCount >= 3) {
                    currentState = FlightState::ASCENT;
                    launchDetectCount = 0;
                }
            } else launchDetectCount = 0;
            break;
        case FlightState::ASCENT:
            if (accelZ > ACCEL_LAUNCH_THRESH) seenHighAccel = true;
            if (seenHighAccel && accelZ < ACCEL_EJECTION_THRESH) {
                currentState = FlightState::ROCKET_DEPLOY;
                seenHighAccel = false;
            }
            break;
        case FlightState::ROCKET_DEPLOY:
            if (effectiveAltitude < 980.0f && effectiveAltitude < altitudeGroundOffset + 20000.0f) {
            }
            if (effectiveAltitude < ALTITUDE_MAIN_DEPLOY * 2 && currentState == FlightState::ROCKET_DEPLOY) {
                currentState = FlightState::DESCENT;
            }
            break;
        case FlightState::DESCENT:
            static float altBuf[ALT_MOVING_AVG_N] = {0};
            static uint8_t altIdx = 0; static uint8_t altCount = 0;
            altBuf[altIdx++] = effectiveAltitude;
            if (altIdx >= ALT_MOVING_AVG_N) altIdx = 0;
            if (altCount < ALT_MOVING_AVG_N) altCount++;
            float avg = 0; for (uint8_t i=0;i<altCount;i++) avg += altBuf[i];
            avg = (altCount>0) ? (avg/altCount) : effectiveAltitude;
            if (avg <= ALTITUDE_MAIN_DEPLOY && avg <= 600.0f) {
                currentState = FlightState::AEROBREAK_RELEASE;
            }
            break;
        case FlightState::AEROBREAK_RELEASE:
            if (effectiveAltitude < ALTITUDE_IMPACT) {
                impactAltCount++;
                if (impactAltCount >= 5) currentState = FlightState::IMPACT;
            }
            break;
        case FlightState::IMPACT:
            break;
        default:
            break;
    }

    if (currentState != prevState) {
        persistNVSImmediate();
        if (DEBUG_VERBOSE) Serial.printf("[STATE] Transition %u -> %u\n", (unsigned)prevState, (unsigned)currentState);
        if (currentState == FlightState::ROCKET_DEPLOY) {
        }
        if (currentState == FlightState::AEROBREAK_RELEASE) {
            servoRelease();
        }
        if (currentState == FlightState::IMPACT) {
            ledcWrite(0, 0);
            digitalWrite(PIN_BEACON, HIGH);
        }
    }
}

// One-line comment: parse incoming RF commands from Serial lines.
void parseRFCommand(const String &cmd) {
    if (cmd == "ARM") {
        txEnabled = true;
        if (DEBUG_VERBOSE) Serial.println("[RF] ARM received");
    } else if (cmd == "STOP_TX") {
        txEnabled = false;
    } else if (cmd == "CAL") {
        if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
        altitudeGroundOffset = g_sensors.altitude_m;
        if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);
        TelemetryMsg msg; snprintf(msg.buf, sizeof(msg.buf), "[CAL] Ground reference zeroed\r\n");
        xQueueSend(sdQueue, &msg, 0);
    } else if (cmd == "SIM_ENABLE") {
        if (currentState == FlightState::BOOT || currentState == FlightState::LAUNCH_PAD) {
            currentState = FlightState::TEST_MODE;
        }
    } else if (cmd == "SIM_ACTIVATE") {
        if (currentState == FlightState::TEST_MODE) {
            simModeActive = true;
            g_sim_altitude = 0.0f;
        }
    } else if (cmd == "RESET_SIM") {
        simModeActive = false;
    }
}

// One-line comment: task that reads local ADCs and signals sensor refresh.
void sensorTask(void* pv) {
    Serial.println("[TASK] sensorTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100);
    while (true) {
        int rawUV = analogRead(PIN_UV);
        float vout = (rawUV / 4095.0f) * 3.3f;
        uvIndexLocal = vout / 0.1f;
        if (uvIndexLocal > 15.0f) uvIndexLocal = 15.0f;

        int rawV = analogRead(PIN_VBAT);
        float v = (rawV / 4095.0f) * 3.3f * VBAT_DIVIDER_RATIO;
        vbatRolling[vbatIdx++ & 3] = v;
        float vavg = 0; for (int i=0;i<4;i++) vavg += vbatRolling[i];
        batteryVoltage = vavg / 4.0f;

        xEventGroupSetBits(sensorEventGroup, SENSOR_REFRESH_BIT);

        vTaskDelayUntil(&lastWake, period);
    }
}

// One-line comment: primary control loop handling state machine and motor/LED patterns.
void controlTask(void* pv) {
    Serial.println("[TASK] controlTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(50);

    int ledCounter = 0;
    int gyroPwm = 0;
    while (true) {
        float effAlt = readEffectiveAltitude();
        evaluateStateMachine(effAlt);

        if (currentState == FlightState::ROCKET_DEPLOY) {
            if (gyroPwm < GYRO_TARGET_PWM) gyroPwm = min(gyroPwm + 5, GYRO_TARGET_PWM);
        } else if (currentState == FlightState::DESCENT || currentState == FlightState::AEROBREAK_RELEASE) {
            gyroPwm = GYRO_TARGET_PWM;
        } else if (currentState == FlightState::IMPACT) {
            gyroPwm = 0;
        }
        ledcWrite(0, gyroPwm);

        bool sdFast = (sdWriteFails > 3);
        if (sdFast) {
            digitalWrite(PIN_STATUS_LED, (ledCounter % 5) < 2);
        } else {
            switch (currentState) {
                case FlightState::BOOT: digitalWrite(PIN_STATUS_LED, (ledCounter % 40) < 20); break;
                case FlightState::LAUNCH_PAD: digitalWrite(PIN_STATUS_LED, HIGH); break;
                case FlightState::ASCENT: digitalWrite(PIN_STATUS_LED, (ledCounter % 10) < 5); break;
                case FlightState::ROCKET_DEPLOY: digitalWrite(PIN_STATUS_LED, (ledCounter%20<2) || (ledCounter%20>=4 && ledCounter%20<6)); break;
                case FlightState::DESCENT: digitalWrite(PIN_STATUS_LED, (ledCounter % 20) < 10); break;
                case FlightState::AEROBREAK_RELEASE: digitalWrite(PIN_STATUS_LED, (ledCounter%20<3)); break;
                case FlightState::IMPACT: digitalWrite(PIN_STATUS_LED, HIGH); break;
                default: digitalWrite(PIN_STATUS_LED, LOW); break;
            }
        }

        ledCounter++;
        vTaskDelayUntil(&lastWake, period);
    }
}

// One-line comment: telemetryTask runs at 1Hz, sends CSV, queues SD writes, parses RF commands.
void telemetryTask(void* pv) {
    Serial.println("[TASK] telemetryTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000);

    char cmdBuf[64]; size_t cmdIdx = 0;
    while (true) {
        vTaskDelayUntil(&lastWake, period);

        missionTimeSec = millis() / 1000;

        float effAlt = readEffectiveAltitude();

        if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
        if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

        packetCount++;

        String csv = formatTelemetryCSV(missionTimeSec, packetCount, effAlt);
        if (txEnabled) {
            Serial.print(csv);
        }

        TelemetryMsg msg; strncpy(msg.buf, csv.c_str(), sizeof(msg.buf)-1); msg.buf[sizeof(msg.buf)-1]='\0';
        xQueueSend(sdQueue, &msg, 0);

        if ((packetCount % NVS_PERSIST_INTERVAL) == 0) {
            nvspref.putUInt("pkt_count", packetCount);
            nvspref.putUInt("mission_t", missionTimeSec);
            nvspref.putUInt("flt_state", (uint32_t)currentState);
        }

        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (cmdIdx > 0) {
                    cmdBuf[cmdIdx] = '\0';
                    parseRFCommand(String(cmdBuf));
                    cmdIdx = 0;
                }
            } else {
                if (cmdIdx < sizeof(cmdBuf)-1) cmdBuf[cmdIdx++] = c;
            }
        }
    }
}

// One-line comment: sdTask receives CSV strings and writes to SD card safely.
void sdTask(void* pv) {
    Serial.println("[TASK] sdTask started");
    TelemetryMsg msg;
    while (true) {
        if (xQueueReceive(sdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!sdReady) {
                sdWriteFails++;
                continue;
            }
            bool needHeader = !SD.exists(SD_FILENAME);
            File f = SD.open(SD_FILENAME, FILE_APPEND);
            if (!f) {
                sdWriteFails++;
                continue;
            }
            if (needHeader) {
                f.print(CSV_HEADER);
            }
            if (!f.print(msg.buf)) {
                sdWriteFails++;
            } else {
                sdWriteFails = 0;
            }
            f.close();
        }
    }
}

// Setup sequence per spec
void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);

    pinMode(PIN_SERVO, OUTPUT);
    pinMode(PIN_GYRO_PWM, OUTPUT);
    pinMode(PIN_BEACON, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_HALL, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_HALL), hallISR, FALLING);

    ledcSetup(0, 25000, 8);
    ledcAttachPin(PIN_GYRO_PWM, 0);

    mainServo.attach(PIN_SERVO);
    mainServo.write(SERVO_LOCKED_DEG);
    mainServo.detach();

    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdReady = SD.begin(PIN_SD_CS);

    // Preferences restore
    nvspref.begin("cansat_nv", false);
    packetCount = nvspref.getUInt("pkt_count", 0);
    missionTimeSec = nvspref.getUInt("mission_t", 0);
    uint32_t persistedState = nvspref.getUInt("flt_state", 0);
    altitudeGroundOffset = nvspref.getFloat("alt_offset", 0.0f);
    currentState = static_cast<FlightState>(persistedState);
    if ((uint8_t)currentState != 0) {
        Serial.printf("[NVS] Resuming from state %u, packet %u, time %us\n", (unsigned)currentState, packetCount, missionTimeSec);
    }

    // Create queue and event group
    sdQueue = xQueueCreate(10, sizeof(TelemetryMsg));
    sensorEventGroup = xEventGroupCreate();

    // Create timers
    servoRetractTimer = xTimerCreate("servoRetract", pdMS_TO_TICKS(1000), pdFALSE, NULL, servoRetractTimerCb);
    gyroTimer = xTimerCreate("gyroTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, gyroTimerCb);
    xTimerStart(gyroTimer, 0);

    // Create tasks (stack sizes as bytes)
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(controlTask, "controlTask", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(telemetryTask, "telemetryTask", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(sdTask, "sdTask", 4096, NULL, 1, NULL, 0);

    // Final boot message
    currentState = FlightState::BOOT;
    Serial.println("[BOOT] CAN-7USAT v3.0 — BU Astronomy Club — All systems init");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
// main.cpp — Flight firmware for CAN-7USAT BU (ESP32, PlatformIO, Arduino)
// Implements FreeRTOS tasks, state machine, telemetry, SD logging, NVS persistence.

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <ESP32Servo.h>

// FreeRTOS primitives
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// Pin map (do not change)
#define PIN_I2C_SDA       21
#define PIN_I2C_SCL       22
#define PIN_GNSS_RX       16
#define PIN_GNSS_TX       17
#define PIN_XBEE_TX        1
#define PIN_XBEE_RX        3
#define PIN_SD_CS          5
#define PIN_SD_SCK        18
#define PIN_SD_MISO       19
#define PIN_SD_MOSI       23
#define PIN_SERVO         25
#define PIN_GYRO_PWM      26
#define PIN_HALL          27
#define PIN_UV            34
#define PIN_VBAT          35
#define PIN_BEACON         4
#define PIN_STATUS_LED     2

// Constants
#define TEAM_ID               "2026-IN-SPACe-CAN-7USAT-BU"
#define TELEMETRY_HZ          1
#define ALTITUDE_MAIN_DEPLOY  600.0f
#define ALTITUDE_IMPACT       5.0f
#define ACCEL_LAUNCH_THRESH   30.0f
#define ACCEL_EJECTION_THRESH 5.0f
#define GYRO_TARGET_PWM       180
#define SERVO_LOCKED_DEG      0
#define SERVO_RELEASE_DEG     90
#define ALT_MOVING_AVG_N      5
#define NVS_PERSIST_INTERVAL  10
#define VBAT_DIVIDER_RATIO    2.0f
#define UV_MV_PER_INDEX       100.0f
#define SD_FILENAME           "/Flight_2026-IN-SPACe-CAN-7USAT-BU.csv"
#define CSV_HEADER            "TEAM_ID,TIME_STAMPING,PACKET_COUNT,ALTITUDE,PRESSURE,TEMP,VOLTAGE,GNSS_TIME,GNSS_LATITUDE,GNSS_LONGITUDE,GNSS_ALTITUDE,GNSS_SATS,ACCELEROMETER_DATA,GYRO_SPIN_RATE,FLIGHT_SOFTWARE_STATE,OPTIONAL_DATA\r\n"

// Debug guard
#define DEBUG_VERBOSE false

// SensorData struct and external declaration (provided by sensor team)
struct SensorData {
    float     altitude_m;
    float     pressure_pa;
    float     temp_c;

    float     pitch_deg;
    float     roll_deg;
    float     yaw_deg;
    float     accel_x;
    float     accel_y;
    float     accel_z;
    bool      imu_calibrated;

    float     gnss_lat;
    float     gnss_lon;
    float     gnss_alt_m;
    uint8_t   gnss_sats;
    uint32_t  gnss_time_s;
    bool      gnss_fix;

    float     pm1_0;
    float     pm2_5;
    float     pm4_0;
    float     pm10;
    float     co2_ppm;
    float     humidity_pct;
    float     temp_scd_c;
    float     no2_ppm;
    float     co_ppm;
    float     aqi_raw;

    float     uv_index;

    SemaphoreHandle_t mutex;
};

extern SensorData g_sensors;

// Flight state enum
enum class FlightState : uint8_t {
    BOOT = 0,
    TEST_MODE = 1,
    LAUNCH_PAD = 2,
    ASCENT = 3,
    ROCKET_DEPLOY = 4,
    DESCENT = 5,
    AEROBREAK_RELEASE = 6,
    IMPACT = 7
};

// FreeRTOS objects and globals
static EventGroupHandle_t sensorEventGroup;
static const EventBits_t SENSOR_REFRESH_BIT = (1 << 0);
static QueueHandle_t sdQueue;
struct TelemetryMsg { char buf[256]; };
static Preferences nvspref;
static Servo mainServo;
static TimerHandle_t servoRetractTimer;
static TimerHandle_t gyroTimer; // 1000ms timer to compute gyro rate

// Persistent state
static uint32_t packetCount = 0;
static uint32_t missionTimeSec = 0;
static FlightState currentState = FlightState::BOOT;
static FlightState prevState = FlightState::BOOT;
static float altitudeGroundOffset = 0.0f;

// Transmit control
static bool txEnabled = false;

// SD/LED status
static bool sdReady = false;
static int sdWriteFails = 0;

// Gyro hall counter
volatile uint32_t hallPulseCount = 0;
static float gyro_spin_rate_degs = 0.0f; // computed every 1000ms by timer

// Local ADC readings
static float batteryVoltage = 0.0f;
static float uvIndexLocal = 0.0f;
static float vbatRolling[4] = {0};
static uint8_t vbatIdx = 0;

// Simulation controls
static bool simModeActive = false;
static float g_sim_altitude = 0.0f;

// Forward declarations
void sensorTask(void* pv);
void controlTask(void* pv);
void telemetryTask(void* pv);
void sdTask(void* pv);
void servoRetractTimerCb(TimerHandle_t xTimer);
void gyroTimerCb(TimerHandle_t xTimer);
void hallISR();
void servoRelease();
String formatTelemetryCSV(uint32_t timestampSec, uint32_t pktNo, float effectiveAltitude);
void evaluateStateMachine(float effectiveAltitude);
float readEffectiveAltitude();
void persistNVSImmediate();

// ISR increments the hall pulse count on falling edge
void IRAM_ATTR hallISR() {
    portENTER_CRITICAL_ISR(NULL);
    hallPulseCount++;
    portEXIT_CRITICAL_ISR(NULL);
}

// Timer callback to retract servo after 1s
void servoRetractTimerCb(TimerHandle_t xTimer) {
    mainServo.write(SERVO_LOCKED_DEG);
    mainServo.detach();
}

// Timer callback runs every 1000ms to compute gyro spin rate and reset counter
void gyroTimerCb(TimerHandle_t xTimer) {
    portENTER_CRITICAL(NULL);
    uint32_t pulses = hallPulseCount;
    hallPulseCount = 0;
    portEXIT_CRITICAL(NULL);
    gyro_spin_rate_degs = pulses * 360.0f; // pulses per second * 360 = deg/s
}

// One-line comment: trigger the servo release non-blocking using a one-shot timer.
void servoRelease() {
    mainServo.attach(PIN_SERVO);
    mainServo.write(SERVO_RELEASE_DEG);
    xTimerStart(servoRetractTimer, 0);
}

// One-line comment: compute an effective altitude that respects sim mode.
float readEffectiveAltitude() {
    if (simModeActive) return g_sim_altitude;
    float alt = 0.0f;
    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, (TickType_t)10);
    alt = g_sensors.altitude_m - altitudeGroundOffset;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);
    return alt;
}

// One-line comment: format telemetry CSV exactly as specified and return as String.
String formatTelemetryCSV(uint32_t timestampSec, uint32_t pktNo, float effectiveAltitude) {
    char buf[256];
    // Acquire minimal sensor snapshot
    float pressure = 0.0f, temp = 0.0f, gnss_lat = 0.0f, gnss_lon = 0.0f, gnss_alt = 0.0f;
    uint8_t gnss_sats = 0;
    uint32_t gnss_time = 0;
    float ax=0, ay=0, az=0, pitch=0, roll=0;
    float pm25=0.0f, co2=0.0f, aqi=0.0f;

    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
    pressure = g_sensors.pressure_pa;
    temp = g_sensors.temp_c;
    gnss_lat = g_sensors.gnss_lat;
    gnss_lon = g_sensors.gnss_lon;
    gnss_alt = g_sensors.gnss_alt_m;
    gnss_sats = g_sensors.gnss_sats;
    gnss_time = g_sensors.gnss_time_s;
    ax = g_sensors.accel_x; ay = g_sensors.accel_y; az = g_sensors.accel_z;
    pitch = g_sensors.pitch_deg; roll = g_sensors.roll_deg;
    pm25 = g_sensors.pm2_5; co2 = g_sensors.co2_ppm; aqi = g_sensors.aqi_raw;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

    // Accelerometer formatted field
    char accelField[64];
    snprintf(accelField, sizeof(accelField), "%.2f/%.2f/%.2f/%.2f/%.2f", ax, ay, az, pitch, roll);

    // Optional data
    char optionalField[80];
    snprintf(optionalField, sizeof(optionalField), "UV=%.2f/PM2.5=%.1f/CO2=%.0f/AQI=%.2f", uvIndexLocal, pm25, co2, aqi);

    // Compose final CSV line
    snprintf(buf, sizeof(buf), "%s,%u,%u,%.1f,%u,%.1f,%.2f,%u,%.4f,%.4f,%.1f,%u,%s,%.1f,%u,%s\r\n",
             TEAM_ID,
             (unsigned)timestampSec,
             (unsigned)pktNo,
             effectiveAltitude,
             (unsigned)pressure,
             temp,
             batteryVoltage,
             (unsigned)gnss_time,
             gnss_lat,
             gnss_lon,
             gnss_alt,
             (unsigned)gnss_sats,
             accelField,
             gyro_spin_rate_degs,
             (unsigned)currentState,
             optionalField);

    return String(buf);
}

// One-line comment: write persistent values to NVS immediately.
void persistNVSImmediate() {
    nvspref.putUInt("pkt_count", packetCount);
    nvspref.putUInt("mission_t", missionTimeSec);
    nvspref.putUInt("flt_state", (uint32_t)currentState);
    nvspref.putFloat("alt_offset", altitudeGroundOffset);
}

// One-line comment: evaluate and perform state transitions based on sensor inputs.
void evaluateStateMachine(float effectiveAltitude) {
    prevState = currentState;

    float accelZ = 0.0f;
    if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, (TickType_t)10);
    accelZ = g_sensors.accel_z;
    if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

    static bool seenHighAccel = false;
    static int launchDetectCount = 0;
    static int impactAltCount = 0;

    switch (currentState) {
        case FlightState::BOOT:
            break;
        case FlightState::TEST_MODE:
            break;
        case FlightState::LAUNCH_PAD:
            if (accelZ > ACCEL_LAUNCH_THRESH) {
                launchDetectCount++;
                if (launchDetectCount >= 3) {
                    currentState = FlightState::ASCENT;
                    launchDetectCount = 0;
                }
            } else launchDetectCount = 0;
            break;
        case FlightState::ASCENT:
            if (accelZ > ACCEL_LAUNCH_THRESH) seenHighAccel = true;
            if (seenHighAccel && accelZ < ACCEL_EJECTION_THRESH) {
                currentState = FlightState::ROCKET_DEPLOY;
                seenHighAccel = false;
            }
            break;
        case FlightState::ROCKET_DEPLOY:
            if (effectiveAltitude < 980.0f && effectiveAltitude < altitudeGroundOffset + 20000.0f) {
            }
            if (effectiveAltitude < ALTITUDE_MAIN_DEPLOY * 2 && currentState == FlightState::ROCKET_DEPLOY) {
                currentState = FlightState::DESCENT;
            }
            break;
        case FlightState::DESCENT:
            static float altBuf[ALT_MOVING_AVG_N] = {0};
            static uint8_t altIdx = 0; static uint8_t altCount = 0;
            altBuf[altIdx++] = effectiveAltitude;
            if (altIdx >= ALT_MOVING_AVG_N) altIdx = 0;
            if (altCount < ALT_MOVING_AVG_N) altCount++;
            float avg = 0; for (uint8_t i=0;i<altCount;i++) avg += altBuf[i];
            avg = (altCount>0) ? (avg/altCount) : effectiveAltitude;
            if (avg <= ALTITUDE_MAIN_DEPLOY && avg <= 600.0f) {
                currentState = FlightState::AEROBREAK_RELEASE;
            }
            break;
        case FlightState::AEROBREAK_RELEASE:
            if (effectiveAltitude < ALTITUDE_IMPACT) {
                impactAltCount++;
                if (impactAltCount >= 5) currentState = FlightState::IMPACT;
            }
            break;
        case FlightState::IMPACT:
            break;
        default:
            break;
    }

    if (currentState != prevState) {
        persistNVSImmediate();
        if (DEBUG_VERBOSE) Serial.printf("[STATE] Transition %u -> %u\n", (unsigned)prevState, (unsigned)currentState);
        if (currentState == FlightState::ROCKET_DEPLOY) {
        }
        if (currentState == FlightState::AEROBREAK_RELEASE) {
            servoRelease();
        }
        if (currentState == FlightState::IMPACT) {
            ledcWrite(0, 0);
            digitalWrite(PIN_BEACON, HIGH);
        }
    }
}

// One-line comment: parse incoming RF commands from Serial lines.
void parseRFCommand(const String &cmd) {
    if (cmd == "ARM") {
        txEnabled = true;
        if (DEBUG_VERBOSE) Serial.println("[RF] ARM received");
    } else if (cmd == "STOP_TX") {
        txEnabled = false;
    } else if (cmd == "CAL") {
        if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
        altitudeGroundOffset = g_sensors.altitude_m;
        if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);
        TelemetryMsg msg; snprintf(msg.buf, sizeof(msg.buf), "[CAL] Ground reference zeroed\r\n");
        xQueueSend(sdQueue, &msg, 0);
    } else if (cmd == "SIM_ENABLE") {
        if (currentState == FlightState::BOOT || currentState == FlightState::LAUNCH_PAD) {
            currentState = FlightState::TEST_MODE;
        }
    } else if (cmd == "SIM_ACTIVATE") {
        if (currentState == FlightState::TEST_MODE) {
            simModeActive = true;
            g_sim_altitude = 0.0f;
        }
    } else if (cmd == "RESET_SIM") {
        simModeActive = false;
    }
}

// One-line comment: task that reads local ADCs and signals sensor refresh.
void sensorTask(void* pv) {
    Serial.println("[TASK] sensorTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100);
    while (true) {
        int rawUV = analogRead(PIN_UV);
        float vout = (rawUV / 4095.0f) * 3.3f;
        uvIndexLocal = vout / 0.1f;
        if (uvIndexLocal > 15.0f) uvIndexLocal = 15.0f;

        int rawV = analogRead(PIN_VBAT);
        float v = (rawV / 4095.0f) * 3.3f * VBAT_DIVIDER_RATIO;
        vbatRolling[vbatIdx++ & 3] = v;
        float vavg = 0; for (int i=0;i<4;i++) vavg += vbatRolling[i];
        batteryVoltage = vavg / 4.0f;

        xEventGroupSetBits(sensorEventGroup, SENSOR_REFRESH_BIT);

        vTaskDelayUntil(&lastWake, period);
    }
}

// One-line comment: primary control loop handling state machine and motor/LED patterns.
void controlTask(void* pv) {
    Serial.println("[TASK] controlTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(50);

    int ledCounter = 0;
    int gyroPwm = 0;
    while (true) {
        float effAlt = readEffectiveAltitude();
        evaluateStateMachine(effAlt);

        if (currentState == FlightState::ROCKET_DEPLOY) {
            if (gyroPwm < GYRO_TARGET_PWM) gyroPwm = min(gyroPwm + 5, GYRO_TARGET_PWM);
        } else if (currentState == FlightState::DESCENT || currentState == FlightState::AEROBREAK_RELEASE) {
            gyroPwm = GYRO_TARGET_PWM;
        } else if (currentState == FlightState::IMPACT) {
            gyroPwm = 0;
        }
        ledcWrite(0, gyroPwm);

        bool sdFast = (sdWriteFails > 3);
        if (sdFast) {
            digitalWrite(PIN_STATUS_LED, (ledCounter % 5) < 2);
        } else {
            switch (currentState) {
                case FlightState::BOOT: digitalWrite(PIN_STATUS_LED, (ledCounter % 40) < 20); break;
                case FlightState::LAUNCH_PAD: digitalWrite(PIN_STATUS_LED, HIGH); break;
                case FlightState::ASCENT: digitalWrite(PIN_STATUS_LED, (ledCounter % 10) < 5); break;
                case FlightState::ROCKET_DEPLOY: digitalWrite(PIN_STATUS_LED, (ledCounter%20<2) || (ledCounter%20>=4 && ledCounter%20<6)); break;
                case FlightState::DESCENT: digitalWrite(PIN_STATUS_LED, (ledCounter % 20) < 10); break;
                case FlightState::AEROBREAK_RELEASE: digitalWrite(PIN_STATUS_LED, (ledCounter%20<3)); break;
                case FlightState::IMPACT: digitalWrite(PIN_STATUS_LED, HIGH); break;
                default: digitalWrite(PIN_STATUS_LED, LOW); break;
            }
        }

        ledCounter++;
        vTaskDelayUntil(&lastWake, period);
    }
}

// One-line comment: telemetryTask runs at 1Hz, sends CSV, queues SD writes, parses RF commands.
void telemetryTask(void* pv) {
    Serial.println("[TASK] telemetryTask started");
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000);

    char cmdBuf[64]; size_t cmdIdx = 0;
    while (true) {
        vTaskDelayUntil(&lastWake, period);

        missionTimeSec = millis() / 1000;

        float effAlt = readEffectiveAltitude();

        if (g_sensors.mutex) xSemaphoreTake(g_sensors.mutex, portMAX_DELAY);
        if (g_sensors.mutex) xSemaphoreGive(g_sensors.mutex);

        packetCount++;

        String csv = formatTelemetryCSV(missionTimeSec, packetCount, effAlt);
        if (txEnabled) {
            Serial.print(csv);
        }

        TelemetryMsg msg; strncpy(msg.buf, csv.c_str(), sizeof(msg.buf)-1); msg.buf[sizeof(msg.buf)-1]='\0';
        xQueueSend(sdQueue, &msg, 0);

        if ((packetCount % NVS_PERSIST_INTERVAL) == 0) {
            nvspref.putUInt("pkt_count", packetCount);
            nvspref.putUInt("mission_t", missionTimeSec);
            nvspref.putUInt("flt_state", (uint32_t)currentState);
        }

        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (cmdIdx > 0) {
                    cmdBuf[cmdIdx] = '\0';
                    parseRFCommand(String(cmdBuf));
                    cmdIdx = 0;
                }
            } else {
                if (cmdIdx < sizeof(cmdBuf)-1) cmdBuf[cmdIdx++] = c;
            }
        }
    }
}

// One-line comment: sdTask receives CSV strings and writes to SD card safely.
void sdTask(void* pv) {
    Serial.println("[TASK] sdTask started");
    TelemetryMsg msg;
    while (true) {
        if (xQueueReceive(sdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!sdReady) {
                sdWriteFails++;
                continue;
            }
            bool needHeader = !SD.exists(SD_FILENAME);
            File f = SD.open(SD_FILENAME, FILE_APPEND);
            if (!f) {
                sdWriteFails++;
                continue;
            }
            if (needHeader) {
                f.print(CSV_HEADER);
            }
            if (!f.print(msg.buf)) {
                sdWriteFails++;
            } else {
                sdWriteFails = 0;
            }
            f.close();
        }
    }
}

// Setup sequence per spec
void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);

    pinMode(PIN_SERVO, OUTPUT);
    pinMode(PIN_GYRO_PWM, OUTPUT);
    pinMode(PIN_BEACON, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_HALL, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_HALL), hallISR, FALLING);

    ledcSetup(0, 25000, 8);
    ledcAttachPin(PIN_GYRO_PWM, 0);

    mainServo.attach(PIN_SERVO);
    mainServo.write(SERVO_LOCKED_DEG);
    mainServo.detach();

    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdReady = SD.begin(PIN_SD_CS);

    // Preferences restore
    nvspref.begin("cansat_nv", false);
    packetCount = nvspref.getUInt("pkt_count", 0);
    missionTimeSec = nvspref.getUInt("mission_t", 0);
    uint32_t persistedState = nvspref.getUInt("flt_state", 0);
    altitudeGroundOffset = nvspref.getFloat("alt_offset", 0.0f);
    currentState = static_cast<FlightState>(persistedState);
    if ((uint8_t)currentState != 0) {
        Serial.printf("[NVS] Resuming from state %u, packet %u, time %us\n", (unsigned)currentState, packetCount, missionTimeSec);
    }

    // Create queue and event group
    sdQueue = xQueueCreate(10, sizeof(TelemetryMsg));
    sensorEventGroup = xEventGroupCreate();

    // Create timers
    servoRetractTimer = xTimerCreate("servoRetract", pdMS_TO_TICKS(1000), pdFALSE, NULL, servoRetractTimerCb);
    gyroTimer = xTimerCreate("gyroTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, gyroTimerCb);
    xTimerStart(gyroTimer, 0);

    // Create tasks (stack sizes as bytes)
    xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(controlTask, "controlTask", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(telemetryTask, "telemetryTask", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(sdTask, "sdTask", 4096, NULL, 1, NULL, 0);

    // Final boot message
    currentState = FlightState::BOOT;
    Serial.println("[BOOT] CAN-7USAT v3.0 — BU Astronomy Club — All systems init");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
