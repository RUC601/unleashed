#include "Kmbox/KmboxB.h"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <setupapi.h>
#include <devguid.h>
#include <thread>

#pragma comment(lib, "setupapi.lib")

// Find a COM port by device description substring
static std::string find_port(const std::string& targetDescription)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
        char buf[512];
        DWORD nSize = 0;

        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buf, sizeof(buf), &nSize) && nSize > 0) {
            buf[(std::min)(nSize, static_cast<DWORD>(sizeof(buf) - 1))] = '\0';
            std::string deviceDescription = buf;

            size_t comPos = deviceDescription.find("COM");
            size_t endPos = deviceDescription.find(")", comPos);

            if (comPos != std::string::npos && endPos != std::string::npos && deviceDescription.find(targetDescription) != std::string::npos) {
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return deviceDescription.substr(comPos, endPos - comPos);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "";
}

// Open a serial port handle with the given baud rate
static bool open_port(HANDLE& hSerial, const char* portName, DWORD baudRate)
{
    hSerial = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE) return false;

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutConstant = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        Diagnostics::Error("[KMBOX-B] Error setting serial timeouts. GetLastError=%lu",
            static_cast<unsigned long>(GetLastError()));
        CloseHandle(hSerial);
        return false;
    }

    return true;
}

static std::string normalize_port_name(const std::string& portName)
{
    if (portName.empty() || portName.rfind("\\\\.\\", 0) == 0)
        return portName;

    if (portName.rfind("COM", 0) == 0 && portName.size() > 4)
        return "\\\\.\\" + portName;

    return portName;
}

KmBoxBManager::~KmBoxBManager()
{
    StopWorkers();
    ClosePort();
}

void KmBoxBManager::SetConnectionState(KmBoxConnectionState state)
{
    const KmBoxConnectionState previous = connectionState.exchange(state, std::memory_order_acq_rel);
    if (previous != state) {
        Diagnostics::Info("[KMBOX-B] connection state %s -> %s",
            ToString(previous), ToString(state));
    }
}

bool KmBoxBManager::OpenConfiguredPort()
{
    if (serialPortName.empty()) {
        SetConnectionState(KmBoxConnectionState::Disconnected);
        return false;
    }

    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }

    HANDLE newHandle = INVALID_HANDLE_VALUE;
    if (!open_port(newHandle, serialPortName.c_str(), CBR_115200)) {
        Diagnostics::Error("[KMBOX-B] Failed to open serial port %s. GetLastError=%lu",
            serialPortName.c_str(), static_cast<unsigned long>(GetLastError()));
        SetConnectionState(KmBoxConnectionState::Error);
        return false;
    }

    hSerial = newHandle;
    SetConnectionState(KmBoxConnectionState::Connected);
    return true;
}

void KmBoxBManager::ClosePort()
{
    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    SetConnectionState(KmBoxConnectionState::Disconnected);
}

bool KmBoxBManager::SendCommandOnce(const std::string& command)
{
    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten = 0;
    const BOOL ok = WriteFile(hSerial, command.c_str(),
        static_cast<DWORD>(command.length()), &bytesWritten, NULL);
    if (!ok || bytesWritten != command.length()) {
        Diagnostics::Error("[KMBOX-B] Serial write failed. bytes=%lu/%zu GetLastError=%lu command=%s",
            static_cast<unsigned long>(bytesWritten),
            command.length(),
            static_cast<unsigned long>(GetLastError()),
            command.c_str());
        return false;
    }

    Diagnostics::Trace("[KMBOX-B] sent command: %s", command.c_str());
    return true;
}

bool KmBoxBManager::SendCommandWithRetry(const std::string& command, KmBoxCommandType type)
{
    for (int attempt = 1; attempt <= KmBoxRuntimeConfig::CommandMaxRetries; ++attempt) {
        if (SendCommandOnce(command)) {
            if (connectionState.load(std::memory_order_acquire) != KmBoxConnectionState::Connected)
                SetConnectionState(KmBoxConnectionState::Connected);
            return true;
        }

        SetConnectionState(KmBoxConnectionState::Error);
        Diagnostics::Error("[KMBOX-B] %s command failed on attempt %d/%d.",
            ToString(type), attempt, KmBoxRuntimeConfig::CommandMaxRetries);

        std::this_thread::sleep_for(std::chrono::milliseconds(
            KmBoxRuntimeConfig::CommandRetryBackoffMs * attempt));

        if (attempt < KmBoxRuntimeConfig::CommandMaxRetries)
            OpenConfiguredPort();
    }

    return false;
}

