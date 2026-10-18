//
// hooks.cpp -- Trampoline hook on MinecraftServer::tick().
//
// Overwrites the first 14 bytes of tick() with JMP to HookedTick.
// The trampoline contains the saved prologue bytes + JMP back to tick+N.
//
// The previous crash was caused by a server autosave bug, not the
// trampoline. With that fixed, this approach is stable.
//

#include "perf_monitor.h"
#include <psapi.h>
#include <cstring>

// -----------------------------------------------------------------------
// State
// -----------------------------------------------------------------------

static constexpr int HOOK_SIZE = 14;  // FF 25 00 00 00 00 + 8-byte addr

static uint8_t  g_originalBytes[32] = {};
static uintptr_t g_tickAddr = 0;
static uint8_t  *g_trampoline = nullptr;
static int       g_prologueLen = 0;

static int64_t g_qpcFreq = 0;

using TickFn = void(__fastcall *)(void *thisPtr);

// -----------------------------------------------------------------------
// Timing helpers
// -----------------------------------------------------------------------

static inline int64_t NowUs()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (li.QuadPart * 1000000) / g_qpcFreq;
}

static inline int64_t NowMs()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

// -----------------------------------------------------------------------
// Instruction length decoder
// -----------------------------------------------------------------------

static int GetInstructionLength(const uint8_t *ip)
{
    const uint8_t *p = ip;

    // Skip prefixes
    while ((*p >= 0x40 && *p <= 0x4F) || *p == 0x66 || *p == 0x67 ||
           *p == 0xF2 || *p == 0xF3) {
        p++;
    }

    uint8_t op = *p++;

    // Simple 1-byte opcodes
    if (op == 0x90 || op == 0xC3 || op == 0xCC) return (int)(p - ip);
    if (op >= 0x50 && op <= 0x5F) return (int)(p - ip);

    // MOV reg, imm
    if (op >= 0xB8 && op <= 0xBF) {
        bool rexW = (ip[0] >= 0x48 && ip[0] <= 0x4F);
        return (int)(p - ip) + (rexW ? 8 : 4);
    }

    // Two-byte escape
    if (op == 0x0F) { p++; }

    // ModRM-based instructions
    {
        uint8_t modrm = *p++;
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t rm  = modrm & 7;

        if (rm == 4 && mod != 3) p++;  // SIB

        if (mod == 0 && rm == 5) p += 4;      // [rip+disp32]
        else if (mod == 1)       p += 1;      // [reg+disp8]
        else if (mod == 2)       p += 4;      // [reg+disp32]

        // Group opcodes with immediate
        uint8_t base_op = ip[(p - ip > 2 && (ip[0] >= 0x40 && ip[0] <= 0x4F)) ? 1 : 0];
        if (base_op == 0x81) p += 4;
        else if (base_op == 0x80 || base_op == 0x83) p += 1;
    }

    int len = (int)(p - ip);
    return (len > 0) ? len : 1;
}

static int CopyPrologue(const uint8_t *src, uint8_t *dst, int minBytes)
{
    int copied = 0;
    while (copied < minBytes) {
        int len = GetInstructionLength(src + copied);
        memcpy(dst + copied, src + copied, len);
        copied += len;
        if (copied > 48) break;
    }
    return copied;
}

// -----------------------------------------------------------------------
// The hook function
// -----------------------------------------------------------------------

static int    g_tickCounter = 0;
static double g_memUsedMb = 0.0;
static double g_memTotalMb = 0.0;
static int    g_memUpdateCounter = 0;

