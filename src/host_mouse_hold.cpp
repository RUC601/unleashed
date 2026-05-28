#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

std::atomic<bool> g_running{ true };
DWORD g_xbutton = XBUTTON2;
int g_holdSeconds = 0;

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
    std::printf("Usage: %s [x1|x2] [--seconds N]\n", exe);
    std::printf("  x2: hold front side button / VK_XBUTTON2 (default)\n");
    std::printf("  x1: hold rear side button / VK_XBUTTON1\n");
    std::printf("  --seconds N: release automatically after N seconds\n");
}

bool TryParseSeconds(const char* text, int& seconds)
{
    if (!text || !*text)
        return false;

    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 600)
        return false;

    seconds = static_cast<int>(value);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index) {
        if (_stricmp(argv[index], "x1") == 0) {
            g_xbutton = XBUTTON1;
        } else if (_stricmp(argv[index], "x2") == 0) {
            g_xbutton = XBUTTON2;
        } else if (_stricmp(argv[index], "-s") == 0 || _stricmp(argv[index], "--seconds") == 0) {
            if (index + 1 >= argc || !TryParseSeconds(argv[index + 1], g_holdSeconds)) {
                std::printf("Invalid --seconds value.\n\n");
                PrintUsage(argv[0]);
                return 2;
            }
            ++index;
        } else if (_stricmp(argv[index], "-h") == 0 || _stricmp(argv[index], "--help") == 0 ||
                   _stricmp(argv[index], "/?") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::printf("Unknown argument: %s\n\n", argv[index]);
            PrintUsage(argv[0]);
            return 2;
        }
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    if (g_holdSeconds > 0)
        std::printf("Holding %s on this host for %d seconds.\n", ButtonName(g_xbutton), g_holdSeconds);
    else
        std::printf("Holding %s on this host.\n", ButtonName(g_xbutton));
    std::printf("Keep this window open while testing DMA KeyState on the secondary machine.\n");
    if (g_holdSeconds <= 0)
        std::printf("Press Enter or Ctrl+C here to release and exit.\n\n");
    else
        std::printf("The button will release automatically.\n\n");

    if (!SendXButton(g_xbutton, true)) {
        std::printf("SendInput(XDOWN) failed. GetLastError=%lu\n", GetLastError());
        return 1;
    }

    std::thread inputThread;
    if (g_holdSeconds <= 0) {
        inputThread = std::thread([] {
            (void)getchar();
            g_running.store(false);
        });
    }

    int tick = 0;
    const auto started = std::chrono::steady_clock::now();
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

        if (g_holdSeconds > 0 &&
            std::chrono::steady_clock::now() - started >= std::chrono::seconds(g_holdSeconds)) {
            g_running.store(false);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ReleaseButton();
    if (inputThread.joinable())
        inputThread.detach();

    std::printf("\nReleased %s. Bye.\n", ButtonName(g_xbutton));
    return 0;
}
