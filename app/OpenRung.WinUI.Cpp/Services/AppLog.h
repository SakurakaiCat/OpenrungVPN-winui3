#pragma once
#include "pch.h"
#include "../Models/Dto.h"

namespace Services
{
    /// UI-thread hop: App attaches the window's dispatcher once at startup;
    /// every service that touches UI-bound state posts through here.
    namespace Ui
    {
        void Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue);
        winrt::Microsoft::UI::Dispatching::DispatcherQueue Dispatcher();
        /// Post a callable to the UI thread; must not be called before Attach.
        void Post(std::function<void()> fn);
    }

    /// Shared log lines backing the logs page. Created against the UI thread;
    /// all mutation flows through Append/Clear on the UI thread.
    /// XAML items controls can only enumerate IObservableVector<IInspectable>
    /// (the bindable form), so lines are stored boxed.
    class LogStore
    {
    public:
        static LogStore& Instance();

        void Attach(); // call on the UI thread once (after Ui::Attach)
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> Lines() const;

        void Append(std::wstring const& line); // UI thread only
        void Clear();

        bool AutoScroll() const { return m_autoScroll; }
        void AutoScroll(bool v) { m_autoScroll = v; }

    private:
        static constexpr size_t MaxLines = 2000;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_lines{ nullptr };
        bool m_autoScroll = true;
    };

    /// App-side log lines ([hh:mm:ss] [app] ...) plus crash capture.
    /// Mirrors the C# AppLog: everything also shows on the logs page.
    class AppLog
    {
    public:
        static void Attach(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue)
        {
            Ui::Attach(queue);
            LogStore::Instance().Attach();
        }

        static void Write(std::wstring const& line);

        /// Core engine lines arriving over SSE: "[time] line".
        static void AppendCoreLine(std::wstring const& time, std::wstring const& line);

        /// Best-effort crash capture to %LOCALAPPDATA%\OpenRung\last-crash.txt.
        /// Never throws; safe on any thread including a dying one.
        static void LogCrash(winrt::hresult hr);
        static void LogCrash(std::string const& what);
    };
}
