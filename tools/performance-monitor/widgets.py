"""
Custom widgets for the performance monitor GUI.

  - TickBreakdownBar: stacked horizontal bar showing per-phase timing
  - TPSGraph: real-time TPS line chart (adapted from server-monitor)
  - EntityChunkPanel: per-dimension entity/chunk counts
  - MemoryBar: process memory usage bar
  - LagSpikePanel: recent lag spikes with root cause
"""

import time
from collections import deque

from PySide6.QtCore import Qt
from PySide6.QtGui import QPainter, QColor, QPen, QFont, QBrush, QLinearGradient
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QGroupBox, QFrame, QScrollArea, QSizePolicy,
)

from models import TickSnapshot, AutosaveSnapshot

# -----------------------------------------------------------------------
# Color palette for tick phases
# -----------------------------------------------------------------------

PHASE_COLORS = {
    "pool_reset":      QColor(100, 100, 120),
    "level_tick_0":    QColor(70, 130, 220),    # Overworld level tick
    "entity_tick_0":   QColor(230, 120, 50),    # Overworld entity tick
    "tracker_tick_0":  QColor(200, 200, 70),    # Overworld tracker
    "level_tick_1":    QColor(150, 60, 180),    # Nether level tick
    "entity_tick_1":   QColor(220, 60, 80),     # Nether entity tick
    "tracker_tick_1":  QColor(180, 180, 50),    # Nether tracker
    "level_tick_2":    QColor(50, 180, 170),    # End level tick
    "entity_tick_2":   QColor(220, 100, 140),   # End entity tick
    "tracker_tick_2":  QColor(160, 160, 50),    # End tracker
    "players_tick":    QColor(80, 200, 120),
    "connection_tick": QColor(80, 180, 220),
    "console_tick":    QColor(120, 120, 140),
    "other":           QColor(60, 60, 80),
}

DIMENSION_NAMES = {0: "Overworld", -1: "Nether", 1: "End"}


# -----------------------------------------------------------------------
# Tick Breakdown Bar
# -----------------------------------------------------------------------

