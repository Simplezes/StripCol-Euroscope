#include "pch.h"
#include "common/Log.h"
#include <ctime>
#include <mutex>

namespace
{
    std::mutex consoleMutex;
    HANDLE consoleOutput = INVALID_HANDLE_VALUE;

    bool HasConsole()
    {
        return consoleOutput != INVALID_HANDLE_VALUE;
    }

    std::string Timestamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        char buffer[16];
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
        return buffer;
    }

    void ProtectHostProcess()
    {
        HWND window = GetConsoleWindow();
        if (window)
            DeleteMenu(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND);

        SetConsoleCtrlHandler(nullptr, TRUE);

        HANDLE input = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        if (input != INVALID_HANDLE_VALUE)
        {
            SetConsoleMode(input, ENABLE_EXTENDED_FLAGS);
            CloseHandle(input);
        }
    }
}

namespace Log
{
    void Write(const std::string &message)
    {
        OutputDebugStringA(("[StripCol] " + message + "\n").c_str());

        std::lock_guard<std::mutex> lock(consoleMutex);
        if (!HasConsole())
            return;

        const std::string line = "[" + Timestamp() + "] " + message + "\r\n";
        DWORD written = 0;
        WriteConsoleA(consoleOutput, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    }

    void ToggleConsole()
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        if (HasConsole())
        {
            CloseHandle(consoleOutput);
            consoleOutput = INVALID_HANDLE_VALUE;
            FreeConsole();
            return;
        }

        if (!AllocConsole())
            return;

        SetConsoleTitleA("StripCol Debug");
        ProtectHostProcess();
        consoleOutput = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    }

    bool IsConsoleOpen()
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        return HasConsole();
    }
}
