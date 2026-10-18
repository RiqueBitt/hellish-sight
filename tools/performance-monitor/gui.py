"""
PySide6 GUI for the server-side performance monitor.

Connects to the injected DLL's TCP server and visualizes tick-level
performance data: phase breakdown, TPS graph, entity/chunk counts,
memory usage, and lag spike root causes.
"""

import logging
import time
from collections import deque

from PySide6.QtCore import Qt, QTimer, Slot
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QGroupBox,
    QSpinBox, QSplitter, QTextEdit, QScrollArea,
)

from connection import PerfConnection
from bots import BotManager
from models import TickSnapshot, AutosaveSnapshot
from widgets import (
    TickBreakdownBar, TPSGraph, StatCard,
    EntityChunkPanel, MemoryBar, LagSpikePanel,
)


DIMENSION_NAMES = {0: "Overworld", -1: "Nether", 1: "End"}


def compute_deltas(prev: TickSnapshot | None, curr: TickSnapshot) -> list[str]:
    """Compare two consecutive snapshots and describe what changed."""
    if prev is None:
        return []

    events: list[str] = []

    prev_levels = {lv.dimension: lv for lv in prev.levels}
    curr_levels = {lv.dimension: lv for lv in curr.levels}

    for dim in (0, -1, 1):
        p = prev_levels.get(dim)
        c = curr_levels.get(dim)
        if not p or not c:
            continue

        name = DIMENSION_NAMES.get(dim, f"Dim{dim}")

        # Player join/leave
        pdiff = c.player_count - p.player_count
        if pdiff > 0:
            events.append(f"+{pdiff} player{'s' if pdiff > 1 else ''} joined ({name})")
        elif pdiff < 0:
            events.append(f"{pdiff} player{'s' if pdiff < -1 else ''} left ({name})")

        # Entity spawn/despawn (threshold to avoid noise from normal fluctuation)
        ediff = c.entity_count - p.entity_count
        if ediff >= 10:
            events.append(f"+{ediff} entities spawned ({name}, now {c.entity_count})")
        elif ediff <= -10:
            events.append(f"{ediff} entities despawned ({name}, now {c.entity_count})")

        # Tile entity changes (redstone, hoppers, etc.)
        tdiff = c.tile_entity_count - p.tile_entity_count
        if tdiff >= 5:
            events.append(f"+{tdiff} tile entities ({name})")
        elif tdiff <= -5:
            events.append(f"{tdiff} tile entities ({name})")

        # Entities pending removal spike
        if c.entities_to_remove > 10 and c.entities_to_remove > p.entities_to_remove + 5:
            events.append(f"{c.entities_to_remove} entities queued for removal ({name})")

        # Chunk load/unload
        if c.loaded_chunks > 0 and p.loaded_chunks > 0:
            cdiff = c.loaded_chunks - p.loaded_chunks
            if cdiff >= 5:
                events.append(f"+{cdiff} chunks loaded ({name}, now {c.loaded_chunks})")
            elif cdiff <= -5:
                events.append(f"{cdiff} chunks unloaded ({name}, now {c.loaded_chunks})")

    return events


