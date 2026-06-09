// =============================================================================
//  CAN-7USAT  |  HEADER : SECTION 2 – MOTION & POSITION SENSORS
//  Bennett University – Astronomy Club / Space Tech Division
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <HardwareSerial.h>

// ---------------------------------------------------------------------------
//  Motion & Position data struct
// ---------------------------------------------------------------------------
struct MotionData_t {
    // ── BNO055 Euler Orientation ──────────────────────────────────────────
    float    euler_heading_deg;   // Compass heading 0-360°
    float    euler_roll_deg;      // Roll  (Yc axis)
    float    euler_pitch_deg;     // Pitch (Zc axis)

    // ── BNO055 Linear Acceleration ───────────────────────────────────────
    float    accel_x_ms2;         // X-axis linear accel (m/s²)
    float    accel_y_ms2;         // Y-axis linear accel (m/s²)
    float    accel_z_ms2;         // Z-axis linear accel (m/s²)
    float    accel_total_g;       // Total acceleration magnitude (g)

    // ── BNO055 Gyroscope (body-rate, not wheel) ───────────────────────────
    float    gyro_x_dps;          // deg/s X
    float    gyro_y_dps;          // deg/s Y
    float    gyro_z_dps;          // deg/s Z

    // ── BNO055 Calibration Status (0=uncal, 3=fully cal) ─────────────────
    uint8_t  bno_cal_sys;
    uint8_t  bno_cal_gyro;

    // ── A3144 Hall Sensor – Momentum Wheel ───────────────────────────────
    float    gyroSpinRate_dps;    // Wheel spin rate in deg/s (GNSS field 14)

    // ── GNSS (NavIC primary / NEO-M9N fallback) ───────────────────────────
    double   latitude_deg;        // Degrees (0.0001° resolution)
    double   longitude_deg;       // Degrees (0.0001° resolution)
    float    gnssAlt_m;           // GNSS altitude (m)
    uint32_t gnssTime_s;          // HHMMSSCC from NMEA
    uint8_t  gnss_sats;           // Number of satellites tracked
    bool     gnssFixValid;        // True = valid 3D fix
    bool     usingNavIC;          // True = NavIC active, False = NEO-M9N fallback
};

// ---------------------------------------------------------------------------
//  Exposed UART objects (used by Section 3 XBee on UART0 – declared in .cpp)
// ---------------------------------------------------------------------------
extern HardwareSerial SerialNavIC;
extern HardwareSerial SerialNEOM9N;

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
bool                  initMotion(void);
void                  updateMotion(void);
const MotionData_t&   getMotionData(void);
void                  spinUpMomentumWheel(void);
void                  stopMomentumWheel(void);
void                  setMotorDuty(uint16_t duty);
void                  calibrateIMU(void);
void                  printMotionData(void);