class TickBreakdownBar(QWidget):
    """Horizontal stacked bar showing which phase consumed time."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(40)
        self.setMaximumHeight(50)
        self._segments: list[tuple[str, int, QColor]] = []  # (label, us, color)
        self._total_us: int = 0
        self._bg = QColor(24, 24, 32)

    def update_from_snapshot(self, snap: TickSnapshot):
        segments = []

        if snap.pool_reset_us > 0:
            segments.append(("Pool Reset", snap.pool_reset_us, PHASE_COLORS["pool_reset"]))

        for i, lv in enumerate(snap.levels):
            suffix = str(i)
            dim_name = DIMENSION_NAMES.get(lv.dimension, f"Dim{lv.dimension}")

            if lv.level_tick_us > 0:
                segments.append(
                    (f"{dim_name} Weather", lv.level_tick_us,
                     PHASE_COLORS.get(f"level_tick_{suffix}", PHASE_COLORS["other"])))

            if not lv.entity_tick_skipped and lv.entity_tick_us > 0:
                segments.append(
                    (f"{dim_name} Entities", lv.entity_tick_us,
                     PHASE_COLORS.get(f"entity_tick_{suffix}", PHASE_COLORS["other"])))

            if lv.tracker_tick_us > 0:
                segments.append(
                    (f"{dim_name} Tracker", lv.tracker_tick_us,
                     PHASE_COLORS.get(f"tracker_tick_{suffix}", PHASE_COLORS["other"])))

        if snap.players_tick_us > 0:
            segments.append(("Players", snap.players_tick_us, PHASE_COLORS["players_tick"]))

        if snap.connection_tick_us > 0:
            segments.append(("Network", snap.connection_tick_us, PHASE_COLORS["connection_tick"]))

        if snap.console_tick_us > 0:
            segments.append(("Console", snap.console_tick_us, PHASE_COLORS["console_tick"]))

        # "Other" time = total - sum of known phases
        accounted = sum(s[1] for s in segments)
        other = snap.total_us - accounted
        if other > 0:
            segments.append(("Other", other, PHASE_COLORS["other"]))

        self._segments = segments
        self._total_us = snap.total_us
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        w, h = self.width(), self.height()
        painter.fillRect(0, 0, w, h, self._bg)

        if self._total_us <= 0 or not self._segments:
            painter.setPen(QPen(QColor(140, 140, 160), 1))
            painter.setFont(QFont("Consolas", 10))
            painter.drawText(0, 0, w, h, Qt.AlignCenter, "No data")
            painter.end()
            return

        # Draw the 50ms budget line
        budget_x = min(w * (50000 / max(self._total_us, 50000)), w)

        # Draw segments
        x = 0.0
        painter.setFont(QFont("Consolas", 8))
        for label, us, color in self._segments:
            seg_w = (us / self._total_us) * w
            if seg_w < 1:
                continue

            painter.fillRect(int(x), 2, int(seg_w), h - 4, color)

            # Label if wide enough
            if seg_w > 60:
                painter.setPen(QPen(QColor(255, 255, 255), 1))
                text = f"{label} {us / 1000:.1f}ms"
                painter.drawText(int(x) + 4, 2, int(seg_w) - 8, h - 4,
                                 Qt.AlignVCenter | Qt.AlignLeft, text)

            x += seg_w

        # Budget line (50ms = 1 tick at 20 TPS)
        if self._total_us > 50000:
            bx = int(w * 50000 / self._total_us)
            painter.setPen(QPen(QColor(255, 80, 80, 180), 2, Qt.DashLine))
            painter.drawLine(bx, 0, bx, h)

        # Total label on the right
        painter.setPen(QPen(QColor(200, 200, 220), 1))
        painter.setFont(QFont("Consolas", 9))
        total_text = f"{self._total_us / 1000:.1f}ms"
        painter.drawText(w - 80, 0, 75, h, Qt.AlignVCenter | Qt.AlignRight, total_text)

        painter.end()


# -----------------------------------------------------------------------
# TPS Graph
# -----------------------------------------------------------------------

class TPSGraph(QWidget):
    """Real-time TPS line chart."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(200)
        self.setMinimumWidth(400)
        self._data: deque[tuple[float, float]] = deque(maxlen=6000)  # 5 min at 20 TPS
        self._bg = QColor(24, 24, 32)
        self._grid = QColor(48, 48, 64)
        self._line_good = QColor(80, 200, 120)
        self._line_warn = QColor(255, 200, 60)
        self._line_bad = QColor(255, 80, 80)

    def add_point(self, timestamp: float, tps: float):
        self._data.append((timestamp, tps))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        w, h = self.width(), self.height()
        ml, mb, mt, mr = 50, 30, 20, 20

        painter.fillRect(0, 0, w, h, self._bg)

        gw = w - ml - mr
        gh = h - mt - mb
        if gw < 10 or gh < 10:
            painter.end()
            return

        # Grid
        painter.setPen(QPen(self._grid, 1))
        painter.setFont(QFont("Consolas", 8))
        for tps_val in [0, 5, 10, 15, 20]:
            y = mt + gh - (tps_val / 20.0) * gh
            painter.drawLine(ml, int(y), w - mr, int(y))
            painter.setPen(QPen(QColor(140, 140, 160), 1))
            painter.drawText(5, int(y) + 4, f"{tps_val}")
            painter.setPen(QPen(self._grid, 1))

        # 15 TPS warning line
        painter.setPen(QPen(QColor(255, 200, 60, 80), 1, Qt.DashLine))
        y15 = mt + gh - (15.0 / 20.0) * gh
        painter.drawLine(ml, int(y15), w - mr, int(y15))

        if len(self._data) < 2:
            painter.setPen(QPen(QColor(140, 140, 160), 1))
            painter.setFont(QFont("Consolas", 12))
            painter.drawText(ml, mt, gw, gh, Qt.AlignCenter, "Waiting for data...")
            painter.end()
            return

        data_list = list(self._data)
        now = data_list[-1][0]
        time_window = 300.0

        points = []
        for t, tps in data_list:
            age = now - t
            if age > time_window:
                continue
            x = ml + (1.0 - age / time_window) * gw
            y = mt + gh - (min(tps, 20.0) / 20.0) * gh
            points.append((x, y, tps))

        for i in range(1, len(points)):
            x1, y1, t1 = points[i - 1]
            x2, y2, t2 = points[i]
            avg = (t1 + t2) / 2

            if avg >= 18:
                color = self._line_good
            elif avg >= 15:
                color = self._line_warn
            else:
                color = self._line_bad

            painter.setPen(QPen(color, 2))
            painter.drawLine(int(x1), int(y1), int(x2), int(y2))

        # Time axis
        painter.setPen(QPen(QColor(140, 140, 160), 1))
        painter.setFont(QFont("Consolas", 8))
        for sec in [0, 60, 120, 180, 240, 300]:
            x = ml + (1.0 - sec / time_window) * gw
            if x >= ml:
                label = "now" if sec == 0 else f"-{sec}s"
                painter.drawText(int(x) - 15, h - 8, label)

        painter.end()


