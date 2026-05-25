#pragma once

#include "KmBoxConfig.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <Windows.h>
#include <math.h>
#include <random>
#include <string>
#include <thread>

class KmBoxMouse
{
public:
    soft_mouse_t MouseData{};
public:
    // Move mouse by relative offset
    int Move(int x, int y);
    // Move mouse with automatic trajectory over given runtime
    int Move_Auto(int x, int y, int Runtime);
    // Left mouse button
    int Left(bool Down);
    // Right mouse button
    int Right(bool Down);
    // Middle mouse button
    int Middle(bool Down);
};

class KmBoxKeyBoard
{
public:
    std::thread t_Listen;
    WORD MonitorPort;
    SOCKET s_ListenSocket = 0;
    bool ListenerRuned = false;
public:
    standard_keyboard_report_t hw_Keyboard;
    standard_mouse_report_t hw_Mouse;
public:
    ~KmBoxKeyBoard();
    void ListenThread();
    int StartMonitor(WORD Port);
    void EndMonitor();
public:
    bool GetKeyState(WORD vKey);
};

class KmBoxNetManager
{
private:
    SOCKADDR_IN AddrServer;
    SOCKET s_Client = 0;
    client_data ReceiveData;
    client_data PostData;
private:
    int NetHandler();
    int SendData(int DataLength);
public:
    ~KmBoxNetManager();
    // Initialize device connection
    int InitDevice(const std::string& IP, WORD Port, const std::string& Mac);
    // Reboot the device
    int RebootDevice();
    // Set network configuration
    int SetConfig(const std::string& IP, WORD Port);
public:
    friend class KmBoxMouse;
    KmBoxMouse Mouse;
    friend class KmBoxKeyBoard;
    KmBoxKeyBoard KeyBoard;
};

namespace kmbox
{
    inline KmBoxNetManager KmBoxMgr;
}
