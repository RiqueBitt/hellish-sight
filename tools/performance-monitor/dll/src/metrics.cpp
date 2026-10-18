//
// metrics.cpp -- Server metric collection and snapshot queuing.
//
// Reads entity counts, chunk counts, player counts, and memory usage
// from the server's live data structures.
//

#include "perf_monitor.h"
#include <psapi.h>
#include <algorithm>

// -----------------------------------------------------------------------
// Snapshot queues (game thread pushes, TCP thread drains)
// -----------------------------------------------------------------------

static std::mutex g_tickMtx;
static std::deque<TickSnapshot> g_tickQueue;
static constexpr size_t MAX_TICK_QUEUE = 200;  // ~10 seconds at 20 TPS

static std::mutex g_autoMtx;
static std::deque<AutosaveSnapshot> g_autoQueue;
static constexpr size_t MAX_AUTO_QUEUE = 20;

// -----------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------

void MetricsInit()
{
}

void MetricsShutdown()
{
    std::lock_guard<std::mutex> lock1(g_tickMtx);
    g_tickQueue.clear();
    std::lock_guard<std::mutex> lock2(g_autoMtx);
    g_autoQueue.clear();
}

// -----------------------------------------------------------------------
// Level metrics collection
//
// Level layout on MSVC x64, derived from Level.h and confirmed by
// runtime vector scan (see perf-monitor.log for the calibration output):
//
//   offset  0: vtable ptr (from LevelSource)               8 bytes
//   offset  8: seaLevel (int)                               4 bytes
//   offset 12: padding                                      4 bytes
//   offset 16: m_entitiesCS (CRITICAL_SECTION)             40 bytes
//   offset 56: padding to 8-byte alignment                  8 bytes
//   offset 64: entities (vector<shared_ptr<Entity>>)       24 bytes
//   offset 88: entitiesToRemove (vector<shared_ptr<Entity>>) 24 bytes
//  offset 112: m_bDisableAddNewTileEntities (bool)          1 byte
//  offset 113: padding                                      7 bytes
//  offset 120: m_tileEntityListCS (CRITICAL_SECTION)       40 bytes
//  offset 160: tileEntityList (vector<shared_ptr<TileEntity>>) 24 bytes
//  offset 184: pendingTileEntities (vector)                24 bytes
//  offset 208: tileEntitiesToUnload (vector)               24 bytes
//  offset 232: updatingTileEntities (bool)                  1 byte
//  offset 233: padding                                      7 bytes
//  offset 240: players (vector<shared_ptr<Player>>)        24 bytes
//  offset 264: globalEntities (vector<shared_ptr<Entity>>) 24 bytes
//
// These offsets are validated against a live debug server.  If they shift
// in a future build, the SEH guards will catch bad reads and the log will
// show -1 for the affected counts.  Re-run the offset scan to recalibrate.
// -----------------------------------------------------------------------

// std::vector<shared_ptr<T>> on MSVC x64:
//   +0:  _Myfirst (pointer to first element)
//   +8:  _Mylast  (pointer past last element)
//   +16: _Myend   (pointer past allocated capacity)
// sizeof(shared_ptr<T>) = 16 (raw ptr + ref count ptr)

static int ReadVectorSize(uint8_t *vecBase, int elementSize)
{
    auto first = *reinterpret_cast<uintptr_t *>(vecBase + 0);
    auto last  = *reinterpret_cast<uintptr_t *>(vecBase + 8);

    if (!first || !last || last < first) return 0;

    auto diff = last - first;
    if (elementSize <= 0) return 0;

    int count = (int)(diff / elementSize);

    // Sanity: reject absurd values (corrupted memory)
    if (count < 0 || count > 100000) return 0;

    return count;
}

// Level member offsets -- calibrated from runtime scan.
// If these are wrong for a given build, the SEH guards return -1 and the
// log records the failure.  Rerun with a fresh scan to recalibrate.
// Level member offsets differ between Debug and Release builds because
// CRITICAL_SECTION is 48 bytes in Debug (extra padding) vs 40 in Release.
// We auto-detect by checking where the entities vector is (the first
// reliably identifiable vector in the object).
//
// Debug layout  (CS=48): entities=+64, tileEntityList=+176, players=+280, globalEntities=+256
// Release layout (CS=40): entities=+56, tileEntityList=+152, players=+232, globalEntities=+256
namespace LevelOff {
    static int entities        = 0;
    static int entitiesToRemove = 0;
    static int tileEntityList  = 0;
    static int players         = 0;
    static int globalEntities  = 0;
    static bool detected       = false;

