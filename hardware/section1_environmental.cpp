// =============================================================================
//  CAN-7USAT  |  SECTION 1 : ENVIRONMENTAL SENSORS
//  Bennett University – Astronomy Club / Space Tech Division
//  Firmware Version : V3.0  (Competition-Compliant, IN-SPACe 2026)
// =============================================================================
//
//  Sensors covered in this file
//  ┌────┬────────────┬────────────────────────────────────────────┬──────────┐
//  │ #  │  Sensor    │  Function                                  │Interface │
//  ├────┼────────────┼────────────────────────────────────────────┼──────────┤
//  │  1 │ BMP390     │ Altitude, Pressure, Temperature             │   I2C    │
//  │  2 │ SPS30      │ PM1.0 / PM2.5 / PM4.0 / PM10              │ I2C/UART │
//  │  3 │ SCD41      │ CO2, Temperature, Humidity                 │   I2C    │
//  │  4 │ MQ-135     │ Air Quality (NH3, NOx, Benzene, Smoke, CO2)│  Analog  │
//  │  5 │ GUVA-S12SD │ UV Index vs. Altitude                      │  Analog  │
//  └────┴────────────┴────────────────────────────────────────────┴──────────┘
//
//  I2C Bus : SDA = GPIO 21 | SCL = GPIO 22  (shared with Section 2 sensors)
//  ADC     : MQ-135 = GPIO 32 | GUVA = GPIO 34
//
//  Libraries required (install via Arduino Library Manager / PlatformIO):
//    • Adafruit BMP3XX          (Adafruit)
//    • Sensirion I2C SPS30      (Sensirion)
//    • Sensirion I2C SCD4x      (Sensirion)
//    • Wire                     (built-in ESP32)
// =============================================================================

#include "section1_environmental.h"

#include <Arduino.h>
#include <Wire.h>

// ▼▼▼  SENSOR #1  –  BMP390  (Altitude / Pressure / Temperature)  ▼▼▼
#include <Adafruit_BMP3XX.h>

// ▼▼▼  SENSOR #2  –  SPS30  (Particulate Matter PM1.0 / PM2.5 / PM4.0 / PM10)  ▼▼▼
#include <SensirionI2cSps30.h>

// ▼▼▼  SENSOR #3  –  SCD41  (CO2 / Temperature / Humidity)  ▼▼▼
#include <SensirionI2cScd4x.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
// SENSOR #4  –  MQ-135  (Air Quality – NH3 / NOx / Benzene / CO2)
#define PIN_MQ135   32   // ADC1_CH4  – GPIO32

// SENSOR #5  –  GUVA-S12SD  (UV Index vs. Altitude)
#define PIN_UV      34   // ADC1_CH6  – GPIO34  (input-only, no pull-up)

// ============================================================
//  I2C ADDRESSES
// ============================================================
// SENSOR #1  –  BMP390
#define BMP390_I2C_ADDR  0x77   // SDO HIGH = 0x77 | SDO LOW = 0x76
// SENSOR #2  –  SPS30  →  fixed address 0x69 (handled inside Sensirion lib)
// SENSOR #3  –  SCD41  →  fixed address 0x62 (handled inside Sensirion lib)

// ============================================================
//  ADC SHARED CONSTANTS  (used by Sensor #4 and #5)
// ============================================================
// ESP32 ADC full-scale at 11 dB attenuation (~3.3 V).
// Use esp_adc_cal_characterize() for production accuracy.
static constexpr float ADC_VREF_MV   = 3300.0f;
static constexpr float ADC_MAX_COUNT = 4095.0f;

// ============================================================
//  SENSOR #4  –  MQ-135  CALIBRATION CONSTANTS
//  (Air Quality: NH3, NOx, Benzene, Smoke, CO2)
// ============================================================
// Load resistance on breakout board (typically 10 kΩ)
static constexpr float MQ135_RL_OHMS = 10000.0f;
// R0 = sensor resistance in clean air (calibrate before flight)
// 76.63 Ω is the typical value; replace with your measured R0
static constexpr float MQ135_R0_OHMS = 76.63f;

