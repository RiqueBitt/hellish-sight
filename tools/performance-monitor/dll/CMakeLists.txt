cmake_minimum_required(VERSION 3.20)
project(PerfMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(perf-monitor SHARED
    src/dllmain.cpp
    src/hooks.cpp
    src/symbols.cpp
    src/metrics.cpp
    src/tcp_server.cpp
    src/json_writer.cpp
)

target_include_directories(perf-monitor PRIVATE src)

target_link_libraries(perf-monitor PRIVATE
    dbghelp     # PDB symbol resolution
    ws2_32      # Winsock TCP server
    psapi       # GetProcessMemoryInfo
)

target_compile_definitions(perf-monitor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# Use /MT to match the server's static CRT (MultiThreaded / MultiThreadedDebug)
set_property(TARGET perf-monitor PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
