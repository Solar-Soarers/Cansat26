"""Inbound command handling for the ground-station WebSocket server."""

from __future__ import annotations

import json
import logging
from typing import Any, Dict

from imu_processor import IMUProcessor

LOGGER = logging.getLogger(__name__)


def handle_command(json_str: str, imu_processor: IMUProcessor, config: Dict[str, Any]) -> None:
    """Handle a command payload received from the Electron client."""
    try:
        payload = json.loads(json_str)
    except json.JSONDecodeError:
        LOGGER.warning("Ignoring malformed command: %s", json_str)
        return

    command_type = payload.get("type")
    if command_type == "RECALIBRATE_IMU":
        imu_processor.recalibrate()
        return
    if command_type == "SET_FILTER_ALPHA":
        imu_processor.set_alpha(payload.get("value", config.get("FILTER_ALPHA", 0.96)))
        config["FILTER_ALPHA"] = imu_processor.alpha
        return
    if command_type == "SET_IK_GAINS":
        config["IK_GAINS"] = {"kp": payload.get("kp", 0.0), "kd": payload.get("kd", 0.0)}
        LOGGER.info("IK gains updated: kp=%.3f kd=%.3f", float(payload.get("kp", 0.0)), float(payload.get("kd", 0.0)))
        return
    if command_type == "ARM_AUTOGYRO":
        LOGGER.info("Autogyro arm command received")
        return
    if command_type == "DISARM_AUTOGYRO":
        LOGGER.info("Autogyro disarm command received")
        return

    LOGGER.warning("Unknown command type: %s", command_type)
