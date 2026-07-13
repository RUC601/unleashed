#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace OW::ProcessConnection {

inline std::atomic<bool> g_connected{ false };
inline std::atomic<bool> g_connecting{ false };
inline std::atomic<bool> g_reconnectRequested{ false };
inline std::atomic<int> g_pid{ 0 };
inline std::atomic<uint64_t> g_baseAddress{ 0 };
inline std::atomic<uint64_t> g_connectionEpoch{ 0 };
inline std::mutex g_statusMutex;
inline std::string g_statusText = "Waiting for Overwatch.exe";

inline void RequestReconnect()
{
    g_reconnectRequested.store(true, std::memory_order_release);
}

inline bool ConsumeReconnectRequest()
{
    return g_reconnectRequested.exchange(false, std::memory_order_acq_rel);
}

inline bool IsReconnectRequested()
{
    return g_reconnectRequested.load(std::memory_order_acquire);
}

inline bool IsConnected()
{
    return g_connected.load(std::memory_order_acquire);
}

inline bool IsConnecting()
{
    return g_connecting.load(std::memory_order_acquire);
}

inline int ConnectedPid()
{
    return g_pid.load(std::memory_order_acquire);
}

inline uint64_t ConnectedBaseAddress()
{
    return g_baseAddress.load(std::memory_order_acquire);
}

inline uint64_t ConnectionEpoch()
{
    return g_connectionEpoch.load(std::memory_order_acquire);
}

inline void SetStatus(bool connected, bool connecting, int pid, uint64_t baseAddress, std::string text)
{
    g_pid.store(connected ? pid : 0, std::memory_order_release);
    g_baseAddress.store(connected ? baseAddress : 0, std::memory_order_release);
    if (connected)
        g_connectionEpoch.fetch_add(1, std::memory_order_acq_rel);
    g_connected.store(connected, std::memory_order_release);
    g_connecting.store(connecting, std::memory_order_release);

    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_statusText = std::move(text);
}

inline std::string StatusText()
{
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_statusText;
}

} // namespace OW::ProcessConnection
