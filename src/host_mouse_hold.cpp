#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

std::atomic<bool> g_running{ true };
DWORD g_xbutton = XBUTTON2;

const char* ButtonName(DWORD button)
{
    return button == XBUTTON1 ? "XBUTTON1 / VK_XBUTTON1" : "XBUTTON2 / VK_XBUTTON2";
}

int ButtonVk(DWORD button)
{
    return button == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
}

bool SendXButton(DWORD button, bool down)
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    input.mi.mouseData = button;
    return SendInput(1, &input, sizeof(input)) == 1;
}

void ReleaseButton()
{
    SendXButton(g_xbutton, false);
}

BOOL WINAPI ConsoleHandler(DWORD event)
{
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running.store(false);
        ReleaseButton();
        return TRUE;
    default:
        return FALSE;
    }
}

void PrintUsage(const char* exe)
{
    std::printf("Usage: %s [x1|x2]\n", exe);
    std::printf("  x2: hold front side button / VK_XBUTTON2 (default)\n");
    std::printf("  x1: hold rear side button / VK_XBUTTON1\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1) {
        if (_stricmp(argv[1], "x1") == 0) {
            g_xbutton = XBUTTON1;
        } else if (_stricmp(argv[1], "x2") == 0) {
            g_xbutton = XBUTTON2;
        } else if (_stricmp(argv[1], "-h") == 0 || _stricmp(argv[1], "--help") == 0 ||
                   _stricmp(argv[1], "/?") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::printf("Unknown button: %s\n\n", argv[1]);
            PrintUsage(argv[0]);
            return 2;
        }
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    std::printf("Holding %s on this host.\n", ButtonName(g_xbutton));
    std::printf("Keep this window open while testing DMA KeyState on the secondary machine.\n");
    std::printf("Press Enter or Ctrl+C here to release and exit.\n\n");

    if (!SendXButton(g_xbutton, true)) {
        std::printf("SendInput(XDOWN) failed. GetLastError=%lu\n", GetLastError());
        return 1;
    }

    std::thread inputThread([] {
        (void)getchar();
        g_running.store(false);
    });

    int tick = 0;
    while (g_running.load()) {
        const bool localDown = (GetAsyncKeyState(ButtonVk(g_xbutton)) & 0x8000) != 0;
        std::printf("\r[%03d] Host local GetAsyncKeyState: %s   ",
            tick++,
            localDown ? "DOWN" : "up");
        std::fflush(stdout);

        // Reassert the down event periodically in case the foreground app consumes
        // or releases synthetic state unexpectedly.
        if ((tick % 5) == 0)
            SendXButton(g_xbutton, true);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ReleaseButton();
    if (inputThread.joinable())
        inputThread.detach();

    std::printf("\nReleased %s. Bye.\n", ButtonName(g_xbutton));
    return 0;
}
