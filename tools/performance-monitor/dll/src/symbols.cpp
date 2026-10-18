//
// symbols.cpp -- Resolve private C++ symbols from the PDB using DbgHelp.
//
// Since we are injected into the process, we can use SymFromName() to
// look up mangled symbol names.  The PDB must be next to the .exe or
// in the _NT_SYMBOL_PATH.
//

#include "perf_monitor.h"

#pragma warning(push)
#pragma warning(disable : 4091) // 'typedef ': ignored on left of '' when no variable is declared
#include <dbghelp.h>
#pragma warning(pop)

#pragma comment(lib, "dbghelp.lib")

// -----------------------------------------------------------------------
// Decorated (mangled) names for the symbols we need.
//
// These were determined from the PDB.  If the server is rebuilt with
// different compiler settings the manglings may change, so we also
// fall back to undecorated name search.
// -----------------------------------------------------------------------

// MinecraftServer::tick() -- private void __cdecl MinecraftServer::tick(void)
// MSVC x64 mangling: ?tick@MinecraftServer@@AEAAXXZ
static const char *TICK_DECORATED   = "?tick@MinecraftServer@@AEAAXXZ";
static const char *TICK_UNDECORATED = "MinecraftServer::tick";

// MinecraftServer::getInstance() -- public static MinecraftServer* __cdecl MinecraftServer::getInstance(void)
// This is inlined in the header so may not exist as a symbol.
// Instead we resolve the static member MinecraftServer::server directly.
// ?server@MinecraftServer@@0PEAV1@EA
static const char *SERVER_PTR_DECORATED   = "?server@MinecraftServer@@0PEAV1@EA";
static const char *SERVER_PTR_UNDECORATED = "MinecraftServer::server";

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static bool TryResolve(HANDLE hProcess, const char *name, uintptr_t &outAddr)
{
    // SYMBOL_INFO needs extra space for the name
    char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = MAX_SYM_NAME;

    if (SymFromName(hProcess, name, sym)) {
        outAddr = static_cast<uintptr_t>(sym->Address);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool ResolveSymbols(HANDLE hProcess, uintptr_t moduleBase, ResolvedSymbols &out)
{
    memset(&out, 0, sizeof(out));

    // Initialize DbgHelp for this process
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_DEBUG);

    if (!SymInitialize(hProcess, nullptr, FALSE)) {
        LogWrite("SymInitialize failed: 0x%08x", GetLastError());
        return false;
    }

    // Load symbols for the main module
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    DWORD64 base = SymLoadModuleEx(hProcess, nullptr, exePath, nullptr,
                                    moduleBase, 0, nullptr, 0);
    if (!base) {
        DWORD err = GetLastError();
        if (err != ERROR_SUCCESS) {
            LogWrite("SymLoadModuleEx failed: 0x%08x", err);
            SymCleanup(hProcess);
            return false;
        }
    }
    LogWrite("Symbols loaded for %s", exePath);

    // Resolve MinecraftServer::tick()
    if (!TryResolve(hProcess, TICK_DECORATED, out.MinecraftServer_tick)) {
        LogWrite("Decorated tick symbol not found, trying undecorated...");
        if (!TryResolve(hProcess, TICK_UNDECORATED, out.MinecraftServer_tick)) {
            LogWrite("ERROR: Could not resolve MinecraftServer::tick()");
            SymCleanup(hProcess);
            return false;
        }
    }

    // Resolve MinecraftServer::server (static pointer)
    // getInstance() is likely inlined, so we grab the static member address
    if (!TryResolve(hProcess, SERVER_PTR_DECORATED, out.MinecraftServer_getInstance)) {
        LogWrite("Decorated server ptr not found, trying undecorated...");
        if (!TryResolve(hProcess, SERVER_PTR_UNDECORATED, out.MinecraftServer_getInstance)) {
            LogWrite("WARNING: Could not resolve MinecraftServer::server -- metrics will be limited");
            // Not fatal -- we can still time the tick, just can't read members
        }
    }

    // Resolve ServerLevel::getChunkMap()
    // ?getChunkMap@ServerLevel@@QEAAPEAVPlayerChunkMap@@XZ
    if (!TryResolve(hProcess, "?getChunkMap@ServerLevel@@QEAAPEAVPlayerChunkMap@@XZ",
                    out.ServerLevel_getChunkMap)) {
        if (!TryResolve(hProcess, "ServerLevel::getChunkMap", out.ServerLevel_getChunkMap)) {
            LogWrite("WARNING: Could not resolve ServerLevel::getChunkMap()");
        }
    }

    return true;
}