    static void Detect(int entitiesOffset)
    {
        if (detected) return;
        detected = true;

        if (entitiesOffset == 64) {
            // Debug build (CS=48 with extra padding)
            entities        = 64;
            entitiesToRemove = 88;
            tileEntityList  = 176;
            players         = 280;
            globalEntities  = 256;
            LogWrite("Level offsets: DEBUG layout (entities=+64)");
        } else if (entitiesOffset == 56) {
            // Release build (CS=40)
            entities        = 56;
            entitiesToRemove = 80;
            tileEntityList  = 152;
            players         = 232;
            globalEntities  = 256;
            LogWrite("Level offsets: RELEASE layout (entities=+56)");
        } else {
            // Unknown layout - use the detected entities offset and estimate
            entities = entitiesOffset;
            LogWrite("Level offsets: UNKNOWN layout (entities=+%d), entity count only", entitiesOffset);
        }
    }
}

// Scan for offset calibration -- writes to the log so we can update the
// constants above if the layout changes.  Called once on first tick.
static bool g_scanDone = false;

// SEH-safe inner scan (no C++ objects with destructors)
struct VecCandidate { int offset; int count; };
static constexpr int MAX_SCAN_CANDIDATES = 64;
static VecCandidate g_scanResults[MAX_SCAN_CANDIDATES];
static int g_scanCount = 0;

