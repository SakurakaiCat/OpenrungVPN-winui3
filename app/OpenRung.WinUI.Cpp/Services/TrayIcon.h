#pragma once
#include "pch.h"

namespace Services
{
    /// Minimal Shell_NotifyIcon wrapper for the unpackaged app. A hidden
    /// message-only window receives tray callbacks; left/right clicks are
    /// surfaced as delegates. The icon exists only while the app runs, which
    /// matches the "closing the window hides it to the tray; the engine keeps
    /// running" flow in README.
    class TrayIcon
    {
    public:
        TrayIcon();
        ~TrayIcon(); // Remove + destroy message window

        TrayIcon(TrayIcon const&) = delete;
        TrayIcon& operator=(TrayIcon const&) = delete;

        void Add(std::wstring const& tooltip);
        void SetTooltip(std::wstring const& tooltip);
        void Remove();

        std::function<void()> LeftClick;
        std::function<void()> RightClick;

    private:
        HWND m_msg = nullptr;
        bool m_added = false;

        static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        static HICON LoadAppIcon();
    };

    /// Popup menu for the tray (right-click). Returns 1 = open, 2 = quit,
    /// 0 = dismissed.
    int ShowTrayMenu(HWND owner, std::wstring const& openLabel, std::wstring const& quitLabel);
}
