#include "Kmbox/KmboxB.h"

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
            buf[nSize] = '\0';
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
    timeouts.ReadIntervalTimeout = 5;
    timeouts.ReadTotalTimeoutConstant = 5;
    timeouts.ReadTotalTimeoutMultiplier = 5;
    timeouts.WriteTotalTimeoutConstant = 5;
    timeouts.WriteTotalTimeoutMultiplier = 5;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Error setting timeouts!" << std::endl;
        CloseHandle(hSerial);
        return false;
    }

    return true;
}

// Send a command string over serial
static void send_command(HANDLE hSerial, const std::string& command)
{
    DWORD bytesWritten;
    if (!WriteFile(hSerial, command.c_str(), command.length(), &bytesWritten, NULL)) {
        std::cerr << "Failed to write to serial port!" << std::endl;
    }
}

int KmBoxBManager::init()
{
    std::string port = find_port("USB-SERIAL CH340");
    if (port.empty()) {
        return -1;
    }
    if (!open_port(hSerial, port.c_str(), CBR_115200)) {
        return -2;
    }
    return 0;
}

void KmBoxBManager::km_move(int X, int Y)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + ")\r\n";
    send_command(hSerial, command.c_str());
}

void KmBoxBManager::km_move_auto(int X, int Y, int runtime)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + "," + std::to_string(runtime) + ")\r\n";
    send_command(hSerial, command.c_str());
}

void KmBoxBManager::km_click()
{
    std::string command = "km.left(" + std::to_string(1) + ")\r\n";
    Sleep(10);
    std::string command1 = "km.left(" + std::to_string(0) + ")\r\n";
    send_command(hSerial, command.c_str());
    send_command(hSerial, command1.c_str());
}
