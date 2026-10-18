"""
Bot player manager for server load testing.

Each bot connects to the game server as a real client using the same
connection flow as the server-monitor tool (PreLogin -> Login -> cipher
handshake -> identity tokens -> keepalive).
"""

import logging
import os
import secrets
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass

from PySide6.QtCore import QObject, Signal

# Import protocol code from the sibling server-monitor tool.
_PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_SM_PATH = os.path.join(_PARENT, "server-monitor")
if _SM_PATH not in sys.path:
    sys.path.append(_SM_PATH)

from protocol import frame_packet, DataInputStream
from packets import (
    PRE_LOGIN, LOGIN, KEEP_ALIVE, CUSTOM_PAYLOAD, DISCONNECT,
    build_prelogin, build_login, build_keep_alive,
    build_custom_payload, parse_prelogin_response,
    parse_login_response_stream, DISCONNECT_REASONS,
)

logger = logging.getLogger("perfmon")

# Reuse the same constants/patterns as server-monitor/connection.py
SMALLID_REJECT = 0xFF

CIPHER_KEY_PREFIX = (
    b"\xFA\x00\x07"
    b"\x00\x4D\x00\x43\x00\x7C\x00\x43\x00\x4B\x00\x65\x00\x79"
    b"\x00\x20"
)
CIPHER_KEY_TOTAL = len(CIPHER_KEY_PREFIX) + 32

CIPHER_ON_PATTERN = (
    b"\xFA\x00\x06"
    b"\x00\x4D\x00\x43\x00\x7C\x00\x43\x00\x4F\x00\x6E"
    b"\x00\x00"
)

CIPHER_ACK_CHANNEL = "MC|CAck"
IDENTITY_TOKEN_ISSUE = "MC|CTIssue"
IDENTITY_TOKEN_CHALLENGE = "MC|CTChallenge"
IDENTITY_TOKEN_RESPONSE = "MC|CTResponse"

PHASE_HANDSHAKE = 0
PHASE_CIPHER_SCAN = 1
PHASE_MONITORING = 2


def _build_channel_pattern(channel: str) -> bytes:
    result = b"\xFA"
    result += struct.pack(">h", len(channel))
    for ch in channel:
        result += struct.pack(">H", ord(ch))
    return result


class CipherState:
    def __init__(self, key: bytes, iv: bytes):
        from Crypto.Cipher import AES
        from Crypto.Util import Counter
        ctr = Counter.new(128, initial_value=int.from_bytes(iv, "big"))
        self._cipher = AES.new(key, AES.MODE_CTR, counter=ctr)

    def process(self, data: bytes) -> bytes:
        return self._cipher.encrypt(data)


@dataclass
class BotState:
    name: str
    thread: threading.Thread | None = None
    sock: socket.socket | None = None
    running: bool = False
    connected: bool = False


