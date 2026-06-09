// =============================================================================
//  CAN-7USAT  |  HEADER : SECTION 3 – COMMS, CONTROL & STORAGE
//  Bennett University – Astronomy Club / Space Tech Division
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
//  Ground-station command codes
// ---------------------------------------------------------------------------
enum GSCommand_t {
    GS_NONE         = 0,
    GS_ARM          = 1,   // ARM → enter LAUNCH_PAD state
    GS_START_TX     = 2,   // Enable 1 Hz telemetry downlink
    GS_STOP_TX      = 3,   // Pause telemetry
    GS_CAL          = 4,   // Zero baro/gyro/accel offsets
    GS_SIM_ENABLE   = 5,   // Enter TEST_MODE simulation
    GS_SIM_ACTIVATE = 6,   // Begin sim altitude profile injection
};

// ---------------------------------------------------------------------------
//  Comms & storage state snapshot
// ---------------------------------------------------------------------------
struct CommsData_t {
    float    battery_V;     // Actual battery voltage (V)
    bool     txEnabled;     // True = XBee TX active
    bool     sdReady;       // True = SD card mounted and CSV open
    bool     cameraActive;  // True = OV2640 capturing
    bool     servoReleased; // True = main chute pin extended
};

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
bool             initComms(const char* teamID);
void             updateBatteryVoltage(void);
void             buildTelemetryPacket(
                    const char* teamID, uint32_t time_s, uint32_t packetCount,
                    float altitude_m, float pressure_Pa, float temp_C, float voltage_V,
                    uint32_t gnssTime, double lat, double lon, float gnssAlt_m, uint8_t sats,
                    float accel_x, float accel_y, float accel_z,
                    float gyroSpinRate_dps, uint8_t flightState, float uvIndex,
                    char* outBuf, size_t bufLen);
void             transmitPacket(const char* packet);
GSCommand_t      processGSCommand(const char* teamID);
void             releaseMainParachute(void);
void             lockServo(void);
void             saveStateToNVS(uint32_t time_s, uint8_t flightState);
void             loadStateFromNVS(uint32_t& time_s, uint8_t& flightState);
void             captureFrame(uint32_t timestamp);
const CommsData_t& getCommsData(void);
void             setTxEnabled(bool en);
float            getBatteryVoltage(void);
void             printCommsData(void);
