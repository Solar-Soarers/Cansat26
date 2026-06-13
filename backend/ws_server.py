"""Async WebSocket broadcast server for telemetry and command handling."""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Any, Callable, Optional, Set

import websockets
from websockets.server import WebSocketServerProtocol

from command_handler import handle_command

LOGGER = logging.getLogger(__name__)


class WSServer:
    """Manage connected Electron clients and broadcast telemetry packets."""

    def __init__(self, host: str = "localhost", port: int = 8765) -> None:
        self.host = host
        self.port = port
        self.clients: Set[WebSocketServerProtocol] = set()
        self.server = None
        self.command_handler: Optional[Callable[[str], None]] = None
        self.on_command = None
        self.imu_processor = None
        self.config = None

    async def start(self, handler: Callable[[str], None] | None = None, imu_processor: Any | None = None, config: dict[str, Any] | None = None) -> None:
        self.on_command = handler
        self.imu_processor = imu_processor
        self.config = config
        self.server = await websockets.serve(self._handler, self.host, self.port)

    async def _handler(self, websocket: WebSocketServerProtocol) -> None:
        self.clients.add(websocket)
        remote = websocket.remote_address
        LOGGER.info("Client connected: %s", remote)
        try:
            async for message in websocket:
                if self.on_command is not None and self.imu_processor is not None and self.config is not None:
                    self.on_command(message)
                else:
                    LOGGER.debug("Received message without command handler: %s", message)
        except websockets.ConnectionClosed:
            pass
        finally:
            self.clients.discard(websocket)
            LOGGER.info("Client disconnected: %s", remote)

    async def broadcast(self, packet_dict: dict[str, Any]) -> None:
        if not self.clients:
            return
        json_str = json.dumps(packet_dict)
        await asyncio.gather(*[client.send(json_str) for client in list(self.clients)], return_exceptions=True)

    async def close(self) -> None:
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