# -----------------------------------------------------------------------
# Stat Card (reused from server-monitor)
# -----------------------------------------------------------------------

class StatCard(QFrame):
    """Compact stat display card."""

    def __init__(self, title: str, parent=None):
        super().__init__(parent)
        self.setFrameStyle(QFrame.Box | QFrame.Raised)
        self.setStyleSheet(
            "StatCard { background: #1a1a24; border: 1px solid #333348; border-radius: 6px; }"
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)

        self._title = QLabel(title)
        self._title.setStyleSheet("color: #8888aa; font-size: 11px;")
        self._title.setAlignment(Qt.AlignCenter)
        layout.addWidget(self._title)

        self._value = QLabel("--")
        self._value.setStyleSheet("color: #ffffff; font-size: 24px; font-weight: bold;")
        self._value.setAlignment(Qt.AlignCenter)
        layout.addWidget(self._value)

    def set_value(self, text: str, color: str = "#ffffff"):
        self._value.setText(text)
        self._value.setStyleSheet(
            f"color: {color}; font-size: 24px; font-weight: bold;"
        )


# -----------------------------------------------------------------------
# Entity / Chunk Panel
# -----------------------------------------------------------------------

class EntityChunkPanel(QGroupBox):
    """Per-dimension entity and chunk counts."""

    def __init__(self, parent=None):
        super().__init__("World Stats", parent)
        self.setStyleSheet(
            "QGroupBox { color: #aaaacc; border: 1px solid #333348; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
            "QGroupBox::title { subcontrol-position: top left; padding: 2px 8px; }"
        )

        layout = QGridLayout(self)
        layout.setSpacing(4)

        # Headers
        headers = ["", "Overworld", "Nether", "End"]
        for col, h in enumerate(headers):
            lbl = QLabel(h)
            lbl.setStyleSheet("color: #8888aa; font-size: 10px; font-weight: bold;")
            lbl.setAlignment(Qt.AlignCenter)
            layout.addWidget(lbl, 0, col)

        # Rows
        row_labels = ["Entities", "Global", "Players", "Chunks", "Tile Ent.", "To Remove"]
        self._cells: dict[tuple[str, int], QLabel] = {}

        for row, label in enumerate(row_labels, start=1):
            lbl = QLabel(label)
            lbl.setStyleSheet("color: #8888aa; font-size: 10px;")
            layout.addWidget(lbl, row, 0)

            for col in range(3):
                cell = QLabel("--")
                cell.setStyleSheet("color: #ccccdd; font-size: 11px; font-family: Consolas;")
                cell.setAlignment(Qt.AlignCenter)
                layout.addWidget(cell, row, col + 1)
                self._cells[(label, col)] = cell

    def update_from_snapshot(self, snap: TickSnapshot):
        dim_order = [0, -1, 1]  # Overworld, Nether, End
        dim_to_col = {0: 0, -1: 1, 1: 2}

        level_map = {lv.dimension: lv for lv in snap.levels}

        for dim in dim_order:
            col = dim_to_col[dim]
            lv = level_map.get(dim)

            if lv:
                self._set_cell("Entities", col, str(lv.entity_count))
                self._set_cell("Global", col, str(lv.global_entity_count))
                self._set_cell("Players", col, str(lv.player_count))
                self._set_cell("Chunks", col, str(lv.loaded_chunks))
                self._set_cell("Tile Ent.", col, str(lv.tile_entity_count))
                self._set_cell("To Remove", col, str(lv.entities_to_remove))
            else:
                for label in ["Entities", "Global", "Players", "Chunks", "Tile Ent.", "To Remove"]:
                    self._set_cell(label, col, "--")

    def _set_cell(self, label: str, col: int, text: str):
        cell = self._cells.get((label, col))
        if cell:
            cell.setText(text)


# -----------------------------------------------------------------------
# Memory Bar
# -----------------------------------------------------------------------