def classify_spike(interval_ms: float, work_ms: float,
                   snap: TickSnapshot, prev: TickSnapshot | None,
                   prev_timestamps: list[float]) -> str:
    """Classify a lag spike by its most likely cause, with context from deltas."""

    # Compute what changed since last tick
    deltas = compute_deltas(prev, snap)
    delta_str = "; ".join(deltas) if deltas else ""

    # Check for player join/leave (takes priority over autosave classification)
    player_joined = any("joined" in d for d in deltas)
    player_left = any("left" in d for d in deltas)
    chunk_change = any("chunk" in d for d in deltas)

    # --- Player join stall ---
    # When a player joins, the server loads their chunks, sends world data,
    # and initializes the player entity. This blocks the run loop.
    if player_joined and interval_ms > 200:
        reason = f"Player join (server stalled {interval_ms:.0f}ms loading player data)"
        if delta_str:
            reason += f" | {delta_str}"
        return reason

    # --- Player leave ---
    if player_left and interval_ms > 200:
        reason = f"Player leave (cleanup stalled {interval_ms:.0f}ms)"
        if delta_str:
            reason += f" | {delta_str}"
        return reason

    # --- Autosave detection ---
    # Large stall (>500ms) with low tick work and NO player join/leave.
    # Autosave's synchronous Flush() compresses + writes the save file,
    # blocking the entire run() loop. Typically recurs at a regular interval.
    if interval_ms > 500 and work_ms < 50:
        pattern = ""
        if len(prev_timestamps) >= 1:
            gap = time.time() - prev_timestamps[-1]
            pattern = f", ~{gap:.0f}s since last spike"
        reason = f"Autosave (sync flush for {interval_ms:.0f}ms{pattern})"
        if delta_str:
            reason += f" | {delta_str}"
        return reason

    # --- Heavy tick (entity/redstone overload) ---
    if work_ms > 50:
        reason = f"Heavy tick ({work_ms:.1f}ms work, {snap.total_entity_count} entities)"
        if delta_str:
            reason += f" | {delta_str}"
        return reason

    # --- Moderate tick slowdown ---
    if work_ms > 25:
        reason = f"Slow tick ({work_ms:.1f}ms work, {snap.total_entity_count} entities)"
        if delta_str:
            reason += f" | {delta_str}"
        return reason

    # --- Short stall with context ---
    if delta_str:
        return f"Stall ({interval_ms:.0f}ms gap, tick {work_ms:.1f}ms) | {delta_str}"

    # --- Minor external stall (no obvious context) ---
    if interval_ms < 200:
        return f"Minor stall ({interval_ms:.0f}ms gap, tick only {work_ms:.1f}ms)"

    # --- Medium external stall ---
    return f"External stall ({interval_ms:.0f}ms gap, tick only {work_ms:.1f}ms)"


