//
// tcp_server.cpp -- Lightweight TCP server for streaming JSON metrics.
//
// Protocol:
//   - Length-prefixed frames: [4 bytes big-endian uint32 length][UTF-8 JSON]
//   - Client sends a "hello" message after connecting
//   - Server responds with "hello_ack" then streams tick snapshots
//
// Threading:
//   - Listener thread: accepts connections, reads client messages
//   - Game thread (via hooks.cpp): pushes snapshots to the queue
//   - Broadcast thread: drains queue, serializes, sends to all clients
//

#include "perf_monitor.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// -----------------------------------------------------------------------
// State
// -----------------------------------------------------------------------

static SOCKET g_listenSock = INVALID_SOCKET;
static HANDLE g_listenerThread = nullptr;
static HANDLE g_broadcastThread = nullptr;
static std::atomic<bool> g_running{false};

static std::mutex g_clientsMtx;
static std::vector<SOCKET> g_clients;
static constexpr int MAX_CLIENTS = 4;

static constexpr int SEND_BUF_LIMIT = 64 * 1024;  // 64 KB per client

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void SendFrame(SOCKET sock, const std::string &json)
{
    uint32_t len = (uint32_t)json.size();
    // Big-endian length prefix
    uint8_t header[4];
    header[0] = (len >> 24) & 0xFF;
    header[1] = (len >> 16) & 0xFF;
    header[2] = (len >>  8) & 0xFF;
    header[3] = (len      ) & 0xFF;

    // Send header + payload (non-blocking, best effort)
    send(sock, reinterpret_cast<const char *>(header), 4, 0);
    send(sock, json.c_str(), (int)json.size(), 0);
}

static void RemoveClient(SOCKET sock)
{
    closesocket(sock);
    std::lock_guard<std::mutex> lock(g_clientsMtx);
    g_clients.erase(
        std::remove(g_clients.begin(), g_clients.end(), sock),
        g_clients.end());

    if (g_clients.empty()) {
        g_clientConnected.store(false, std::memory_order_release);
        LogWrite("Last client disconnected, metrics collection paused");
    }
}

// -----------------------------------------------------------------------
// Listener thread -- accepts new connections
// -----------------------------------------------------------------------

static DWORD WINAPI ListenerThread(LPVOID)
{
    LogWrite("Listener thread started");

    while (g_running.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(g_listenSock, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(0, &readSet, nullptr, nullptr, &timeout);
        if (result <= 0) continue;

        if (FD_ISSET(g_listenSock, &readSet)) {
            sockaddr_in clientAddr;
            int addrLen = sizeof(clientAddr);
            SOCKET clientSock = accept(g_listenSock,
                                       reinterpret_cast<sockaddr *>(&clientAddr),
                                       &addrLen);
            if (clientSock == INVALID_SOCKET) continue;

            // Check client limit
            {
                std::lock_guard<std::mutex> lock(g_clientsMtx);
                if ((int)g_clients.size() >= MAX_CLIENTS) {
                    LogWrite("Client rejected: max connections reached");
                    closesocket(clientSock);
                    continue;
                }
            }

            // Set non-blocking
            u_long nonBlock = 1;
            ioctlsocket(clientSock, FIONBIO, &nonBlock);

            // Set TCP_NODELAY for low latency
            int nodelay = 1;
            setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));

            char addrStr[64];
            inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
            LogWrite("Client connected from %s:%d", addrStr, ntohs(clientAddr.sin_port));

            // Send hello_ack
            std::string ack = SerializeHelloAck(3); // 3 dimensions
            SendFrame(clientSock, ack);

            // Add to client list
            {
                std::lock_guard<std::mutex> lock(g_clientsMtx);
                g_clients.push_back(clientSock);
                g_clientConnected.store(true, std::memory_order_release);
            }

            LogWrite("Client added, metrics collection active");
        }
    }

    LogWrite("Listener thread exiting");
    return 0;
}

// -----------------------------------------------------------------------
// Broadcast thread -- drains snapshot queue and sends to all clients
// -----------------------------------------------------------------------