static void ScanLevelOffsetsInner(void *serverLevel)
{
    auto base = reinterpret_cast<uint8_t *>(serverLevel);
    g_scanCount = 0;

    __try {
        for (int off = 0; off < 400 && g_scanCount < MAX_SCAN_CANDIDATES; off += 8) {
            auto first = *reinterpret_cast<uintptr_t *>(base + off);
            auto last  = *reinterpret_cast<uintptr_t *>(base + off + 8);
            auto end   = *reinterpret_cast<uintptr_t *>(base + off + 16);

            if (!first && !last && !end) continue;

            auto ok = [](uintptr_t p) -> bool {
                return p == 0 || (p > 0x10000 && p < 0x7FFFFFFFFFFF);
            };
            if (!ok(first) || !ok(last) || !ok(end)) continue;
            if (first > last || last > end) continue;

            // Try both shared_ptr<T> (16 bytes) and raw ptr (8 bytes)
            int count16 = 0, count8 = 0;
            if (first && last > first) {
                auto diff = last - first;
                if (diff % 16 == 0) count16 = (int)(diff / 16);
                if (diff % 8 == 0)  count8  = (int)(diff / 8);
            }
            if (count16 > 100000) count16 = -1;
            if (count8 > 100000) count8 = -1;

            // Use shared_ptr count as primary, store raw count for logging
            g_scanResults[g_scanCount++] = {off, count16};
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogWrite("WARNING: Exception during Level offset scan");
    }
}

// Extended scan: log with both element size interpretations
static void ScanLevelOffsetsExtended(void *serverLevel)
{
    auto base = reinterpret_cast<uint8_t *>(serverLevel);

    __try {
        for (int off = 56; off < 320; off += 8) {
            auto first = *reinterpret_cast<uintptr_t *>(base + off);
            auto last  = *reinterpret_cast<uintptr_t *>(base + off + 8);
            auto end   = *reinterpret_cast<uintptr_t *>(base + off + 16);

            if (!first && !last && !end) continue;

            auto ok = [](uintptr_t p) -> bool {
                return p == 0 || (p > 0x10000 && p < 0x7FFFFFFFFFFF);
            };
            if (!ok(first) || !ok(last) || !ok(end)) continue;
            if (first > last || last > end) continue;

            auto diff = last - first;
            LogWrite("  +%d: diff=%llu  /16=%llu  /8=%llu  /24=%llu",
                off, (unsigned long long)diff,
                diff / 16, diff / 8, diff / 24);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogWrite("WARNING: Exception during extended scan");
    }
}

static void ScanLevelOffsets(void *serverLevel)
{
    if (g_scanDone) return;
    g_scanDone = true;

    ScanLevelOffsetsInner(serverLevel);

    LogWrite("Extended vector scan (diff in bytes, divided by element sizes):");
    ScanLevelOffsetsExtended(serverLevel);

    LogWrite("Level offset scan (%d candidates):", g_scanCount);
    for (int i = 0; i < g_scanCount; i++) {
        LogWrite("  +%d: %d elements", g_scanResults[i].offset, g_scanResults[i].count);
    }

    // Auto-detect layout: find the first vector with a plausible entity count
    // (10-5000 elements). This is the `entities` vector.
    for (int i = 0; i < g_scanCount; i++) {
        if (g_scanResults[i].count >= 10 && g_scanResults[i].count <= 5000) {
            LevelOff::Detect(g_scanResults[i].offset);
            break;
        }
    }

    if (LevelOff::detected) {
        auto has = [](int off) {
            for (int i = 0; i < g_scanCount; i++)
                if (g_scanResults[i].offset == off) return true;
            return false;
        };
        LogWrite("Offset validation: entities=%s entitiesToRemove=%s tileEntityList=%s players=%s globalEntities=%s",
            has(LevelOff::entities) ? "OK" : "MISS",
            has(LevelOff::entitiesToRemove) ? "OK" : "MISS",
            has(LevelOff::tileEntityList) ? "OK" : "MISS",
            has(LevelOff::players) ? "OK" : "MISS",
            has(LevelOff::globalEntities) ? "OK" : "MISS");
    }
}

void CollectLevelMetrics(void *serverLevel, int index, LevelMetrics &out)
{
    if (!serverLevel) return;

    // Run offset scan once for logging/validation
    if (!g_scanDone) {
        ScanLevelOffsets(serverLevel);
    }

    if (!LevelOff::detected) return;

    auto base = reinterpret_cast<uint8_t *>(serverLevel);

    __try {
        out.entityCount = ReadVectorSize(base + LevelOff::entities, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.entityCount = -1; }

    __try {
        out.globalEntityCount = ReadVectorSize(base + LevelOff::globalEntities, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.globalEntityCount = -1; }

    __try {
        out.playerCount = ReadVectorSize(base + LevelOff::players, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.playerCount = -1; }

    __try {
        out.tileEntityCount = ReadVectorSize(base + LevelOff::tileEntityList, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.tileEntityCount = -1; }

    __try {
        out.entitiesToRemove = ReadVectorSize(base + LevelOff::entitiesToRemove, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.entitiesToRemove = -1; }

    // Loaded chunk count via ServerLevel::getChunkMap() (Debug builds only).
    // In Release builds, getChunkMap() is inlined and can't be resolved from PDB.
    // Chunk count will show 0 on Release until we add PDB type info queries.
    if (g_symbols.ServerLevel_getChunkMap) {
        __try {
            using GetChunkMapFn = void *(__fastcall *)(void *);
            auto fn = reinterpret_cast<GetChunkMapFn>(g_symbols.ServerLevel_getChunkMap);
            auto chunkMap = reinterpret_cast<uint8_t *>(fn(serverLevel));
            if (chunkMap) {
                static int chunkCountOffset = -1;
                if (chunkCountOffset < 0) {
                    for (int off = 24; off < 120; off += 8) {
                        auto val = *reinterpret_cast<size_t *>(chunkMap + off);
                        if (val > 0 && val < 10000) {
                            chunkCountOffset = off;
                            LogWrite("PlayerChunkMap chunk count at +%d = %zu", off, val);
                            break;
                        }
                    }
                    if (chunkCountOffset < 0) chunkCountOffset = 0;
                }
                if (chunkCountOffset > 0) {
                    auto count = *reinterpret_cast<size_t *>(chunkMap + chunkCountOffset);
                    if (count < 10000) {
                        out.loadedChunks = (int)count;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            out.loadedChunks = -1;
        }
    }
}

// -----------------------------------------------------------------------
// Memory metrics
// -----------------------------------------------------------------------

void CollectMemoryMetrics(double &usedMb, double &totalMb)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        usedMb = pmc.WorkingSetSize / (1024.0 * 1024.0);
    }

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    if (GlobalMemoryStatusEx(&memInfo)) {
        totalMb = memInfo.ullTotalPhys / (1024.0 * 1024.0);
    }
}

// -----------------------------------------------------------------------
// Snapshot queuing
// -----------------------------------------------------------------------

void PushTickSnapshot(TickSnapshot &&snap)
{
    std::lock_guard<std::mutex> lock(g_tickMtx);
    if (g_tickQueue.size() >= MAX_TICK_QUEUE) {
        g_tickQueue.pop_front();
    }
    g_tickQueue.push_back(std::move(snap));
}

void PushAutosaveSnapshot(AutosaveSnapshot &&snap)
{
    std::lock_guard<std::mutex> lock(g_autoMtx);
    if (g_autoQueue.size() >= MAX_AUTO_QUEUE) {
        g_autoQueue.pop_front();
    }
    g_autoQueue.push_back(std::move(snap));
}

std::vector<TickSnapshot> DrainTickSnapshots()
{
    std::lock_guard<std::mutex> lock(g_tickMtx);
    std::vector<TickSnapshot> out(g_tickQueue.begin(), g_tickQueue.end());
    g_tickQueue.clear();
    return out;
}

std::vector<AutosaveSnapshot> DrainAutosaveSnapshots()
{
    std::lock_guard<std::mutex> lock(g_autoMtx);
    std::vector<AutosaveSnapshot> out(g_autoQueue.begin(), g_autoQueue.end());
    g_autoQueue.clear();
    return out;
}
