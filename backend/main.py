"""Entry point for the INSpace CanSat telemetry backend."""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from typing import Any

import config
from command_handler import handle_command
from imu_processor import IMUProcessor
from packet_builder import PacketBuilder
from serial_reader import SerialReader, validate_field_count
from ws_server import WSServer


def _check_venv() -> None:
    in_venv = (
        hasattr(sys, 'real_prefix') or
        (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix)
    )
    if not in_venv:
        print('WARNING: Not running inside a virtual environment.')
        print('         Run: source venv/bin/activate  then retry.')
        print('         Continuing anyway - imports may fail on managed Python.')

_check_venv()


class PacketCommandState(dict[str, Any]):
    """Shared mutable config for runtime-tuned command state."""


def configure_logging() -> None:
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(levelname)s — %(message)s", datefmt="%H:%M:%S")


async def run_backend(mock: bool) -> None:
    reader = SerialReader(config.PORT, config.BAUD, mock=mock)
    imu_proc = IMUProcessor(alpha=config.FILTER_ALPHA)
    builder = PacketBuilder()
    server = WSServer(config.WS_HOST, config.WS_PORT)
    runtime_config: PacketCommandState = PacketCommandState(
        FILTER_ALPHA=config.FILTER_ALPHA,
        IK_GAINS={"kp": 1.2, "kd": 0.05},
    )

    await server.start(
        handler=lambda message: handle_command(message, imu_proc, runtime_config),
        imu_processor=imu_proc,
        config=runtime_config,
    )
    logging.info("WS server listening on ws://%s:%s", config.WS_HOST, config.WS_PORT)
    logging.info("Opening serial port %s at %s baud", config.PORT, config.BAUD)

    try:
        async for raw_line in reader.lines():
            try:
                raw_fields = raw_line.split(",")
                if len(raw_fields) != 42 or not validate_field_count(raw_line):
                    logging.warning("MALFORMED PKT — got %s fields: %s", len(raw_fields), raw_line[:80])
                    continue
                compensated, drift = imu_proc.update({
                    "accelX": float(raw_fields[8]),
                    "accelY": float(raw_fields[9]),
                    "accelZ": float(raw_fields[10]),
                    "gyroX": float(raw_fields[5]),
                    "gyroY": float(raw_fields[6]),
                    "gyroZ": float(raw_fields[7]),
                    "heading": float(raw_fields[4]),
                })
                packet = builder.build(raw_fields, compensated, drift)
                await server.broadcast(packet)
            except Exception as error:  # pragma: no cover - packet-level safety
                logging.exception("Packet processing failure: %s", error)
                continue
    finally:
        await server.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="INSpace CanSat telemetry backend")
    parser.add_argument("--port", default=config.PORT)
    parser.add_argument("--baud", type=int, default=config.BAUD)
    parser.add_argument("--alpha", type=float, default=config.FILTER_ALPHA)
    parser.add_argument("--mock", action="store_true")
    args = parser.parse_args()

    config.PORT = args.port
    config.BAUD = args.baud
    config.FILTER_ALPHA = args.alpha

    configure_logging()
    asyncio.run(run_backend(args.mock))