class PerfMonitorWindow(QMainWindow):

    def __init__(self):
        super().__init__()
        self.setWindowTitle("LCE Server Performance Monitor")
        self.setMinimumSize(1000, 700)

        self._connection = PerfConnection(self)
        self._bot_manager = BotManager(self)
        self._flog = logging.getLogger("perfmon")

        # History for timeline
        self._history: deque[TickSnapshot] = deque(maxlen=6000)  # 5 min at 20 TPS

        # Wire signals
        self._connection.connected.connect(self._on_connected)
        self._connection.disconnected.connect(self._on_disconnected)
        self._connection.tick_received.connect(self._on_tick)
        self._connection.autosave_received.connect(self._on_autosave)
        self._connection.error.connect(self._on_error)
        self._connection.log.connect(self._append_log)

        self._bot_manager.log.connect(self._append_log)
        self._bot_manager.bot_added.connect(self._on_bot_added)
        self._bot_manager.bot_removed.connect(self._on_bot_removed)

        self._build_ui()

        # Graph refresh timer
        self._refresh = QTimer(self)
        self._refresh.timeout.connect(self._refresh_graph)
        self._refresh.start(500)

        # Periodic TPS log (every 5 seconds)
        self._tps_log_timer = QTimer(self)
        self._tps_log_timer.timeout.connect(self._log_tps)
        self._tps_log_timer.start(5000)
        self._last_logged_tps = 0.0

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        # --- Connection bar ---
        conn_box = QGroupBox("Connection")
        conn_box.setStyleSheet(
            "QGroupBox { color: #aaaacc; border: 1px solid #333348; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
            "QGroupBox::title { subcontrol-position: top left; padding: 2px 8px; }"
        )
        conn_layout = QHBoxLayout(conn_box)

        conn_layout.addWidget(QLabel("Host:"))
        self._host_input = QLineEdit("127.0.0.1")
        self._host_input.setFixedWidth(140)
        conn_layout.addWidget(self._host_input)

        conn_layout.addWidget(QLabel("Port:"))
        self._port_input = QSpinBox()
        self._port_input.setRange(1, 65535)
        self._port_input.setValue(19800)
        self._port_input.setFixedWidth(80)
        conn_layout.addWidget(self._port_input)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setFixedWidth(100)
        self._connect_btn.clicked.connect(self._toggle_connection)
        conn_layout.addWidget(self._connect_btn)

        self._status_label = QLabel("Disconnected")
        self._status_label.setStyleSheet("color: #ff5555; font-weight: bold;")
        conn_layout.addWidget(self._status_label)
        conn_layout.addStretch()
        root.addWidget(conn_box)

        # --- Bot controls ---
        bot_box = QGroupBox("Load Testing")
        bot_box.setStyleSheet(
            "QGroupBox { color: #aaaacc; border: 1px solid #333348; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
            "QGroupBox::title { subcontrol-position: top left; padding: 2px 8px; }"
        )
        bot_layout = QHBoxLayout(bot_box)

        bot_layout.addWidget(QLabel("Game Port:"))
        self._game_port_input = QSpinBox()
        self._game_port_input.setRange(1, 65535)
        self._game_port_input.setValue(25565)
        self._game_port_input.setFixedWidth(80)
        bot_layout.addWidget(self._game_port_input)

        self._add_bot_btn = QPushButton("Add Bot")
        self._add_bot_btn.setFixedWidth(80)
        self._add_bot_btn.clicked.connect(self._add_bot)
        bot_layout.addWidget(self._add_bot_btn)

        self._remove_bot_btn = QPushButton("Remove Bot")
        self._remove_bot_btn.setFixedWidth(100)
        self._remove_bot_btn.clicked.connect(self._remove_bot)
        bot_layout.addWidget(self._remove_bot_btn)

        self._remove_all_bots_btn = QPushButton("Remove All")
        self._remove_all_bots_btn.setFixedWidth(90)
        self._remove_all_bots_btn.clicked.connect(self._remove_all_bots)
        bot_layout.addWidget(self._remove_all_bots_btn)

        self._bot_count_label = QLabel("Bots: 0")
        self._bot_count_label.setStyleSheet("color: #aaaacc; font-weight: bold;")
        bot_layout.addWidget(self._bot_count_label)

        bot_layout.addStretch()
        root.addWidget(bot_box)

        # --- Stat cards ---
        stats_layout = QHBoxLayout()
        self._tps_card = StatCard("TPS (est.)")
        self._mspt_card = StatCard("Tick Time")
        self._entities_card = StatCard("Entities")
        self._players_card = StatCard("Players")
        self._memory_card = StatCard("Memory")
        self._spikes_card = StatCard("Lag Spikes")

        for card in [
            self._tps_card, self._mspt_card, self._entities_card,
            self._players_card, self._memory_card, self._spikes_card,
        ]:
            stats_layout.addWidget(card)
        root.addLayout(stats_layout)

        # --- Tick breakdown bar ---
        self._breakdown = TickBreakdownBar()
        root.addWidget(self._breakdown)

        # --- Main content splitter ---
        hsplit = QSplitter(Qt.Horizontal)

        # Left: graph + memory
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(0, 0, 0, 0)

        self._graph = TPSGraph()
        left_layout.addWidget(self._graph)

        self._memory_bar = MemoryBar()
        left_layout.addWidget(self._memory_bar)

        hsplit.addWidget(left)

        # Right: entity panel + spike panel
        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)

        self._entity_panel = EntityChunkPanel()
        right_layout.addWidget(self._entity_panel)

        self._spike_panel = LagSpikePanel()
        spike_scroll = QScrollArea()
        spike_scroll.setWidget(self._spike_panel)
        spike_scroll.setWidgetResizable(True)
        spike_scroll.setStyleSheet("QScrollArea { border: none; background: transparent; }")
        right_layout.addWidget(spike_scroll)

        hsplit.addWidget(right)
        hsplit.setStretchFactor(0, 3)
        hsplit.setStretchFactor(1, 2)
        root.addWidget(hsplit)

        # --- Log pane ---
        log_box = QGroupBox("Log")
        log_box.setStyleSheet(
            "QGroupBox { color: #aaaacc; border: 1px solid #333348; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
            "QGroupBox::title { subcontrol-position: top left; padding: 2px 8px; }"
        )
        log_layout = QVBoxLayout(log_box)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Consolas", 9))
        self._log.setStyleSheet("background: #12121a; color: #ccccdd; border: none;")
        self._log.setMaximumHeight(150)
        log_layout.addWidget(self._log)
        root.addWidget(log_box)

        self._apply_theme()

    def _apply_theme(self):
        self.setStyleSheet("""
            QMainWindow { background: #0e0e16; }
            QWidget { color: #ccccdd; font-family: 'Segoe UI', sans-serif; }
            QLabel { color: #aaaacc; }
            QLineEdit, QSpinBox {
                background: #1a1a24; color: #ffffff; border: 1px solid #333348;
                border-radius: 4px; padding: 4px 8px;
            }
            QPushButton {
                background: #2a2a3a; color: #ffffff; border: 1px solid #444466;
                border-radius: 4px; padding: 6px 16px;
            }
            QPushButton:hover { background: #3a3a4a; }
            QPushButton:pressed { background: #1a1a2a; }
            QGroupBox { background: #14141e; }
        """)

    # --- Spike counter ---
    _spike_count = 0
    _spike_timestamps: list[float] = []
    _prev_snap: TickSnapshot | None = None

    # TPS tracking
    _tps_samples: list[float] = []

    # --- Slots ---

    def _toggle_connection(self):
        if self._connection.is_connected:
            self._connection.disconnect()
        else:
            host = self._host_input.text().strip()
            port = self._port_input.value()
            self._history.clear()
            self._spike_count = 0
            self._spike_timestamps = []
            self._prev_snap = None
            self._tps_samples = []
            self._connection.connect_to(host, port)

    @Slot(dict)
    def _on_connected(self, hello: dict):
        self._status_label.setText("Connected")
        self._status_label.setStyleSheet("color: #50c878; font-weight: bold;")
        self._connect_btn.setText("Disconnect")

    @Slot(str)
    def _on_disconnected(self, reason: str):
        self._status_label.setText(f"Disconnected: {reason}")
        self._status_label.setStyleSheet("color: #ff5555; font-weight: bold;")
        self._connect_btn.setText("Connect")

    @Slot(object)
    def _on_tick(self, snap: TickSnapshot):
        self._history.append(snap)
        now = time.time()

        # TPS from tick-to-tick interval (total_us).
        # This measures the real time between consecutive tick() calls,
        # including any stalls from autosave or other work in run().
        interval_us = snap.total_us
        if interval_us > 0:
            instant_tps = min(1_000_000.0 / interval_us, 20.0)
        else:
            instant_tps = 20.0

        self._tps_samples.append(instant_tps)
        if len(self._tps_samples) > 20:
            self._tps_samples = self._tps_samples[-20:]
        tps = sum(self._tps_samples) / len(self._tps_samples)

        if tps >= 19.0:
            tps_color = "#50c878"
        elif tps >= 15.0:
            tps_color = "#ffc83d"
        else:
            tps_color = "#ff5050"
        self._tps_card.set_value(f"{tps:.1f}", tps_color)

        # MSPT = actual tick() work time (carried in pool_reset_us)
        mspt = snap.pool_reset_us / 1000.0
        if mspt <= 50:
            mspt_color = "#50c878"
        elif mspt <= 65:
            mspt_color = "#ffc83d"
        else:
            mspt_color = "#ff5050"
        self._mspt_card.set_value(f"{mspt:.1f}ms", mspt_color)

        total_ent = snap.total_entity_count
        self._entities_card.set_value(str(total_ent))
        total_players = sum(lv.player_count for lv in snap.levels)
        self._players_card.set_value(str(total_players))

        self._memory_card.set_value(f"{snap.memory_used_mb:.0f}MB")

        # Update breakdown bar
        self._breakdown.update_from_snapshot(snap)

        # Update entity/chunk panel
        self._entity_panel.update_from_snapshot(snap)

        # Update memory bar
        self._memory_bar.update_memory(snap.memory_used_mb, snap.memory_total_mb)

        # TPS graph data point
        self._graph.add_point(now, tps)

        # Check for lag spikes: tick interval exceeded 150ms (3x the 50ms budget).
        # This filters out steady-state slowness (e.g. 15 TPS = 66ms intervals)
        # and only reports true spikes -- autosave stalls, player joins, etc.
        if snap.total_us > 150000:
            self._spike_count += 1
            interval_ms = snap.total_us / 1000.0
            work_ms = snap.pool_reset_us / 1000.0

            reason = classify_spike(
                interval_ms, work_ms, snap, self._prev_snap,
                self._spike_timestamps)
            self._spike_timestamps.append(now)

            self._spike_panel.add_spike_with_reason(snap, reason)
            self._append_log(
                f"LAG SPIKE: {interval_ms:.0f}ms interval, "
                f"tick work={work_ms:.1f}ms, "
                f"entities={snap.total_entity_count}, "
                f"TPS={tps:.1f}, "
                f"cause={reason}"
            )

        self._spikes_card.set_value(
            str(self._spike_count),
            "#ff5050" if self._spike_count > 0 else "#50c878",
        )

        # Log notable events even without a spike
        if self._prev_snap is not None and snap.total_us <= 75000:
            deltas = compute_deltas(self._prev_snap, snap)
            for d in deltas:
                if "joined" in d or "left" in d:
                    self._append_log(f"EVENT: {d}")

        self._prev_snap = snap

    @Slot(object)
    def _on_autosave(self, snap: AutosaveSnapshot):
        self._spike_panel.add_autosave(snap)
        self._append_log(
            f"AUTOSAVE: {snap.total_ms:.0f}ms total "
            f"(players={snap.players_us / 1000:.0f}ms, "
            f"levels={snap.levels_us / 1000:.0f}ms, "
            f"rules={snap.rules_us / 1000:.0f}ms, "
            f"flush={snap.flush_us / 1000:.0f}ms)"
        )

    @Slot(str)
    def _on_error(self, msg: str):
        self._append_log(f"ERROR: {msg}")

    # --- Bot controls ---

    def _add_bot(self):
        host = self._host_input.text().strip()
        port = self._game_port_input.value()
        self._bot_manager.add_bot(host, port)

    def _remove_bot(self):
        self._bot_manager.remove_bot()

    def _remove_all_bots(self):
        self._bot_manager.remove_all()

    @Slot(str)
    def _on_bot_added(self, name: str):
        self._bot_count_label.setText(f"Bots: {self._bot_manager.bot_count}")

    @Slot(str)
    def _on_bot_removed(self, name: str):
        self._bot_count_label.setText(f"Bots: {self._bot_manager.bot_count}")

    def _append_log(self, msg: str):
        self._flog.info(msg)
        ts = time.strftime("%H:%M:%S")
        self._log.append(f"[{ts}] {msg}")

    def _refresh_graph(self):
        self._graph.update()

    def _log_tps(self):
        if not self._connection.is_connected or not self._tps_samples:
            return
        tps = sum(self._tps_samples) / len(self._tps_samples)
        snap = self._prev_snap
        if snap:
            work_ms = snap.pool_reset_us / 1000.0
            ent = snap.total_entity_count
            players = sum(lv.player_count for lv in snap.levels)
            chunks = sum(lv.loaded_chunks for lv in snap.levels if lv.loaded_chunks > 0)
            self._append_log(
                f"TPS: {tps:.1f} | MSPT: {work_ms:.1f}ms | "
                f"Players: {players} | Entities: {ent} | Chunks: {chunks} | "
                f"Memory: {snap.memory_used_mb:.0f}MB"
            )
        else:
            self._append_log(f"TPS: {tps:.1f}")

    def closeEvent(self, event):
        self._bot_manager.remove_all()
        self._connection.disconnect()
        super().closeEvent(event)