static DWORD WINAPI BroadcastThread(LPVOID)
{
    LogWrite("Broadcast thread started");

    while (g_running.load()) {
        // Sleep briefly to batch snapshots (50ms = 1 tick at 20 TPS)
        Sleep(50);

        if (!g_clientConnected.load(std::memory_order_relaxed)) continue;

        // Drain tick snapshots
        auto ticks = DrainTickSnapshots();
        auto autosaves = DrainAutosaveSnapshots();

        if (ticks.empty() && autosaves.empty()) continue;

        // Serialize all snapshots
        std::vector<std::string> frames;
        frames.reserve(ticks.size() + autosaves.size());

        for (auto &snap : ticks) {
            frames.push_back(SerializeTick(snap));
        }
        for (auto &snap : autosaves) {
            frames.push_back(SerializeAutosave(snap));
        }

        // Send to all clients
        std::vector<SOCKET> deadClients;

        {
            std::lock_guard<std::mutex> lock(g_clientsMtx);
            for (SOCKET sock : g_clients) {
                bool failed = false;
                for (auto &json : frames) {
                    uint32_t len = (uint32_t)json.size();
                    uint8_t header[4];
                    header[0] = (len >> 24) & 0xFF;
                    header[1] = (len >> 16) & 0xFF;
                    header[2] = (len >>  8) & 0xFF;
                    header[3] = (len      ) & 0xFF;

                    int r1 = send(sock, reinterpret_cast<const char *>(header), 4, 0);
                    int r2 = send(sock, json.c_str(), (int)json.size(), 0);

                    if (r1 == SOCKET_ERROR || r2 == SOCKET_ERROR) {
                        int err = WSAGetLastError();
                        if (err != WSAEWOULDBLOCK) {
                            failed = true;
                            break;
                        }
                        // WOULDBLOCK: client can't keep up, skip this frame
                        break;
                    }
                }
                if (failed) {
                    deadClients.push_back(sock);
                }
            }
        }

        // Clean up dead clients
        for (SOCKET sock : deadClients) {
            LogWrite("Client disconnected (send error)");
            RemoveClient(sock);
        }
    }

    LogWrite("Broadcast thread exiting");
    return 0;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool TcpServerStart(int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // WSA may already be initialized by the server -- that's fine
    }

    g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSock == INVALID_SOCKET) {
        LogWrite("socket() failed: %d", WSAGetLastError());
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(g_listenSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LogWrite("bind() failed on port %d: %d", port, WSAGetLastError());
        closesocket(g_listenSock);
        g_listenSock = INVALID_SOCKET;
        return false;
    }

    if (listen(g_listenSock, 4) == SOCKET_ERROR) {
        LogWrite("listen() failed: %d", WSAGetLastError());
        closesocket(g_listenSock);
        g_listenSock = INVALID_SOCKET;
        return false;
    }

    g_running.store(true);

    g_listenerThread = CreateThread(nullptr, 0, ListenerThread, nullptr, 0, nullptr);
    g_broadcastThread = CreateThread(nullptr, 0, BroadcastThread, nullptr, 0, nullptr);

    return true;
}

void TcpServerStop()
{
    g_running.store(false);

    if (g_listenSock != INVALID_SOCKET) {
        closesocket(g_listenSock);
        g_listenSock = INVALID_SOCKET;
    }

    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(g_clientsMtx);
        for (SOCKET s : g_clients) {
            closesocket(s);
        }
        g_clients.clear();
    }
    g_clientConnected.store(false);

    // Wait for threads
    if (g_listenerThread) {
        WaitForSingleObject(g_listenerThread, 3000);
        CloseHandle(g_listenerThread);
        g_listenerThread = nullptr;
    }
    if (g_broadcastThread) {
        WaitForSingleObject(g_broadcastThread, 3000);
        CloseHandle(g_broadcastThread);
        g_broadcastThread = nullptr;
    }

    LogWrite("TCP server stopped");
}