class MemoryBar(QWidget):
    """Simple horizontal bar showing memory usage."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(24)
        self.setMaximumHeight(30)
        self._used_mb = 0.0
        self._total_mb = 1.0
        self._bg = QColor(24, 24, 32)

    def update_memory(self, used_mb: float, total_mb: float):
        self._used_mb = used_mb
        self._total_mb = max(total_mb, 1.0)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        painter.fillRect(0, 0, w, h, self._bg)

        ratio = min(self._used_mb / self._total_mb, 1.0)
        bar_w = int(w * ratio)

        if ratio < 0.6:
            color = QColor(80, 200, 120)
        elif ratio < 0.8:
            color = QColor(255, 200, 60)
        else:
            color = QColor(255, 80, 80)

        painter.fillRect(0, 2, bar_w, h - 4, color)

        painter.setPen(QPen(QColor(220, 220, 240), 1))
        painter.setFont(QFont("Consolas", 9))
        text = f"Memory: {self._used_mb:.0f} / {self._total_mb:.0f} MB ({ratio * 100:.0f}%)"
        painter.drawText(8, 0, w - 16, h, Qt.AlignVCenter, text)

        painter.end()


# -----------------------------------------------------------------------
# Lag Spike Panel
# -----------------------------------------------------------------------

class LagSpikePanel(QGroupBox):
    """Shows recent lag spikes with the exact causal phase."""

    def __init__(self, parent=None):
        super().__init__("Lag Spikes", parent)
        self.setStyleSheet(
            "QGroupBox { color: #aaaacc; border: 1px solid #333348; "
            "border-radius: 6px; margin-top: 8px; padding-top: 14px; }"
            "QGroupBox::title { subcontrol-position: top left; padding: 2px 8px; }"
        )

        self._layout = QVBoxLayout(self)
        self._layout.setSpacing(2)

        self._entries: deque[QLabel] = deque(maxlen=50)
        self._placeholder = QLabel("No lag spikes detected")
        self._placeholder.setStyleSheet("color: #50c878; font-size: 11px;")
        self._placeholder.setAlignment(Qt.AlignCenter)
        self._layout.addWidget(self._placeholder)
        self._layout.addStretch()

    def add_spike(self, snap: TickSnapshot):
        """Legacy - called without reason."""
        self.add_spike_with_reason(snap, "Unknown")

    def add_spike_with_reason(self, snap: TickSnapshot, reason: str):
        """Add a spike entry with a classified reason."""
        if self._placeholder.isVisible():
            self._placeholder.setVisible(False)

        interval_ms = snap.total_us / 1000.0
        if interval_ms >= 1000:
            dur_str = f"{interval_ms / 1000:.1f}s"
        else:
            dur_str = f"{interval_ms:.0f}ms"

        ts = time.strftime("%H:%M:%S")
        text = f"[{ts}] {dur_str} - {reason}"

        # Color: red for major (>500ms), orange for moderate
        if interval_ms >= 500:
            color = "#ff5050"
        else:
            color = "#ffc83d"

        lbl = QLabel(text)
        lbl.setStyleSheet(f"color: {color}; font-size: 10px; font-family: Consolas;")
        lbl.setWordWrap(True)

        # Insert at top
        self._layout.insertWidget(0, lbl)
        self._entries.appendleft(lbl)

        # Remove old entries if over limit
        while len(self._entries) > 50:
            old = self._entries.pop()
            self._layout.removeWidget(old)
            old.deleteLater()

    def add_autosave(self, snap: AutosaveSnapshot):
        """Add an autosave event."""
        if self._placeholder.isVisible():
            self._placeholder.setVisible(False)

        # Find dominant sub-phase
        parts = [
            ("Players", snap.players_us),
            ("Levels", snap.levels_us),
            ("Rules", snap.rules_us),
            ("Flush", snap.flush_us),
        ]
        cause, cause_us = max(parts, key=lambda x: x[1])

        ts = time.strftime("%H:%M:%S")
        text = (f"[{ts}] AUTOSAVE {snap.total_ms:.0f}ms "
                f"- {cause} ({cause_us / 1000:.1f}ms)")

        lbl = QLabel(text)
        lbl.setStyleSheet("color: #ffc83d; font-size: 10px; font-family: Consolas;")

        self._layout.insertWidget(0, lbl)
        self._entries.appendleft(lbl)

        while len(self._entries) > 50:
            old = self._entries.pop()
            self._layout.removeWidget(old)
            old.deleteLater()
