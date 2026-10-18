//
// dllmain.cpp -- DLL entry point.
//
// When injected into Minecraft.Server.exe:
//   1. Resolve MinecraftServer::tick() via PDB symbols
//   2. Install a trampoline hook on tick()
//   3. Start a TCP server for the GUI to connect to
//
// On unload (FreeLibrary or process exit):
//   1. Remove the hook (restore original bytes)
//   2. Stop the TCP server
//

#include "perf_monitor.h"
#include <cstdio>
#include <cstdarg>

// -----------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------

std::atomic<bool> g_hooksInstalled{false};
std::atomic<bool> g_clientConnected{false};

static HMODULE g_hModule = nullptr;
static HANDLE  g_initThread = nullptr;

static const int DEFAULT_PORT = 19800;
ResolvedSymbols g_symbols = {};

// -----------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------

static FILE *g_logFile = nullptr;
static std::mutex g_logMtx;

void LogInit(HMODULE hModule)
{
    char path[MAX_PATH];
    GetModuleFileNameA(hModule, path, MAX_PATH);

    // Replace DLL name with log name
    char *sep = strrchr(path, '\\');
    if (sep) {
        strcpy(sep + 1, "perf-monitor.log");
    } else {
        strcpy(path, "perf-monitor.log");
    }

    g_logFile = fopen(path, "w");
}

void LogWrite(const char *fmt, ...)
{
    if (!g_logFile) return;

    std::lock_guard<std::mutex> lock(g_logMtx);

    // Timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logFile, "%02d:%02d:%02d.%03d  ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);

    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

void LogShutdown()
{
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

// -----------------------------------------------------------------------
// Initialization thread (runs after injection)
// -----------------------------------------------------------------------

static DWORD WINAPI InitThread(LPVOID)
{
    LogWrite("perf-monitor.dll loaded into PID %u", GetCurrentProcessId());

    // Get the base address of the main executable module
    HMODULE hExe = GetModuleHandleA(nullptr);
    if (!hExe) {
        LogWrite("FATAL: GetModuleHandle(NULL) failed");
        return 1;
    }
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hExe);
    LogWrite("Module base: 0x%llx", (unsigned long long)moduleBase);

    // Resolve symbols from PDB
    ResolvedSymbols syms{};
    HANDLE hProcess = GetCurrentProcess();

    if (!ResolveSymbols(hProcess, moduleBase, syms)) {
        LogWrite("FATAL: Symbol resolution failed");
        return 1;
    }

    g_symbols = syms;
    LogWrite("MinecraftServer::tick() at 0x%llx", (unsigned long long)syms.MinecraftServer_tick);
    LogWrite("MinecraftServer::getInstance() at 0x%llx", (unsigned long long)syms.MinecraftServer_getInstance);
    LogWrite("ServerLevel::getChunkMap() at 0x%llx", (unsigned long long)syms.ServerLevel_getChunkMap);

    // Initialize metrics subsystem
    MetricsInit();

    // Install the tick hook
    if (!InstallTickHook(syms.MinecraftServer_tick)) {
        LogWrite("FATAL: Hook installation failed");
        return 1;
    }
    LogWrite("Tick hook installed");

    // Start TCP server
    if (!TcpServerStart(DEFAULT_PORT)) {
        LogWrite("FATAL: TCP server failed to start on port %d", DEFAULT_PORT);
        RemoveTickHook();
        return 1;
    }
    LogWrite("TCP server listening on port %d", DEFAULT_PORT);

    return 0;
}

// -----------------------------------------------------------------------
// DllMain
// -----------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        LogInit(hModule);

        // Spawn initialization on a separate thread so DllMain returns
        // quickly (loader lock).
        g_initThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        LogWrite("DLL detaching...");

        // Wait for init to finish before tearing down
        if (g_initThread) {
            WaitForSingleObject(g_initThread, 5000);
            CloseHandle(g_initThread);
            g_initThread = nullptr;
        }

        TcpServerStop();
        RemoveTickHook();
        MetricsShutdown();

        LogWrite("Cleanup complete");
        LogShutdown();
        break;
    }

    return TRUE;
}
