#include "pch.h"
#include "StartupLog.h"
#include "AppSettings.h"
#include "../Models/Dto.h"

#include <cstdio>
#include <io.h>
#include <fcntl.h>

using namespace Services;

namespace
{
    HANDLE g_file = INVALID_HANDLE_VALUE;
    bool g_consoleAttached = false;

    void AppendToConsole(std::string const& utf8)
    {
        if (!g_consoleAttached) return;
        // stdout was redirected to the parent console; printf lands there.
        fwrite(utf8.data(), 1, utf8.size(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }

    void AppendToFile(std::string const& utf8)
    {
        if (g_file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        ::WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        ::FlushFileBuffers(g_file);
    }
}

namespace Services
{
    void StartupLog::Init()
    {
        static bool initialized = false;
        if (initialized) return;
        initialized = true;

        // Show output in the calling PowerShell/cmd window. Without a
        // console attachment a GUI subsystem process has no stdout at all.
        if (::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            g_consoleAttached = true;
            // Reopen the standard streams onto the console handles; without
            // this printf goes to the (absent) default stdout.
#pragma warning(push)
#pragma warning(disable : 4996) // _wfreopen deprecation: fine for diagnostics
            _wfreopen(L"CONOUT$", L"w", stdout);
            _wfreopen(L"CONERR$", L"w", stderr);
#pragma warning(pop)
        }

        try
        {
            std::wstring dir = AppSettings::LocalDataDir() + L"\\logs";
            ::CreateDirectoryW(AppSettings::LocalDataDir().c_str(), nullptr);
            ::CreateDirectoryW(dir.c_str(), nullptr);
            g_file = ::CreateFileW((dir + L"\\startup.log").c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        catch (...)
        {
            // Diagnostics must never take the process down.
        }

        Write(std::string("---- startup, pid=") + std::to_string(::GetCurrentProcessId()) + " ----");
    }

    void StartupLog::Write(std::wstring const& line)
    {
        SYSTEMTIME now;
        ::GetLocalTime(&now);
        wchar_t stamp[16];
        _snwprintf_s(stamp, _TRUNCATE, L"%02u:%02u:%02u.%03u",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        std::wstring text = L"[" + std::wstring(stamp) + L"] " + line;
        std::string utf8 = WideToUtf8(text);
        AppendToConsole(utf8);
        AppendToFile(utf8 + "\r\n");
    }

    void StartupLog::Write(std::string const& line)
    {
        Write(Utf8ToWide(line));
    }

    std::wstring StartupLog::Describe(winrt::hresult_error const& ex)
    {
        wchar_t buf[16];
        _snwprintf_s(buf, _TRUNCATE, L"0x%08X", static_cast<unsigned>(ex.code().value));
        return L"exception " + std::wstring(buf) + L" " + std::wstring(ex.message());
    }
}