class BotConnection:
    """Single bot connection -- mirrors ServerConnection from server-monitor."""

    def __init__(self, bot: BotState, host: str, port: int, xuid: int,
                 log_fn, on_connected_fn):
        self._bot = bot
        self._host = host
        self._port = port
        self._xuid = xuid
        self._log = log_fn
        self._on_connected = on_connected_fn
        self._sock: socket.socket | None = None
        self._recv_buf = bytearray()
        self._scan_buf = bytearray()
        self._phase = PHASE_HANDSHAKE
        self._got_prelogin = False
        self._got_login = False
        self._recv_cipher: CipherState | None = None
        self._send_cipher: CipherState | None = None
        self._cipher_key = b""
        self._cipher_iv = b""
        self._cipher_scan_start = 0.0
        self._identity_token = b""

    def run(self):
        self._log(f"Bot '{self._bot.name}' connecting to {self._host}:{self._port}...")
        self._sock = socket.create_connection((self._host, self._port), timeout=10)
        self._bot.sock = self._sock
        self._sock.settimeout(5.0)

        # Phase 1: SmallID assignment
        first_byte = self._recv_exact(1)
        if first_byte[0] == SMALLID_REJECT:
            reject_data = self._recv_exact(5)
            reason_id = struct.unpack(">i", reject_data[1:5])[0]
            reason = DISCONNECT_REASONS.get(reason_id, f"Unknown({reason_id})")
            self._log(f"Bot '{self._bot.name}' rejected: {reason}")
            return

        self._log(f"Bot '{self._bot.name}' assigned smallId={first_byte[0]}")

        # Send PreLogin
        self._send_packet(PRE_LOGIN, build_prelogin(self._bot.name))
        self._log(f"Bot '{self._bot.name}' sent PreLogin")

        # Main recv loop
        self._sock.settimeout(1.0)
        last_keepalive = time.time()
        keepalive_counter = 0

        while self._bot.running:
            try:
                chunk = self._sock.recv(65536)
            except socket.timeout:
                # Cipher scan timeout
                if (self._phase == PHASE_CIPHER_SCAN
                        and not self._cipher_key
                        and time.time() - self._cipher_scan_start > 3.0):
                    self._handle_cipher_scan(b"")
                # Keepalive
                now = time.time()
                if now - last_keepalive >= 10.0:
                    keepalive_counter += 1
                    self._send_packet(KEEP_ALIVE, build_keep_alive(keepalive_counter))
                    last_keepalive = now
                continue
            except OSError:
                break

            if not chunk:
                self._log(f"Bot '{self._bot.name}' server closed connection")
                break

            self._recv_buf.extend(chunk)
            self._process_frames()

            now = time.time()
            if now - last_keepalive >= 10.0:
                keepalive_counter += 1
                self._send_packet(KEEP_ALIVE, build_keep_alive(keepalive_counter))
                last_keepalive = now

        self._log(f"Bot '{self._bot.name}' disconnected")

    def _process_frames(self):
        while len(self._recv_buf) >= 4:
            payload_len = struct.unpack(">I", self._recv_buf[:4])[0]
            total = 4 + payload_len
            if len(self._recv_buf) < total:
                break

            raw_payload = bytes(self._recv_buf[4:total])
            del self._recv_buf[:total]

            if self._recv_cipher:
                raw_payload = self._recv_cipher.process(raw_payload)

            if not raw_payload:
                continue

            if self._phase == PHASE_HANDSHAKE:
                self._handle_handshake(raw_payload)
            elif self._phase == PHASE_CIPHER_SCAN:
                self._handle_cipher_scan(raw_payload)
            elif self._phase == PHASE_MONITORING:
                self._handle_monitoring(raw_payload)

    def _handle_handshake(self, payload: bytes):
        if not payload:
            return
        packet_id = payload[0]
        data = payload[1:]

        if packet_id == PRE_LOGIN and not self._got_prelogin:
            try:
                parsed = parse_prelogin_response(data)
                self._got_prelogin = True
                self._log(f"Bot '{self._bot.name}' PreLogin OK: {parsed['player_count']} players online")
                self._send_packet(LOGIN, build_login(self._bot.name, self._xuid))
                self._log(f"Bot '{self._bot.name}' sent Login")
            except Exception as e:
                self._log(f"Bot '{self._bot.name}' PreLogin parse error: {e}")

        elif packet_id == LOGIN and not self._got_login:
            try:
                dis = DataInputStream(data)
                parsed = parse_login_response_stream(dis)
                self._got_login = True
                self._log(f"Bot '{self._bot.name}' Login OK: entityId={parsed['entity_id']}")
                self._phase = PHASE_CIPHER_SCAN
                self._cipher_scan_start = time.time()
                # Feed remaining bytes from this frame into cipher scan
                remaining = data[len(data) - dis.remaining:]
                if remaining:
                    self._handle_cipher_scan(remaining)
            except Exception as e:
                self._log(f"Bot '{self._bot.name}' Login parse error: {e}")

        elif packet_id == DISCONNECT:
            try:
                dis = DataInputStream(data)
                reason_id = dis.read_int()
                reason = DISCONNECT_REASONS.get(reason_id, f"Unknown({reason_id})")
                self._log(f"Bot '{self._bot.name}' kicked: {reason}")
            except Exception:
                self._log(f"Bot '{self._bot.name}' kicked (unknown reason)")
            self._bot.running = False

    def _handle_cipher_scan(self, payload: bytes):
        self._scan_buf.extend(payload)

        # Timeout: no cipher key after 3s = server has cipher disabled
        if (not self._cipher_key
                and time.time() - self._cipher_scan_start > 3.0):
            self._log(f"Bot '{self._bot.name}' no cipher, proceeding unencrypted")
            self._phase = PHASE_MONITORING
            self._bot.connected = True
            self._on_connected()
            buf = bytes(self._scan_buf)
            self._scan_buf.clear()
            if buf:
                self._handle_monitoring(buf)
            return

        # Look for MC|CKey
        if not self._cipher_key:
            idx = self._scan_buf.find(CIPHER_KEY_PREFIX)
            if idx >= 0 and idx + CIPHER_KEY_TOTAL <= len(self._scan_buf):
                key_start = idx + len(CIPHER_KEY_PREFIX)
                key_data = bytes(self._scan_buf[key_start:key_start + 32])
                self._cipher_key = key_data[:16]
                self._cipher_iv = key_data[16:32]
                self._log(f"Bot '{self._bot.name}' got cipher key")

                # Send MC|CAck and activate send cipher
                self._send_packet(CUSTOM_PAYLOAD, build_custom_payload(CIPHER_ACK_CHANNEL))
                iv_send = bytearray(self._cipher_iv)
                iv_send[0] ^= 0x80
                self._send_cipher = CipherState(self._cipher_key, bytes(iv_send))
                self._log(f"Bot '{self._bot.name}' send cipher active")

                del self._scan_buf[:idx + CIPHER_KEY_TOTAL]

        # Look for MC|COn
        if self._cipher_key and not self._recv_cipher:
            idx = self._scan_buf.find(CIPHER_ON_PATTERN)
            if idx >= 0:
                self._recv_cipher = CipherState(self._cipher_key, self._cipher_iv)
                self._log(f"Bot '{self._bot.name}' cipher handshake complete")
                self._phase = PHASE_MONITORING
                self._bot.connected = True
                self._on_connected()
                self._scan_buf.clear()

    def _handle_monitoring(self, payload: bytes):
        self._scan_buf.extend(payload)
        self._scan_identity_tokens()
        # Trim buffer
        if len(self._scan_buf) > 4096:
            del self._scan_buf[:-512]

    def _scan_identity_tokens(self):
        # MC|CTIssue: server sends a token for us to store
        issue_prefix = _build_channel_pattern(IDENTITY_TOKEN_ISSUE)
        idx = self._scan_buf.find(issue_prefix)
        if idx >= 0:
            data_start = idx + len(issue_prefix)
            if data_start + 2 + 32 <= len(self._scan_buf):
                data_len = struct.unpack(">h", self._scan_buf[data_start:data_start+2])[0]
                if data_len == 32:
                    token = bytes(self._scan_buf[data_start+2:data_start+2+32])
                    self._identity_token = token
                    self._log(f"Bot '{self._bot.name}' received identity token")
                    del self._scan_buf[:data_start + 2 + 32]
                    return

        # MC|CTChallenge: server wants us to present our token
        challenge_prefix = _build_channel_pattern(IDENTITY_TOKEN_CHALLENGE)
        idx = self._scan_buf.find(challenge_prefix)
        if idx >= 0:
            data_start = idx + len(challenge_prefix)
            if data_start + 2 <= len(self._scan_buf):
                del self._scan_buf[:data_start + 2]
                if self._identity_token and len(self._identity_token) == 32:
                    self._send_packet(CUSTOM_PAYLOAD,
                        build_custom_payload(IDENTITY_TOKEN_RESPONSE, self._identity_token))
                    self._log(f"Bot '{self._bot.name}' sent identity token response")
                else:
                    self._send_packet(CUSTOM_PAYLOAD,
                        build_custom_payload(IDENTITY_TOKEN_RESPONSE))
                    self._log(f"Bot '{self._bot.name}' sent empty identity token response")

    def _send_packet(self, packet_id: int, payload: bytes):
        raw = frame_packet(packet_id, payload)
        if self._send_cipher:
            header = raw[:4]
            encrypted = self._send_cipher.process(raw[4:])
            raw = header + encrypted
        try:
            if self._sock:
                self._sock.sendall(raw)
        except OSError:
            pass

    def _recv_exact(self, n: int) -> bytes:
        data = b""
        while len(data) < n:
            chunk = self._sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed during recv")
            data += chunk
        return data