void KmBoxBManager::EnqueueCommand(const std::string& command, KmBoxCommandType type)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (commandQueue.size() >= KmBoxRuntimeConfig::CommandQueueMaxSize) {
            Diagnostics::Error("[KMBOX-B] Command queue full; dropping oldest command.");
            commandQueue.pop_front();
        }

        KmBoxQueuedSerialCommand queued{};
        queued.command = command;
        queued.type = type;
        queued.enqueuedAt = std::chrono::steady_clock::now();
        commandQueue.push_back(std::move(queued));
    }
    queueCv.notify_one();
}

void KmBoxBManager::StartWorkers()
{
    if (!workerRunning.exchange(true, std::memory_order_acq_rel)) {
        queueThread = std::thread(&KmBoxBManager::QueueWorkerLoop, this);
    }
    if (!heartbeatRunning.exchange(true, std::memory_order_acq_rel)) {
        heartbeatThread = std::thread(&KmBoxBManager::HeartbeatLoop, this);
    }
}

void KmBoxBManager::StopWorkers()
{
    if (workerRunning.exchange(false, std::memory_order_acq_rel)) {
        queueCv.notify_all();
        if (queueThread.joinable())
            queueThread.join();
    }
    if (heartbeatRunning.exchange(false, std::memory_order_acq_rel)) {
        if (heartbeatThread.joinable())
            heartbeatThread.join();
    }
}

void KmBoxBManager::QueueWorkerLoop()
{
    auto lastFlush = std::chrono::steady_clock::now();
    while (workerRunning.load(std::memory_order_acquire)) {
        KmBoxQueuedSerialCommand command{};
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock,
                std::chrono::milliseconds(KmBoxRuntimeConfig::CommandFlushIntervalMs),
                [this]() {
                    return !workerRunning.load(std::memory_order_acquire) || !commandQueue.empty();
                });

            if (!workerRunning.load(std::memory_order_acquire))
                break;
            if (commandQueue.empty())
                continue;

            command = std::move(commandQueue.front());
            commandQueue.pop_front();
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - lastFlush;
        const auto flushInterval = std::chrono::milliseconds(KmBoxRuntimeConfig::CommandFlushIntervalMs);
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (connectionState.load(std::memory_order_acquire) != KmBoxConnectionState::Connected)
            OpenConfiguredPort();

        if (!SendCommandWithRetry(command.command, command.type)) {
            Diagnostics::Error("[KMBOX-B] Dropped %s command after retries.",
                ToString(command.type));
        }

        lastFlush = std::chrono::steady_clock::now();
    }
}

void KmBoxBManager::HeartbeatLoop()
{
    while (heartbeatRunning.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(KmBoxRuntimeConfig::HeartbeatIntervalMs));
        if (!heartbeatRunning.load(std::memory_order_acquire))
            break;

        if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Disconnected) {
            OpenConfiguredPort();
            continue;
        }

        if (!SendCommandWithRetry("km.move(0,0)\r\n", KmBoxCommandType::Heartbeat)) {
            Diagnostics::Error("[KMBOX-B] heartbeat failed; attempting serial reconnect.");
            OpenConfiguredPort();
        }
    }
}

int KmBoxBManager::init()
{
    return init("");
}

int KmBoxBManager::init(const std::string& portName)
{
    SetConnectionState(KmBoxConnectionState::Connecting);
    std::string port = portName.empty() ? find_port("USB-SERIAL CH340") : normalize_port_name(portName);
    if (port.empty()) {
        Diagnostics::Error("[KMBOX-B] Serial port not found.");
        SetConnectionState(KmBoxConnectionState::Disconnected);
        return -1;
    }
    serialPortName = port;
    if (!OpenConfiguredPort()) {
        return -2;
    }
    StartWorkers();
    return 0;
}

void KmBoxBManager::km_move(int X, int Y)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseMove);
}

void KmBoxBManager::km_move_auto(int X, int Y, int runtime)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + "," + std::to_string(runtime) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseAutoMove);
}

void KmBoxBManager::km_click()
{
    std::string command = "km.left(" + std::to_string(1) + ")\r\n";
    std::string command1 = "km.left(" + std::to_string(0) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseButton);
    EnqueueCommand(command1, KmBoxCommandType::MouseButton);
}

void KmBoxBManager::km_left(bool down)
{
    std::string command = "km.left(" + std::to_string(down ? 1 : 0) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseButton);
}

void KmBoxBManager::km_right(bool down)
{
    std::string command = "km.right(" + std::to_string(down ? 1 : 0) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseButton);
}

void KmBoxBManager::km_middle(bool down)
{
    std::string command = "km.middle(" + std::to_string(down ? 1 : 0) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseButton);
}
