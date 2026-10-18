"""
Data models for performance monitor snapshots.

All dataclasses are frozen (immutable) per project coding style.
Parsing functions validate at the boundary.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class LevelSnapshot:
    dimension: int
    level_tick_us: int
    entity_tick_us: int
    entity_tick_skipped: bool
    tracker_tick_us: int
    entity_count: int
    global_entity_count: int
    player_count: int
    loaded_chunks: int
    entities_to_remove: int
    tile_entity_count: int


@dataclass(frozen=True)
class TickSnapshot:
    tick: int
    timestamp_ms: int
    total_us: int

    pool_reset_us: int
    levels: tuple[LevelSnapshot, ...]
    players_tick_us: int
    connection_tick_us: int
    console_tick_us: int

    total_players: int
    memory_used_mb: float
    memory_total_mb: float
    is_autosaving: bool
    is_paused: bool

    @property
    def total_ms(self) -> float:
        return self.total_us / 1000.0

    @property
    def tps(self) -> float:
        """Estimated TPS: 20 if tick fits in budget, lower if it overruns."""
        if self.total_us <= 50000:
            return 20.0
        # Tick took longer than 50ms -- server can't keep up
        return min(1_000_000 / self.total_us, 20.0)

    @property
    def total_entity_count(self) -> int:
        return sum(lv.entity_count for lv in self.levels)


@dataclass(frozen=True)
class AutosaveSnapshot:
    tick: int
    timestamp_ms: int
    total_us: int
    players_us: int
    levels_us: int
    rules_us: int
    flush_us: int

    @property
    def total_ms(self) -> float:
        return self.total_us / 1000.0


def parse_level(data: dict) -> LevelSnapshot:
    return LevelSnapshot(
        dimension=data.get("dimension", 0),
        level_tick_us=data.get("level_tick_us", 0),
        entity_tick_us=data.get("entity_tick_us", 0),
        entity_tick_skipped=data.get("entity_tick_skipped", False),
        tracker_tick_us=data.get("tracker_tick_us", 0),
        entity_count=data.get("entity_count", 0),
        global_entity_count=data.get("global_entity_count", 0),
        player_count=data.get("player_count", 0),
        loaded_chunks=data.get("loaded_chunks", 0),
        entities_to_remove=data.get("entities_to_remove", 0),
        tile_entity_count=data.get("tile_entity_count", 0),
    )


def parse_tick(data: dict) -> TickSnapshot:
    phases = data.get("phases", {})
    levels_raw = phases.get("levels", [])
    return TickSnapshot(
        tick=data.get("tick", 0),
        timestamp_ms=data.get("timestamp_ms", 0),
        total_us=data.get("total_us", 0),
        pool_reset_us=phases.get("pool_reset_us", 0),
        levels=tuple(parse_level(lv) for lv in levels_raw),
        players_tick_us=phases.get("players_tick_us", 0),
        connection_tick_us=phases.get("connection_tick_us", 0),
        console_tick_us=phases.get("console_tick_us", 0),
        total_players=data.get("server", {}).get("total_players", 0),
        memory_used_mb=data.get("server", {}).get("memory_used_mb", 0.0),
        memory_total_mb=data.get("server", {}).get("memory_total_mb", 0.0),
        is_autosaving=data.get("server", {}).get("is_autosaving", False),
        is_paused=data.get("server", {}).get("is_paused", False),
    )


def parse_autosave(data: dict) -> AutosaveSnapshot:
    breakdown = data.get("breakdown", {})
    return AutosaveSnapshot(
        tick=data.get("tick", 0),
        timestamp_ms=data.get("timestamp_ms", 0),
        total_us=data.get("total_us", 0),
        players_us=breakdown.get("players_us", 0),
        levels_us=breakdown.get("levels_us", 0),
        rules_us=breakdown.get("rules_us", 0),
        flush_us=breakdown.get("flush_us", 0),
    )
