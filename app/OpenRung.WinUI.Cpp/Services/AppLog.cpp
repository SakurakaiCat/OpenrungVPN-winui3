#include "pch.h"
#include "AppLog.h"
#include "AppSettings.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Dispatching;
using namespace winrt::Microsoft::UI::Xaml;
using namespace Services;

namespace Services
{
    namespace
    {
        DispatcherQueue g_dispatcher{ nullptr };
    }

    namespace Ui
    {
        void Attach(DispatcherQueue const& queue) { g_dispatcher = queue; }
        DispatcherQueue Dispatcher() { return g_dispatcher; }

        void Post(std::function<void()> fn)
        {
            auto queue = g_dispatcher;
            if (!queue) return; // pre-attach calls are dropped, same as C# AppLog
            queue.TryEnqueue([fn = std::move(fn)] { fn(); });
        }
    }

    LogStore& LogStore::Instance()
    {
        static LogStore instance;
        return instance;
    }

    void LogStore::Attach()
    {
        if (!m_lines)
            m_lines = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> LogStore::Lines() const
    {
        return m_lines;
    }

    void LogStore::Append(std::wstring const& line)
    {
        if (!m_lines) return;
        m_lines.Append(winrt::box_value(line));
        while (m_lines.Size() > MaxLines)
            m_lines.RemoveAt(0);
    }

    void LogStore::Clear()
    {
        if (m_lines) m_lines.Clear();
    }

    void AppLog::Write(std::wstring const& line)
    {
        wchar_t stamp[16];
        SYSTEMTIME now;
        ::GetLocalTime(&now);
        _snwprintf_s(stamp, _TRUNCATE, L"%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);

        std::wstring text = L"[";
        text += stamp;
        text += L"] [app] ";
        text += line;
        Ui::Post([text = std::move(text)] {
            LogStore::Instance().Append(text);
        });
    }

    void AppLog::AppendCoreLine(std::wstring const& time, std::wstring const& line)
    {
        std::wstring text = L"[";
        text += time;
        text += L"] ";
        text += line;
        Ui::Post([text = std::move(text)] {
            LogStore::Instance().Append(text);
        });
    }

    void AppLog::LogCrash(winrt::hresult hr)
    {
        wchar_t buf[32];
        _snwprintf_s(buf, _TRUNCATE, L"HRESULT 0x%08X", static_cast<unsigned>(hr.value));
        LogCrash(WideToUtf8(buf));
    }

    void AppLog::LogCrash(std::string const& what)
    {
        try
        {
            // %LOCALAPPDATA%\OpenRung\last-crash.txt
            std::wstring dir = AppSettings::LocalDataDir();
            ::CreateDirectoryW(dir.c_str(), nullptr);
            std::wstring path = dir + L"\\last-crash.txt";

            HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;

            SYSTEMTIME now;
            ::GetLocalTime(&now);
            wchar_t stamp[32];
            _snwprintf_s(stamp, _TRUNCATE, L"%04u-%02u-%02uT%02u:%02u:%02u",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

            std::string payload = "[";
            payload += WideToUtf8(stamp);
            payload += "] ";
            payload += what;
            payload += "\r\n\r\n";
            DWORD written = 0;
            ::WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
            ::CloseHandle(file);
        }
        catch (...)
        {
            // Crash logging must never itself throw.
        }
    }
}
