"""Serial and mock packet ingestion for the telemetry backend."""

from __future__ import annotations

import asyncio
import logging
import random
from collections.abc import AsyncIterator
from typing import List, Optional

import serial

LOGGER = logging.getLogger(__name__)
EXPECTED_FIELD_COUNT = 42


class SerialReader:
    """Read raw CSV telemetry lines from UART or generate mock packets."""

    def __init__(self, port: str, baud: int, mock: bool = False) -> None:
        self.port = port
        self.baud = baud
        self.mock = mock
        self._serial: Optional[serial.Serial] = None
        self._mock_state = {
            "packet_id": 1,
            "time_ms": 0,
            "altitude": 342.6,
            "lat": 28.6139,
            "lon": 77.2090,
            "heading": 127.0,
            "pitch": 12.4,
            "roll": -3.8,
            "battery_mv": 3840.0,
            "gyro_z": 0.03,
            "rotor_rpm": 1240.0,
        }

    async def lines(self) -> AsyncIterator[str]:
        """Yield one raw CSV telemetry line at a time."""
        if self.mock:
            async for line in self._mock_lines():
                yield line
            return

        self._serial = serial.Serial(self.port, self.baud, timeout=1)
        loop = asyncio.get_running_loop()
        try:
            while True:
                line = await loop.run_in_executor(None, self._serial.readline)
                if not line:
                    continue
                text = line.decode("utf-8", errors="ignore").strip()
                if text:
                    yield text
        finally:
            if self._serial and self._serial.is_open:
                self._serial.close()

    async def _mock_lines(self) -> AsyncIterator[str]:
        while True:
            yield self._generate_mock_line()
            await asyncio.sleep(0.1)

    def _generate_mock_line(self) -> str:
        state = self._mock_state
        state["time_ms"] += 100
        state["altitude"] = max(0.0, state["altitude"] - 0.25 + random.uniform(-0.08, 0.08))
        state["lat"] += random.uniform(-0.00002, 0.00002)
        state["lon"] += random.uniform(-0.00002, 0.00002)
        state["heading"] = (state["heading"] + random.uniform(-1.0, 1.0)) % 360.0
        state["pitch"] += random.uniform(-0.2, 0.2)
        state["roll"] += random.uniform(-0.2, 0.2)
        state["battery_mv"] = max(3200.0, state["battery_mv"] - 0.2)
        state["gyro_z"] += 0.003
        state["rotor_rpm"] = max(800.0, min(2600.0, state["rotor_rpm"] + random.uniform(-15.0, 20.0)))

        fields = [
            state["packet_id"],
            1716720000000 + state["time_ms"],
            state["pitch"],
            state["roll"],
            state["heading"],
            random.uniform(-0.2, 0.2),
            random.uniform(-0.2, 0.2),
            0.03 + state["gyro_z"],
            random.uniform(-0.1, 0.1),
            random.uniform(-0.1, 0.1),
            9.81 + random.uniform(-0.05, 0.05),
            random.uniform(-35.0, 35.0),
            random.uniform(-35.0, 35.0),
            random.uniform(-35.0, 35.0),
            state["altitude"],
            state["altitude"] + random.uniform(-1.2, 1.2),
            97010.0 + random.uniform(-100.0, 100.0),
            24.3 + random.uniform(-0.5, 0.5),
            31.2 + random.uniform(-0.5, 0.5),
            62.0 + random.uniform(-1.0, 1.0),
            state["lat"],
            state["lon"],
            random.uniform(0.0, 18.0),
            state["heading"],
            9,
            0.9,
            2,
            state["rotor_rpm"],
            14.2,
            -6.7,
            87.0,
            2,
            state["battery_mv"],
            76.0,
            210.0,
            -68,
            12.4,
            3,
            1,
            1,
            0,
            3.2,
        ]
        state["packet_id"] += 1
        return ",".join(str(value) for value in fields)


def validate_field_count(raw_line: str) -> bool:
    """Validate that a line contains the expected number of comma-separated fields."""
    return len(raw_line.split(",")) == EXPECTED_FIELD_COUNT