// ============================================================
//  SENSOR #5  –  GUVA-S12SD  CALIBRATION CONSTANTS
//  (UV Index vs. Altitude)
// ============================================================
// Output: ~100 mV per UV index unit (0 mV = UV 0, 1000 mV = UV 10)
static constexpr float UV_SENSITIVITY_MV = 100.0f;

// ============================================================
//  SENSOR DRIVER OBJECTS
// ============================================================
static Adafruit_BMP3XX   bmp;    // SENSOR #1 – BMP390   (I2C)
static SensirionI2cSps30 sps30;  // SENSOR #2 – SPS30    (I2C)
static SensirionI2cScd4x scd41;  // SENSOR #3 – SCD41    (I2C)
// SENSOR #4 – MQ-135     → no object; raw analogRead(PIN_MQ135)
// SENSOR #5 – GUVA-S12SD → no object; raw analogRead(PIN_UV)

// ============================================================
//  MODULE-LEVEL DATA CACHE  (filled by updateEnvironmental())
// ============================================================
static EnvData_t envData;
static bool      bmpOK   = false;  // SENSOR #1 online flag
static bool      sps30OK = false;  // SENSOR #2 online flag
static bool      scd41OK = false;  // SENSOR #3 online flag
// SENSOR #4 & #5 are always considered online (analog, no init needed)

// ===========================================================================
//  initEnvironmental()
//  Call once from setup().
//  Returns true if the MANDATORY sensor (BMP390) initialises successfully.
//  SPS30, SCD41, MQ-135 and UV sensor failures are logged but do NOT abort.
// ===========================================================================
bool initEnvironmental(void)
{
    // Initialise I2C bus once (shared with Section 2 — safe to call again)
    Wire.begin(21, 22);
    Wire.setClock(400000);  // 400 kHz Fast-mode

    bool allOK = true;

    // ╔══════════════════════════════════════════════════════════╗
    // ║  SENSOR #1  –  BMP390                                   ║
    // ║  Measures : Altitude (m) | Pressure (Pa) | Temp (°C)   ║
    // ║  Interface: I2C @ 0x77  |  MANDATORY for flight        ║
    // ╚══════════════════════════════════════════════════════════╝
    if (!bmp.begin_I2C(BMP390_I2C_ADDR, &Wire)) {
        Serial.println("[ENV][S1-BMP390] NOT FOUND – check wiring / SDO pin!");
        bmpOK = false;
        allOK = false;
    } else {
        // Oversampling & IIR filter tuned for barometric altimetry
        bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
        bmp.setPressureOversampling(BMP3_OVERSAMPLING_32X);
        bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
        bmp.setOutputDataRate(BMP3_ODR_25_HZ);
        bmpOK = true;
        Serial.println("[ENV][S1-BMP390] OK – Alt/Pressure/Temp active");
    }

    // ╔══════════════════════════════════════════════════════════╗
    // ║  SENSOR #2  –  SPS30                                    ║
    // ║  Measures : PM1.0 / PM2.5 / PM4.0 / PM10 (µg/m³)      ║
    // ║  Interface: I2C @ 0x69  |  Optional (science payload)  ║
    // ╚══════════════════════════════════════════════════════════╝
    sps30.begin(Wire);
    uint16_t spsErr = sps30.startMeasurement();
    if (spsErr != 0) {
        Serial.printf("[ENV][S2-SPS30] startMeasurement failed (err=0x%04X) – optional\n",
                      spsErr);
        sps30OK = false;
    } else {
        sps30OK = true;
        Serial.println("[ENV][S2-SPS30] OK – PM1.0/PM2.5/PM4.0/PM10 active");
    }

    // ╔══════════════════════════════════════════════════════════╗
    // ║  SENSOR #3  –  SCD41                                    ║
    // ║  Measures : CO2 (ppm) | Temperature (°C) | Humidity (%)║
    // ║  Interface: I2C @ 0x62  |  Optional (science payload)  ║
    // ╚══════════════════════════════════════════════════════════╝
    scd41.begin(Wire);
    // Must stop any running measurement before reconfiguring (SCD4x datasheet)
    uint16_t scdErr = scd41.stopPeriodicMeasurement();
    delay(500);   // SCD41 requires 500 ms idle after stop
    scdErr = scd41.startPeriodicMeasurement();   // ~1 sample per 5 s
    if (scdErr != 0) {
        Serial.printf("[ENV][S3-SCD41] startPeriodicMeasurement failed (err=0x%04X) – optional\n",
                      scdErr);
        scd41OK = false;
    } else {
        scd41OK = true;
        Serial.println("[ENV][S3-SCD41] OK – CO2/Temp/Humidity active");
    }

    // ╔══════════════════════════════════════════════════════════╗
    // ║  SENSOR #4  –  MQ-135                                   ║
    // ║  Measures : Air Quality (NH3, NOx, Benzene, Smoke, CO2) ║
    // ║  Interface: Analog ADC  GPIO32  |  Always active        ║
    // ╚══════════════════════════════════════════════════════════╝
    analogSetPinAttenuation(PIN_MQ135, ADC_11db);  // 0–3.3 V full-scale
    Serial.println("[ENV][S4-MQ135] ANALOG active on GPIO32");

    // ╔══════════════════════════════════════════════════════════╗
    // ║  SENSOR #5  –  GUVA-S12SD                               ║
    // ║  Measures : UV Index vs. Altitude (0 – 10+)             ║
    // ║  Interface: Analog ADC  GPIO34  |  Always active        ║
    // ╚══════════════════════════════════════════════════════════╝
    analogSetPinAttenuation(PIN_UV, ADC_11db);     // 0–3.3 V full-scale
    Serial.println("[ENV][S5-GUVA] ANALOG active on GPIO34");

    // Zero the data cache
    memset(&envData, 0, sizeof(EnvData_t));

    return allOK;   // false only if BMP390 (mandatory) not found
}

