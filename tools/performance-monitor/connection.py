"""
TCP client for the injected performance monitor DLL.

Connects to the DLL's TCP server, reads length-prefixed JSON frames,
parses them into model objects, and emits Qt signals.
"""

import json
import logging
import socket
import struct
import threading
import time

from PySide6.QtCore import QObject, Signal

from models import (
    TickSnapshot, AutosaveSnapshot,
    parse_tick, parse_autosave,
)

logger = logging.getLogger("perfmon")


class PerfConnection(QObject):
    """Manages TCP connection to the injected DLL's metrics server."""

    connected = Signal(dict)         # hello_ack payload
    disconnected = Signal(str)       # reason
    tick_received = Signal(object)   # TickSnapshot
    autosave_received = Signal(object)  # AutosaveSnapshot
    error = Signal(str)
    log = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False

    @property
    def is_connected(self) -> bool:
        return self._running and self._sock is not None

    def connect_to(self, host: str, port: int):
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(
            target=self._run, args=(host, port), daemon=True
        )
        self._thread.start()

    def disconnect(self):
        self._running = False
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass

    def _run(self, host: str, port: int):
        try:
            self._do_connect(host, port)
        except Exception as e:
            self.error.emit(str(e))
        finally:
            self._running = False
            if self._sock:
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None

    def _do_connect(self, host: str, port: int):
        self.log.emit(f"Connecting to {host}:{port}...")
        self._sock = socket.create_connection((host, port), timeout=10)
        self._sock.settimeout(2.0)

        self.log.emit("Connected, waiting for hello_ack...")

        # Read frames in a loop
        recv_buf = bytearray()

        while self._running:
            try:
                chunk = self._sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                break

            if not chunk:
                break

            recv_buf.extend(chunk)

            # Process complete frames
            while len(recv_buf) >= 4:
                payload_len = struct.unpack(">I", recv_buf[:4])[0]

                if payload_len > 1_000_000:
                    raw_hex = recv_buf[:min(32, len(recv_buf))].hex()
                    self.error.emit(
                        f"Frame too large: {payload_len} bytes "
                        f"(0x{payload_len:08X}). Raw: {raw_hex} -- "
                        f"wrong port? DLL listens on 19800 by default"
                    )
                    self._running = False
                    break

                total = 4 + payload_len
                if len(recv_buf) < total:
                    break

                json_bytes = bytes(recv_buf[4:total])
                del recv_buf[:total]

                try:
                    data = json.loads(json_bytes)
                except json.JSONDecodeError as e:
                    logger.warning("Invalid JSON frame: %s", e)
                    continue

                self._dispatch(data)

        self.log.emit("Connection lost")
        self.disconnected.emit("Connection closed")
        time.sleep(0.1)

    def _dispatch(self, data: dict):
        msg_type = data.get("type", "")

        if msg_type == "hello_ack":
            self.log.emit(
                f"Server: {data.get('level_count', '?')} levels, "
                f"target TPS {data.get('server_tps_target', 20)}"
            )
            self.connected.emit(data)

        elif msg_type == "tick":
            try:
                snap = parse_tick(data)
                self.tick_received.emit(snap)
            except Exception as e:
                logger.warning("Failed to parse tick: %s", e)

        elif msg_type == "autosave":
            try:
                snap = parse_autosave(data)
                self.autosave_received.emit(snap)
            except Exception as e:
                logger.warning("Failed to parse autosave: %s", e)

        else:
            self.log.emit(f"Unknown message type: {msg_type}")
