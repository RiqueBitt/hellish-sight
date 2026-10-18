#pragma once
//
// perf_monitor.h -- shared types and declarations for the injected
// performance-monitoring DLL.
//
// The DLL is injected into Minecraft.Server.exe at runtime.  It uses
// DbgHelp to resolve private symbols from the PDB, installs a
// trampoline on MinecraftServer::tick(), and collects per-phase timing
// from inside the server process.  Metrics are served as length-prefixed
// JSON over a lightweight TCP socket.
//

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <deque>

#include <windows.h>

// -----------------------------------------------------------------------
// Snapshot data -- produced on the game thread, consumed by TCP thread
// -----------------------------------------------------------------------

struct LevelMetrics {
    int dimension;           // 0 = Overworld, -1 = Nether, 1 = End
    int64_t levelTickUs;     // Level::tick() (weather)
    int64_t entityTickUs;    // Level::tickEntities()
    bool    entityTickSkipped;
    int64_t trackerTickUs;   // EntityTracker::tick()
    int     entityCount;
    int     globalEntityCount;
    int     playerCount;
    int     loadedChunks;
    int     entitiesToRemove;
    int     tileEntityCount;
};

struct TickSnapshot {
    int     tick;
    int64_t timestampMs;
    int64_t totalUs;

    int64_t poolResetUs;
    std::vector<LevelMetrics> levels;
    int64_t playersTickUs;
    int64_t connectionTickUs;
    int64_t consoleTickUs;

    int     totalPlayers;
    double  memoryUsedMb;
    double  memoryTotalMb;
    bool    isAutosaving;
    bool    isPaused;
};

struct AutosaveSnapshot {
    int     tick;
    int64_t timestampMs;
    int64_t totalUs;
    int64_t playersUs;
    int64_t levelsUs;
    int64_t rulesUs;
    int64_t flushUs;
};

// -----------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------

// Set to true once hooks are installed; the tick wrapper checks this.
extern std::atomic<bool> g_hooksInstalled;

// Set to true when at least one TCP client is connected.  When false the
// tick wrapper still runs (it must -- it IS the tick) but skips all
// timing and metric collection.
extern std::atomic<bool> g_clientConnected;

// -----------------------------------------------------------------------
// symbols.cpp -- PDB symbol resolution
// -----------------------------------------------------------------------

struct ResolvedSymbols {
    uintptr_t MinecraftServer_tick;       // private void tick()
    uintptr_t MinecraftServer_getInstance; // static MinecraftServer* getInstance()
    uintptr_t ServerLevel_getChunkMap;    // public PlayerChunkMap* getChunkMap()
};

bool ResolveSymbols(HANDLE hProcess, uintptr_t moduleBase, ResolvedSymbols &out);

// Global resolved symbols (set during init, read from hooks/metrics)
extern ResolvedSymbols g_symbols;

// -----------------------------------------------------------------------
// hooks.cpp -- trampoline installation
// -----------------------------------------------------------------------

bool InstallTickHook(uintptr_t tickAddr);
void RemoveTickHook();

// -----------------------------------------------------------------------
// metrics.cpp -- timing and data collection
// -----------------------------------------------------------------------

void MetricsInit();
void MetricsShutdown();

// Called from inside the hooked tick to collect stats.
// The hook itself is in hooks.cpp; these helpers read server internals.
void CollectLevelMetrics(void *serverLevel, int index, LevelMetrics &out);
void CollectMemoryMetrics(double &usedMb, double &totalMb);

// Push a completed snapshot to the outbound queue (thread-safe).
void PushTickSnapshot(TickSnapshot &&snap);
void PushAutosaveSnapshot(AutosaveSnapshot &&snap);

// Pop all pending snapshots (called by TCP thread).
std::vector<TickSnapshot>    DrainTickSnapshots();
std::vector<AutosaveSnapshot> DrainAutosaveSnapshots();

// -----------------------------------------------------------------------
// tcp_server.cpp -- lightweight JSON-over-TCP server
// -----------------------------------------------------------------------

bool TcpServerStart(int port);
void TcpServerStop();

// -----------------------------------------------------------------------
// json_writer.cpp -- serialization
// -----------------------------------------------------------------------

std::string SerializeTick(const TickSnapshot &s);
std::string SerializeAutosave(const AutosaveSnapshot &s);
std::string SerializeHelloAck(int levelCount);

// -----------------------------------------------------------------------
// Logging (writes to perf-monitor.log next to the DLL)
// -----------------------------------------------------------------------

void LogInit(HMODULE hModule);
void LogWrite(const char *fmt, ...);
void LogShutdown();
