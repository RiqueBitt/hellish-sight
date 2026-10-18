#!/usr/bin/env python3
"""
DLL Injector for perf-monitor.dll.

Finds a running Minecraft.Server.exe process and injects the performance
monitoring DLL using the standard CreateRemoteThread + LoadLibraryA technique.

Usage:
    python inject.py                    # Auto-find server process
    python inject.py --pid 12345        # Inject into specific PID
    python inject.py --dll path/to.dll  # Use a specific DLL path
    python inject.py --eject            # Unload the DLL from the server
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys

# Win32 constants
PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
INFINITE = 0xFFFFFFFF

# Win32 API
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

# Type aliases
DWORD = wt.DWORD
HANDLE = wt.HANDLE
LPVOID = wt.LPVOID
LPCSTR = wt.LPCSTR
BOOL = wt.BOOL
SIZE_T = ctypes.c_size_t

# Function prototypes
kernel32.OpenProcess.restype = HANDLE
kernel32.OpenProcess.argtypes = [DWORD, BOOL, DWORD]

kernel32.VirtualAllocEx.restype = LPVOID
kernel32.VirtualAllocEx.argtypes = [HANDLE, LPVOID, SIZE_T, DWORD, DWORD]

kernel32.VirtualFreeEx.restype = BOOL
kernel32.VirtualFreeEx.argtypes = [HANDLE, LPVOID, SIZE_T, DWORD]

kernel32.WriteProcessMemory.restype = BOOL
kernel32.WriteProcessMemory.argtypes = [HANDLE, LPVOID, ctypes.c_void_p, SIZE_T, ctypes.POINTER(SIZE_T)]

kernel32.CreateRemoteThread.restype = HANDLE
kernel32.CreateRemoteThread.argtypes = [HANDLE, ctypes.c_void_p, SIZE_T, LPVOID, LPVOID, DWORD, ctypes.POINTER(DWORD)]

kernel32.WaitForSingleObject.restype = DWORD
kernel32.WaitForSingleObject.argtypes = [HANDLE, DWORD]

kernel32.GetExitCodeThread.restype = BOOL
kernel32.GetExitCodeThread.argtypes = [HANDLE, ctypes.POINTER(DWORD)]

kernel32.CloseHandle.restype = BOOL
kernel32.CloseHandle.argtypes = [HANDLE]

kernel32.GetModuleHandleA.restype = HANDLE
kernel32.GetModuleHandleA.argtypes = [LPCSTR]

kernel32.GetProcAddress.restype = ctypes.c_void_p
kernel32.GetProcAddress.argtypes = [HANDLE, LPCSTR]


def find_server_pid() -> int | None:
    """Find the PID of a running Minecraft.Server.exe."""
    # Use EnumProcesses to list all PIDs
    arr = (DWORD * 4096)()
    cb_needed = DWORD()
    if not psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(cb_needed)):
        return None

    num_procs = cb_needed.value // ctypes.sizeof(DWORD)

    for i in range(num_procs):
        pid = arr[i]
        if pid == 0:
            continue

        h = kernel32.OpenProcess(0x0410, False, pid)  # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
        if not h:
            continue

        try:
            name_buf = (ctypes.c_char * 260)()
            if psapi.GetModuleBaseNameA(h, None, name_buf, 260):
                name = name_buf.value.decode("utf-8", errors="ignore")
                if name.lower() == "minecraft.server.exe":
                    return pid
        finally:
            kernel32.CloseHandle(h)

    return None


def inject_dll(pid: int, dll_path: str) -> bool:
    """Inject a DLL into the target process."""
    dll_path = os.path.abspath(dll_path)
    if not os.path.isfile(dll_path):
        print(f"ERROR: DLL not found: {dll_path}")
        return False

    dll_bytes = dll_path.encode("utf-8") + b"\x00"
    print(f"Injecting {dll_path} into PID {pid}...")

    # Open the target process
    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        print(f"ERROR: OpenProcess failed (error {ctypes.get_last_error()})")
        print("       Are you running as Administrator?")
        return False

    try:
        # Allocate memory in the target for the DLL path string
        remote_mem = kernel32.VirtualAllocEx(
            h_process, None, len(dll_bytes),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
        )
        if not remote_mem:
            print(f"ERROR: VirtualAllocEx failed (error {ctypes.get_last_error()})")
            return False

        # Write the DLL path into the allocated memory
        written = SIZE_T(0)
        if not kernel32.WriteProcessMemory(
            h_process, remote_mem, dll_bytes, len(dll_bytes), ctypes.byref(written)
        ):
            print(f"ERROR: WriteProcessMemory failed (error {ctypes.get_last_error()})")
            kernel32.VirtualFreeEx(h_process, remote_mem, 0, MEM_RELEASE)
            return False

        # Get the address of LoadLibraryA in kernel32
        h_kernel32 = kernel32.GetModuleHandleA(b"kernel32.dll")
        load_library_addr = kernel32.GetProcAddress(h_kernel32, b"LoadLibraryA")
        if not load_library_addr:
            print("ERROR: Could not find LoadLibraryA")
            kernel32.VirtualFreeEx(h_process, remote_mem, 0, MEM_RELEASE)
            return False

        # Create a remote thread that calls LoadLibraryA(dll_path)
        thread_id = DWORD(0)
        h_thread = kernel32.CreateRemoteThread(
            h_process, None, 0,
            ctypes.cast(load_library_addr, LPVOID),
            remote_mem, 0, ctypes.byref(thread_id)
        )
        if not h_thread:
            print(f"ERROR: CreateRemoteThread failed (error {ctypes.get_last_error()})")
            kernel32.VirtualFreeEx(h_process, remote_mem, 0, MEM_RELEASE)
            return False

        print(f"Remote thread created (TID {thread_id.value}), waiting...")

        # Wait for LoadLibrary to complete
        kernel32.WaitForSingleObject(h_thread, 10000)  # 10 second timeout

        # Check the exit code (HMODULE of the loaded DLL, or 0 on failure)
        exit_code = DWORD(0)
        kernel32.GetExitCodeThread(h_thread, ctypes.byref(exit_code))
        kernel32.CloseHandle(h_thread)

        # Free the remote string memory
        kernel32.VirtualFreeEx(h_process, remote_mem, 0, MEM_RELEASE)

        if exit_code.value == 0:
            print("ERROR: LoadLibraryA returned NULL in the target process.")
            print("       Check that the DLL and its dependencies (dbghelp.dll) are accessible.")
            print("       Check perf-monitor.log next to the DLL for details.")
            return False

        print(f"DLL loaded at 0x{exit_code.value:X}")
        print("Injection successful! The performance monitor is now active.")
        print("Connect the GUI to localhost:19800 to view metrics.")
        return True

    finally:
        kernel32.CloseHandle(h_process)


def eject_dll(pid: int, dll_name: str = "perf-monitor.dll") -> bool:
    """Unload the DLL from the target process."""
    print(f"Ejecting {dll_name} from PID {pid}...")

    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        print(f"ERROR: OpenProcess failed (error {ctypes.get_last_error()})")
        return False

    try:
        # Find the DLL's HMODULE in the target process
        h_modules = (HANDLE * 1024)()
        cb_needed = DWORD()
        if not psapi.EnumProcessModules(h_process, ctypes.byref(h_modules),
                                         ctypes.sizeof(h_modules), ctypes.byref(cb_needed)):
            print("ERROR: EnumProcessModules failed")
            return False

        num_modules = cb_needed.value // ctypes.sizeof(HANDLE)
        target_module = None

        for i in range(num_modules):
            name_buf = (ctypes.c_char * 260)()
            if psapi.GetModuleBaseNameA(h_process, h_modules[i], name_buf, 260):
                name = name_buf.value.decode("utf-8", errors="ignore")
                if name.lower() == dll_name.lower():
                    target_module = h_modules[i]
                    break

        if not target_module:
            print(f"ERROR: {dll_name} not found in process")
            return False

        # Get FreeLibrary address
        h_kernel32 = kernel32.GetModuleHandleA(b"kernel32.dll")
        free_library_addr = kernel32.GetProcAddress(h_kernel32, b"FreeLibrary")

        # Call FreeLibrary(hModule) in the target
        thread_id = DWORD(0)
        h_thread = kernel32.CreateRemoteThread(
            h_process, None, 0,
            ctypes.cast(free_library_addr, LPVOID),
            target_module, 0, ctypes.byref(thread_id)
        )
        if not h_thread:
            print(f"ERROR: CreateRemoteThread failed (error {ctypes.get_last_error()})")
            return False

        kernel32.WaitForSingleObject(h_thread, 10000)
        kernel32.CloseHandle(h_thread)

        print("DLL ejected successfully.")
        return True

    finally:
        kernel32.CloseHandle(h_process)


def main():
    parser = argparse.ArgumentParser(description="Inject perf-monitor.dll into Minecraft.Server.exe")
    parser.add_argument("--pid", type=int, default=None, help="Target process ID (auto-detect if omitted)")
    parser.add_argument("--dll", default=None, help="Path to perf-monitor.dll")
    parser.add_argument("--eject", action="store_true", help="Eject (unload) the DLL instead of injecting")
    args = parser.parse_args()

    # Find or validate the PID
    pid = args.pid
    if pid is None:
        pid = find_server_pid()
        if pid is None:
            print("ERROR: No running Minecraft.Server.exe found.")
            print("       Start the server first, or specify --pid manually.")
            sys.exit(1)
        print(f"Found Minecraft.Server.exe (PID {pid})")

    if args.eject:
        success = eject_dll(pid)
        sys.exit(0 if success else 1)

    # Find the DLL
    dll_path = args.dll
    if dll_path is None:
        # Look for it relative to this script
        script_dir = os.path.dirname(os.path.abspath(__file__))
        candidates = [
            os.path.join(script_dir, "dll", "build", "Release", "perf-monitor.dll"),
            os.path.join(script_dir, "dll", "build", "Debug", "perf-monitor.dll"),
            os.path.join(script_dir, "dll", "build", "perf-monitor.dll"),
            os.path.join(script_dir, "perf-monitor.dll"),
        ]
        for candidate in candidates:
            if os.path.isfile(candidate):
                dll_path = candidate
                break

        if dll_path is None:
            print("ERROR: Could not find perf-monitor.dll")
            print("       Build it first or specify --dll path")
            print("       Searched:", "\n       ".join(candidates))
            sys.exit(1)

    success = inject_dll(pid, dll_path)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
