// =============================================================================
//  CAN-7USAT  |  HEADER : SECTION 1 – ENVIRONMENTAL SENSORS
//  Bennett University – Astronomy Club / Space Tech Division
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
//  Packed data struct – all environmental readings in one place
// ---------------------------------------------------------------------------
struct EnvData_t {
    // ── BMP390 ───────────────────────────────────────────────────────────────
    float    altitude_m;      // Relative altitude above ground (m)  [MANDATORY]
    float    pressure_Pa;     // Atmospheric pressure (Pa)            [MANDATORY]
    float    temperature_C;   // Temperature from BMP390 (°C)         [MANDATORY]

    // ── SPS30 ────────────────────────────────────────────────────────────────
    float    pm1p0;           // PM1.0  mass concentration (µg/m³)
    float    pm2p5;           // PM2.5  mass concentration (µg/m³)
    float    pm4p0;           // PM4.0  mass concentration (µg/m³)
    float    pm10p0;          // PM10.0 mass concentration (µg/m³)

    // ── SCD41 ────────────────────────────────────────────────────────────────
    uint16_t co2_ppm;         // CO2 concentration (ppm)
    float    humidity_pct;    // Relative humidity (%)
    float    temp_scd_C;      // Temperature from SCD41 (°C)

    // ── MQ-135 ───────────────────────────────────────────────────────────────
    float    mq135_ratio;     // Rs/R0 ratio (use gas curves for ppm)
    int      mq135_rawADC;    // Raw 12-bit ADC value

    // ── GUVA-S12SD ───────────────────────────────────────────────────────────
    float    uv_index;        // UV index (0 – 16)
    int      uv_rawADC;       // Raw 12-bit ADC value
};

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
bool              initEnvironmental(void);
void              updateEnvironmental(void);
const EnvData_t&  getEnvData(void);
void              calibrateBaroGround(float& groundAlt_m);
void              printEnvData(void);