class BotManager(QObject):
    """Manages bot player connections for load testing."""

    bot_added = Signal(str)
    bot_removed = Signal(str)
    bot_error = Signal(str, str)
    log = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bots: dict[str, BotState] = {}
        self._lock = threading.Lock()
        self._next_id = 1

    @property
    def bot_count(self) -> int:
        with self._lock:
            return len(self._bots)

    def add_bot(self, host: str, port: int) -> str:
        with self._lock:
            name = f"Bot{self._next_id}"
            self._next_id += 1
            xuid = secrets.randbits(62) | (1 << 32)

        bot = BotState(name=name)
        bot.running = True
        bot.thread = threading.Thread(
            target=self._run_bot, args=(bot, host, port, xuid),
            daemon=True)

        with self._lock:
            self._bots[name] = bot

        bot.thread.start()
        return name

    def remove_bot(self) -> str | None:
        with self._lock:
            if not self._bots:
                return None
            name = list(self._bots.keys())[-1]
            bot = self._bots.pop(name)

        bot.running = False
        if bot.sock:
            try:
                bot.sock.close()
            except OSError:
                pass
        self.bot_removed.emit(name)
        self.log.emit(f"Bot '{name}' disconnecting")
        return name

    def remove_all(self):
        with self._lock:
            names = list(self._bots.keys())
        for _ in names:
            self.remove_bot()

    def _run_bot(self, bot: BotState, host: str, port: int, xuid: int):
        try:
            def on_connected():
                self.bot_added.emit(bot.name)

            conn = BotConnection(bot, host, port, xuid, self.log.emit, on_connected)
            conn.run()
        except Exception as e:
            self.bot_error.emit(bot.name, str(e))
            self.log.emit(f"Bot '{bot.name}' error: {e}")
        finally:
            bot.running = False
            bot.connected = False
            if bot.sock:
                try:
                    bot.sock.close()
                except OSError:
                    pass
                bot.sock = None
            with self._lock:
                self._bots.pop(bot.name, None)
