"""Complementary-filter IMU processing and drift tracking."""

from __future__ import annotations

import logging
import math
import time
from typing import Dict, Tuple

from config import CALIB_INTERVAL_S, FILTER_ALPHA, MAG_DECLINATION_DEG

LOGGER = logging.getLogger(__name__)


class IMUProcessor:
    """Maintain complementary-filter state and drift estimates across packets."""

    def __init__(self, alpha: float = FILTER_ALPHA) -> None:
        self.comp_pitch = 0.0
        self.comp_roll = 0.0
        self.comp_heading = 0.0
        self.drift_rate_x = 0.0
        self.drift_rate_y = 0.0
        self.drift_rate_z = 0.0
        self.accum_x = 0.0
        self.accum_y = 0.0
        self.accum_z = 0.0
        self.last_packet_time: float | None = None
        self.last_calib_time: float | None = time.time()
        self.alpha = alpha
        self.prev_accel_pitch = 0.0
        self.prev_accel_roll = 0.0
        self.prev_heading = 0.0

    def update(self, raw_imu_dict: Dict[str, float]) -> Tuple[Dict[str, float], Dict[str, float]]:
        current_time = time.time()
        if self.last_packet_time is None:
            dt = 0.01
        else:
            dt = current_time - self.last_packet_time
        dt = max(0.001, min(0.1, dt))
        self.last_packet_time = current_time

        accel_x = float(raw_imu_dict.get("accelX", 0.0))
        accel_y = float(raw_imu_dict.get("accelY", 0.0))
        accel_z = float(raw_imu_dict.get("accelZ", 0.0))
        gyro_x = float(raw_imu_dict.get("gyroX", 0.0))
        gyro_y = float(raw_imu_dict.get("gyroY", 0.0))
        gyro_z = float(raw_imu_dict.get("gyroZ", 0.0))
        raw_heading = float(raw_imu_dict.get("heading", 0.0))

        accel_pitch = math.degrees(math.atan2(accel_y, math.sqrt(accel_x ** 2 + accel_z ** 2)))
        accel_roll = math.degrees(math.atan2(-accel_x, accel_z if accel_z != 0 else 1e-6))

        self.comp_pitch = self.alpha * (self.comp_pitch + gyro_x * dt) + (1.0 - self.alpha) * accel_pitch
        self.comp_roll = self.alpha * (self.comp_roll + gyro_y * dt) + (1.0 - self.alpha) * accel_roll
        self.comp_heading = (raw_heading + MAG_DECLINATION_DEG) % 360.0

        prev_pitch = self.prev_accel_pitch
        prev_roll = self.prev_accel_roll
        prev_heading = self.prev_heading

        self.drift_rate_x = gyro_x - ((accel_pitch - prev_pitch) / dt if dt else 0.0)
        self.drift_rate_y = gyro_y - ((accel_roll - prev_roll) / dt if dt else 0.0)
        self.drift_rate_z = gyro_z - ((raw_heading - prev_heading) / dt if dt else 0.0)

        self.accum_x += self.drift_rate_x * dt
        self.accum_y += self.drift_rate_y * dt
        self.accum_z += self.drift_rate_z * dt

        self.prev_accel_pitch = accel_pitch
        self.prev_accel_roll = accel_roll
        self.prev_heading = raw_heading

        ms_since_calib = (current_time - (self.last_calib_time or current_time)) * 1000.0

        compensated = {
            "pitch": self.comp_pitch,
            "roll": self.comp_roll,
            "heading": self.comp_heading,
        }
        drift = {
            "rateX": self.drift_rate_x,
            "rateY": self.drift_rate_y,
            "rateZ": self.drift_rate_z,
            "accumulatedX": self.accum_x,
            "accumulatedY": self.accum_y,
            "accumulatedZ": self.accum_z,
            "filterAlpha": self.alpha,
            "lastCalibrationMs": ms_since_calib,
        }
        return compensated, drift

    def recalibrate(self) -> None:
        self.accum_x = 0.0
        self.accum_y = 0.0
        self.accum_z = 0.0
        self.last_calib_time = time.time()
        LOGGER.info("IMU recalibrated")

    def set_alpha(self, new_alpha: float) -> None:
        self.alpha = max(0.80, min(0.99, float(new_alpha)))
        LOGGER.info("Complementary filter alpha set to %.3f", self.alpha)