// ===========================================================================
//  updateEnvironmental()
//  Call at ≥1 Hz from the main loop / RTOS task.
//  Populates the module-level EnvData_t cache.
// ===========================================================================
void updateEnvironmental(void)
{
    // ─────────────────────────────────────────────────────────
    //  SENSOR #1  –  BMP390  |  Altitude / Pressure / Temp
    // ─────────────────────────────────────────────────────────
    if (bmpOK) {
        if (bmp.performReading()) {
            envData.pressure_Pa   = bmp.pressure;              // Pascals
            envData.temperature_C = bmp.temperature;           // °C
            // ISA baseline; main.cpp subtracts groundAlt for relative altitude
            envData.altitude_m    = bmp.readAltitude(1013.25f);// metres
        } else {
            Serial.println("[ENV][S1-BMP390] read failed – holding last value");
        }
    }

    // ─────────────────────────────────────────────────────────
    //  SENSOR #2  –  SPS30  |  PM1.0 / PM2.5 / PM4.0 / PM10
    // ─────────────────────────────────────────────────────────
    if (sps30OK) {
        uint16_t dataReady = 0;
        uint16_t err = sps30.readDataReadyFlag(dataReady);
        if (err == 0 && dataReady) {
            float pm1p0, pm2p5, pm4p0, pm10p0,
                  nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typSize;
            err = sps30.readMeasuredValues(
                      pm1p0, pm2p5, pm4p0, pm10p0,
                      nc0p5, nc1p0, nc2p5, nc4p0, nc10p0,
                      typSize);
            if (err == 0) {
                envData.pm1p0  = pm1p0;   // µg/m³
                envData.pm2p5  = pm2p5;   // µg/m³
                envData.pm4p0  = pm4p0;   // µg/m³
                envData.pm10p0 = pm10p0;  // µg/m³
            } else {
                Serial.printf("[ENV][S2-SPS30] readMeasuredValues error 0x%04X\n", err);
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    //  SENSOR #3  –  SCD41  |  CO2 / Temperature / Humidity
    // ─────────────────────────────────────────────────────────
    if (scd41OK) {
        bool     dataReady = false;
        uint16_t err       = scd41.getDataReadyFlag(dataReady);
        if (err == 0 && dataReady) {
            uint16_t co2_ppm  = 0;
            float    temp_scd = 0.0f;
            float    hum_scd  = 0.0f;
            err = scd41.readMeasurement(co2_ppm, temp_scd, hum_scd);
            if (err == 0 && co2_ppm != 0) {   // co2==0 → sensor still compensating
                envData.co2_ppm      = co2_ppm;   // ppm
                envData.humidity_pct = hum_scd;   // %RH
                envData.temp_scd_C   = temp_scd;  // °C
            } else if (co2_ppm == 0) {
                Serial.println("[ENV][S3-SCD41] co2=0 returned – sensor warming up");
            } else {
                Serial.printf("[ENV][S3-SCD41] readMeasurement error 0x%04X\n", err);
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    //  SENSOR #4  –  MQ-135  |  Air Quality (Analog, GPIO32)
    //  Outputs Rs/R0 ratio; use gas-curve log equations for ppm
    // ─────────────────────────────────────────────────────────
    {
        int   raw    = analogRead(PIN_MQ135);
        float vOut_V = (raw / ADC_MAX_COUNT) * (ADC_VREF_MV / 1000.0f);  // V

        // Guard: avoid divide-by-zero when sensor is cold / disconnected
        if (vOut_V > 0.01f) {
            // Rs = RL × (Vc/Vout − 1)   where Vc = 3.3 V
            float Rs = MQ135_RL_OHMS * ((ADC_VREF_MV / 1000.0f / vOut_V) - 1.0f);
            envData.mq135_ratio = (Rs > 0.0f) ? (Rs / MQ135_R0_OHMS) : 0.0f;
        } else {
            envData.mq135_ratio = 0.0f;   // sensor not warm / not connected
        }
        envData.mq135_rawADC = raw;
    }

    // ─────────────────────────────────────────────────────────
    //  SENSOR #5  –  GUVA-S12SD  |  UV Index (Analog, GPIO34)
    //  Output: ~100 mV per UV index unit  (Range: UV 0 – 16)
    // ─────────────────────────────────────────────────────────
    {
        int   raw = analogRead(PIN_UV);
        float vMV = (raw / ADC_MAX_COUNT) * ADC_VREF_MV;    // millivolts
        envData.uv_index  = vMV / UV_SENSITIVITY_MV;         // UV index 0–16
        envData.uv_rawADC = raw;
    }
}

// ===========================================================================
//  getEnvData()
//  Returns a const reference to the latest populated EnvData_t snapshot.
// ===========================================================================
const EnvData_t& getEnvData(void)
{
    return envData;
}

// ===========================================================================
//  calibrateBaroGround()
//  Records the current BMP390 absolute altitude as the ground-level reference.
//  Call once on the launch pad after the GS CAL command (Rule 6.1-vi).
//  All subsequent altitude_m readings are relative to this baseline.
// ===========================================================================
void calibrateBaroGround(float& groundAlt_m)
{
    if (!bmpOK) {
        Serial.println("[ENV] calibrateBaroGround: BMP390 not available");
        return;
    }

    float   sum     = 0.0f;
    int     samples = 0;
    const   int N   = 20;

    for (int i = 0; i < N; i++) {
        if (bmp.performReading()) {
            sum += bmp.readAltitude(1013.25f);
            samples++;
        }
        delay(50);   // 50 ms × 20 = 1 s total averaging window
    }

    if (samples > 0) {
        groundAlt_m = sum / (float)samples;
        Serial.printf("[ENV] Ground altitude calibrated: %.2f m (ISA ref), n=%d\n",
                      groundAlt_m, samples);
    } else {
        Serial.println("[ENV] calibrateBaroGround: no valid readings obtained");
    }
}

// ===========================================================================
//  printEnvData()  —  Debug / bench helper; remove calls in production build
// ===========================================================================
void printEnvData(void)
{
    Serial.printf(
        "[ENV] Alt=%.2fm  P=%.1fPa  T_bmp=%.1fC  UV=%.2f  "
        "CO2=%uppm  Hum=%.1f%%  PM2.5=%.2fug/m3  MQ135_Rs/R0=%.3f\n",
        envData.altitude_m,
        envData.pressure_Pa,
        envData.temperature_C,
        envData.uv_index,
        (unsigned)envData.co2_ppm,
        envData.humidity_pct,
        envData.pm2p5,
        envData.mq135_ratio);
}
