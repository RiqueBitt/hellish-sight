//
// json_writer.cpp -- Hand-rolled JSON serialization for metric snapshots.
//
// No external JSON library dependency.  The schema is fixed and simple
// enough that snprintf is cleaner than pulling in nlohmann/json.
//

#include "perf_monitor.h"
#include <cstdio>

// Stack buffer size for JSON assembly.  A typical tick snapshot is
// 500-1000 bytes.  Autosave snapshots are smaller.
static constexpr int BUF_SIZE = 4096;

std::string SerializeTick(const TickSnapshot &s)
{
    char buf[BUF_SIZE];
    int pos = 0;

    pos += snprintf(buf + pos, BUF_SIZE - pos,
        "{\"type\":\"tick\","
        "\"tick\":%d,"
        "\"timestamp_ms\":%lld,"
        "\"total_us\":%lld,"
        "\"phases\":{",
        s.tick,
        (long long)s.timestampMs,
        (long long)s.totalUs);

    pos += snprintf(buf + pos, BUF_SIZE - pos,
        "\"pool_reset_us\":%lld,",
        (long long)s.poolResetUs);

    // Levels array
    pos += snprintf(buf + pos, BUF_SIZE - pos, "\"levels\":[");
    for (size_t i = 0; i < s.levels.size(); i++) {
        const auto &lm = s.levels[i];
        if (i > 0) buf[pos++] = ',';

        pos += snprintf(buf + pos, BUF_SIZE - pos,
            "{"
            "\"dimension\":%d,"
            "\"level_tick_us\":%lld,"
            "\"entity_tick_us\":%lld,"
            "\"entity_tick_skipped\":%s,"
            "\"tracker_tick_us\":%lld,"
            "\"entity_count\":%d,"
            "\"global_entity_count\":%d,"
            "\"player_count\":%d,"
            "\"loaded_chunks\":%d,"
            "\"entities_to_remove\":%d,"
            "\"tile_entity_count\":%d"
            "}",
            lm.dimension,
            (long long)lm.levelTickUs,
            (long long)lm.entityTickUs,
            lm.entityTickSkipped ? "true" : "false",
            (long long)lm.trackerTickUs,
            lm.entityCount,
            lm.globalEntityCount,
            lm.playerCount,
            lm.loadedChunks,
            lm.entitiesToRemove,
            lm.tileEntityCount);
    }
    pos += snprintf(buf + pos, BUF_SIZE - pos, "],");

    pos += snprintf(buf + pos, BUF_SIZE - pos,
        "\"players_tick_us\":%lld,"
        "\"connection_tick_us\":%lld,"
        "\"console_tick_us\":%lld"
        "},",
        (long long)s.playersTickUs,
        (long long)s.connectionTickUs,
        (long long)s.consoleTickUs);

    pos += snprintf(buf + pos, BUF_SIZE - pos,
        "\"server\":{"
        "\"total_players\":%d,"
        "\"memory_used_mb\":%.1f,"
        "\"memory_total_mb\":%.1f,"
        "\"is_autosaving\":%s,"
        "\"is_paused\":%s,"
        "\"tps_target\":20"
        "}}",
        s.totalPlayers,
        s.memoryUsedMb,
        s.memoryTotalMb,
        s.isAutosaving ? "true" : "false",
        s.isPaused ? "true" : "false");

    return std::string(buf, pos);
}

std::string SerializeAutosave(const AutosaveSnapshot &s)
{
    char buf[BUF_SIZE];
    int pos = snprintf(buf, BUF_SIZE,
        "{\"type\":\"autosave\","
        "\"tick\":%d,"
        "\"timestamp_ms\":%lld,"
        "\"total_us\":%lld,"
        "\"breakdown\":{"
        "\"players_us\":%lld,"
        "\"levels_us\":%lld,"
        "\"rules_us\":%lld,"
        "\"flush_us\":%lld"
        "}}",
        s.tick,
        (long long)s.timestampMs,
        (long long)s.totalUs,
        (long long)s.playersUs,
        (long long)s.levelsUs,
        (long long)s.rulesUs,
        (long long)s.flushUs);

    return std::string(buf, pos);
}

std::string SerializeHelloAck(int levelCount)
{
    char buf[256];
    int pos = snprintf(buf, 256,
        "{\"type\":\"hello_ack\","
        "\"version\":1,"
        "\"server_tps_target\":20,"
        "\"level_count\":%d,"
        "\"dimensions\":[0,-1,1]}",
        levelCount);

    return std::string(buf, pos);
}