// SEH-safe level reader (separate from player count to avoid one killing the other)
static void ReadLevels(void *thisPtr, std::vector<LevelMetrics> &out)
{
    LevelMetrics localLevels[3] = {};
    uint32_t levelCount = 0;

    __try {
        auto base = reinterpret_cast<uint8_t *>(thisPtr);
        auto levelsData   = *reinterpret_cast<void ***>(base + 24);
        auto levelsLength = *reinterpret_cast<uint32_t *>(base + 32);

        if (levelsData && levelsLength > 0 && levelsLength <= 3) {
            levelCount = levelsLength;
            for (uint32_t i = 0; i < levelsLength; i++) {
                localLevels[i].dimension = (i == 0) ? 0 : (i == 1) ? -1 : 1;
                if (levelsData[i]) {
                    CollectLevelMetrics(levelsData[i], i, localLevels[i]);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) {
            LogWrite("WARNING: Exception reading level data");
            logged = true;
        }
        return;
    }

    for (uint32_t i = 0; i < levelCount; i++) {
        out.push_back(localLevels[i]);
    }
}


// Track the time between tick() calls to measure true TPS.
// When autosave (or anything else in run()) stalls, the next tick is
// delayed, and the interval grows beyond 50ms.
static int64_t g_lastTickStartUs = 0;
static int64_t g_tickIntervalUs = 50000;  // default = 50ms = 20 TPS
static bool    g_skipSnapshot = false;    // suppress catch-up ticks

static void __fastcall HookedTick(void *thisPtr)
{
    auto originalTick = reinterpret_cast<TickFn>(g_trampoline);

    int64_t tickStart = NowUs();
    int64_t rawInterval = 50000;
    if (g_lastTickStartUs > 0) {
        rawInterval = tickStart - g_lastTickStartUs;
    }
    g_lastTickStartUs = tickStart;

    // Detect catch-up ticks: after a stall, run() fires tick() rapidly
    // to burn through accumulated unprocessedTime. These have intervals
    // well below 50ms.  We still call tick() but don't report them to
    // the GUI -- they'd flood the TPS average with fake "20 TPS" readings.
    // Only the stall tick itself (interval > 50ms) gets reported.
    if (rawInterval < 25000) {
        // Catch-up tick: run it but don't emit a snapshot
        g_skipSnapshot = true;
    } else {
        g_tickIntervalUs = rawInterval;
        g_skipSnapshot = false;
    }

    if (!g_clientConnected.load(std::memory_order_relaxed)) {
        originalTick(thisPtr);
        return;
    }

    originalTick(thisPtr);

    // Skip catch-up ticks - only emit real-time ticks to the GUI
    if (g_skipSnapshot) return;

    int64_t tickEnd = NowUs();
    int64_t workUs = tickEnd - tickStart;

    g_tickCounter++;

    g_memUpdateCounter++;
    if (g_memUpdateCounter >= 20) {
        g_memUpdateCounter = 0;
        CollectMemoryMetrics(g_memUsedMb, g_memTotalMb);
    }

    std::vector<LevelMetrics> levelMetrics;
    ReadLevels(thisPtr, levelMetrics);
    int totalPlayers = 0;
    for (auto &lm : levelMetrics) totalPlayers += lm.playerCount;

    TickSnapshot snap{};
    snap.tick         = g_tickCounter;
    snap.timestampMs  = NowMs();
    snap.totalUs      = g_tickIntervalUs;  // tick-to-tick interval (for TPS)
    snap.poolResetUs  = workUs;            // tick() work time (for MSPT)
    snap.levels       = std::move(levelMetrics);
    snap.playersTickUs    = 0;
    snap.connectionTickUs = 0;
    snap.consoleTickUs    = 0;
    snap.totalPlayers  = totalPlayers;
    snap.memoryUsedMb  = g_memUsedMb;
    snap.memoryTotalMb = g_memTotalMb;
    snap.isAutosaving  = false;
    snap.isPaused      = false;

    PushTickSnapshot(std::move(snap));
}

// -----------------------------------------------------------------------
// Hook installation
// -----------------------------------------------------------------------

bool InstallTickHook(uintptr_t tickAddr)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreq = freq.QuadPart;

    g_tickAddr = tickAddr;

    // Log first 32 bytes of tick()
    {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(tickAddr);
        char hex[128];
        int pos = 0;
        for (int i = 0; i < 32 && pos < 120; i++)
            pos += snprintf(hex + pos, 128 - pos, "%02X ", p[i]);
        LogWrite("tick() prologue: %s", hex);
    }

    memcpy(g_originalBytes, reinterpret_cast<void *>(tickAddr), 32);

    // Allocate trampoline
    g_trampoline = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE,
                     PAGE_EXECUTE_READWRITE));
    if (!g_trampoline) {
        LogWrite("VirtualAlloc failed: 0x%08x", GetLastError());
        return false;
    }

    // Copy prologue
    g_prologueLen = CopyPrologue(
        reinterpret_cast<const uint8_t *>(tickAddr),
        g_trampoline, HOOK_SIZE);
    LogWrite("Copied %d prologue bytes", g_prologueLen);

    // Log decoded instructions
    {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(tickAddr);
        int off = 0;
        while (off < g_prologueLen) {
            int len = GetInstructionLength(p + off);
            char hex[64]; int hpos = 0;
            for (int i = 0; i < len && hpos < 60; i++)
                hpos += snprintf(hex + hpos, 64 - hpos, "%02X ", p[off + i]);
            LogWrite("  +%d: [%d] %s", off, len, hex);
            off += len;
        }
    }

    // Append JMP back to tick + prologueLen
    uint8_t *jmpBack = g_trampoline + g_prologueLen;
    uintptr_t resumeAddr = tickAddr + g_prologueLen;
    jmpBack[0] = 0xFF;
    jmpBack[1] = 0x25;
    *reinterpret_cast<uint32_t *>(jmpBack + 2) = 0;
    *reinterpret_cast<uintptr_t *>(jmpBack + 6) = resumeAddr;

    // Patch tick() entry to JMP to HookedTick
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void *>(tickAddr), HOOK_SIZE,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogWrite("VirtualProtect failed: 0x%08x", GetLastError());
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
        return false;
    }

    uint8_t *patch = reinterpret_cast<uint8_t *>(tickAddr);
    patch[0] = 0xFF;
    patch[1] = 0x25;
    *reinterpret_cast<uint32_t *>(patch + 2) = 0;
    *reinterpret_cast<uintptr_t *>(patch + 6) = reinterpret_cast<uintptr_t>(&HookedTick);

    VirtualProtect(reinterpret_cast<void *>(tickAddr), HOOK_SIZE, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(tickAddr), HOOK_SIZE);

    g_hooksInstalled.store(true, std::memory_order_release);
    return true;
}

void RemoveTickHook()
{
    if (!g_hooksInstalled.load()) return;

    DWORD oldProtect;
    VirtualProtect(reinterpret_cast<void *>(g_tickAddr), 32,
                   PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(reinterpret_cast<void *>(g_tickAddr), g_originalBytes, g_prologueLen);
    VirtualProtect(reinterpret_cast<void *>(g_tickAddr), 32,
                   oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(g_tickAddr), 32);

    if (g_trampoline) {
        VirtualFree(g_trampoline, 0, MEM_RELEASE);
        g_trampoline = nullptr;
    }

    g_hooksInstalled.store(false, std::memory_order_release);
    LogWrite("Tick hook removed");
}
