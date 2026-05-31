"""Convert raw telemetry fields into the canonical TelemetryPacket schema."""

from __future__ import annotations

import logging
from typing import Any, Dict, List, Optional

from config import SEA_LEVEL_HPA
from gps_utils import compute_descent_rate, fix_type_str, haversine

LOGGER = logging.getLogger(__name__)


class PacketBuilder:
    """Assemble the full telemetry packet and track mission-level derived state."""

    def __init__(self) -> None:
        self.apogee = 0.0
        self.prev_alt: Optional[float] = None
        self.last_time: Optional[float] = None
        self.mission_start_ms: Optional[int] = None
        self.launch_lat: Optional[float] = None
        self.launch_lon: Optional[float] = None
        self.packet_count = 0
        self.expected_next_id: Optional[int] = None
        self.lost_packets = 0

    # Unit conversion cheatsheet:
    # pressure:     raw Pa  ÷ 100       = hPa  (store expects hPa)
    # battery:      raw mV  ÷ 1000      = V    (store expects V)
    # fix_type:     int 0/1/2           → "NO_FIX" / "2D" / "3D"
    # autogyro:     int 0/1/2/3         → "IDLE"/"SPINNING_UP"/"ACTIVE"/"FAULT"
    # mission:      int 0–4             → phase string
    # booleans:     int 0/1             → Python bool (JSON serialises as true/false)
    # gyro drift:   raw gyro °/s × dt   = incremental angle offset per frame

    def _float(self, raw_fields: List[str], index: int, default: float = 0.0) -> float:
        try:
            return float(raw_fields[index])
        except (IndexError, ValueError, TypeError):
            return default

    def _int(self, raw_fields: List[str], index: int, default: int = 0) -> int:
        try:
            return int(float(raw_fields[index]))
        except (IndexError, ValueError, TypeError):
            return default

    def build(self, raw_fields: List[str], compensated: Dict[str, float], drift: Dict[str, float]) -> Dict[str, Any]:
        try:
            pkt_id = self._int(raw_fields, 0)
            timestamp = self._int(raw_fields, 1)
            pitch_raw = self._float(raw_fields, 2)
            roll_raw = self._float(raw_fields, 3)
            heading_raw = self._float(raw_fields, 4)
            gyro_x = self._float(raw_fields, 5)
            gyro_y = self._float(raw_fields, 6)
            gyro_z = self._float(raw_fields, 7)
            accel_x = self._float(raw_fields, 8)
            accel_y = self._float(raw_fields, 9)
            accel_z = self._float(raw_fields, 10)
            mag_x = self._float(raw_fields, 11)
            mag_y = self._float(raw_fields, 12)
            mag_z = self._float(raw_fields, 13)
            alt_baro = self._float(raw_fields, 14)
            alt_gps = self._float(raw_fields, 15)
            pressure_hpa = self._float(raw_fields, 16) / 100.0
            temp_ext = self._float(raw_fields, 17)
            temp_int = self._float(raw_fields, 18)
            humidity = self._float(raw_fields, 19)
            lat = self._float(raw_fields, 20)
            lon = self._float(raw_fields, 21)
            speed_kmh = self._float(raw_fields, 22)
            course_deg = self._float(raw_fields, 23)
            satellites = self._int(raw_fields, 24)
            hdop = self._float(raw_fields, 25)
            fix_type_code = self._int(raw_fields, 26)
            rotor_rpm = self._float(raw_fields, 27)
            servo_a = self._float(raw_fields, 28)
            servo_b = self._float(raw_fields, 29)
            stability_idx = self._float(raw_fields, 30)
            autogyro_state_code = self._int(raw_fields, 31)
            bat_v = self._float(raw_fields, 32) / 1000.0
            bat_pct = self._float(raw_fields, 33)
            current_ma = self._float(raw_fields, 34)
            rssi = self._int(raw_fields, 35)
            snr = self._float(raw_fields, 36)
            mission_phase_code = self._int(raw_fields, 37)
            chute_deployed = bool(self._int(raw_fields, 38))
            autogyro_armed = bool(self._int(raw_fields, 39))
            buzzer_active = bool(self._int(raw_fields, 40))
            uv_index = self._float(raw_fields, 41)
        except Exception as error:  # pragma: no cover - packet-level safety net
            LOGGER.exception("Failed to parse raw packet: %s", error)
            raise

        if self.expected_next_id is not None and pkt_id != self.expected_next_id:
            gap = pkt_id - self.expected_next_id
            if gap > 0:
                self.lost_packets += gap
                LOGGER.warning("Packet gap detected: expected %s got %s (lost %s)", self.expected_next_id, pkt_id, gap)
        self.expected_next_id = pkt_id + 1

        if mission_phase_code >= 1 and self.mission_start_ms is None:
            self.mission_start_ms = timestamp

        mission_elapsed_ms = timestamp - self.mission_start_ms if self.mission_start_ms is not None else 0

        if self.launch_lat is None and fix_type_code == 2:
            self.launch_lat = lat
            self.launch_lon = lon

        distance_from_pad = (
            haversine(self.launch_lat, self.launch_lon, lat, lon)
            if self.launch_lat is not None and self.launch_lon is not None
            else 0.0
        )

        apogee = max(self.apogee, alt_baro)
        self.apogee = apogee

        dt = 0.0 if self.last_time is None else max(0.0, (timestamp - self.last_time) / 1000.0)
        descent_rate = compute_descent_rate(alt_baro, self.prev_alt, dt)
        self.prev_alt = alt_baro
        self.last_time = timestamp

        if self.packet_count == 0:
            self.packet_count = pkt_id
        self.packet_count += 1

        packet_loss_percent = (self.lost_packets / (pkt_id + 1)) * 100.0 if pkt_id >= 0 else 0.0

        autogyro_state_map = {0: "IDLE", 1: "SPINNING_UP", 2: "ACTIVE", 3: "FAULT"}
        mission_phase_map = {0: "PRE_LAUNCH", 1: "ASCENT", 2: "APOGEE", 3: "DESCENT", 4: "LANDED"}

        packet = {
            "packetId": pkt_id,
            "timestamp": timestamp,
            "missionElapsedMs": mission_elapsed_ms,
            "imu": {
                "raw": {
                    "pitch": pitch_raw,
                    "roll": roll_raw,
                    "heading": heading_raw,
                    "gyroX": gyro_x,
                    "gyroY": gyro_y,
                    "gyroZ": gyro_z,
                    "accelX": accel_x,
                    "accelY": accel_y,
                    "accelZ": accel_z,
                    "magX": mag_x,
                    "magY": mag_y,
                    "magZ": mag_z,
                },
                "compensated": {
                    "pitch": compensated.get("pitch", 0.0),
                    "roll": compensated.get("roll", 0.0),
                    "heading": compensated.get("heading", 0.0),
                },
                "drift": {
                    "rateX": drift.get("rateX", 0.0),
                    "rateY": drift.get("rateY", 0.0),
                    "rateZ": drift.get("rateZ", 0.0),
                    "accumulatedX": drift.get("accumulatedX", 0.0),
                    "accumulatedY": drift.get("accumulatedY", 0.0),
                    "accumulatedZ": drift.get("accumulatedZ", 0.0),
                    "filterAlpha": drift.get("filterAlpha", 0.0),
                    "lastCalibrationMs": drift.get("lastCalibrationMs", 0.0),
                },
            },
            "altitude": {
                "barometric": alt_baro,
                "gps": alt_gps,
                "seaLevel": SEA_LEVEL_HPA,
                "apogee": apogee,
                "descentRate": descent_rate,
            },
            "gps": {
                "latitude": lat,
                "longitude": lon,
                "altitudeGPS": alt_gps,
                "speedKmh": speed_kmh,
                "courseDeg": course_deg,
                "satellites": satellites,
                "hdop": hdop,
                "fixType": fix_type_str(fix_type_code),
                "distanceFromPad": distance_from_pad,
            },
            "env": {
                "temperatureExternal": temp_ext,
                "temperatureInternal": temp_int,
                "pressure": pressure_hpa,
                "humidity": humidity,
                "uvIndex": uv_index,
            },
            "autogyro": {
                "rotorRpm": rotor_rpm,
                "servoAlpha": servo_a,
                "servoBeta": servo_b,
                "ikInputPitch": compensated.get("pitch", 0.0),
                "ikInputRoll": compensated.get("roll", 0.0),
                "stabilityIndex": stability_idx,
                "state": autogyro_state_map.get(autogyro_state_code, "FAULT"),
                "correctionXDeg": servo_a,
                "correctionYDeg": servo_b,
            },
            "power": {
                "batteryVoltage": bat_v,
                "batteryPercent": bat_pct,
                "current": current_ma,
            },
            "link": {
                "rssi": rssi,
                "snr": snr,
                "packetLossPercent": packet_loss_percent,
                "port": "UNKNOWN",
                "baudRate": 115200,
            },
            "mission": {
                "phase": mission_phase_map.get(mission_phase_code, "UNKNOWN"),
                "parachuteDeployed": chute_deployed,
                "autogyroArmed": autogyro_armed,
                "buzzerActive": buzzer_active,
            },
        }

        return packet
