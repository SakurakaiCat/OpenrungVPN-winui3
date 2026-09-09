#include "pch.h"
#include "TrayIcon.h"
#include <shellapi.h>
#include <strsafe.h>

namespace Services
{
    namespace
    {
        constexpr UINT WM_APP_TRAY = WM_APP + 0x200;
        constexpr GUID kIconGuid = {
            0x2f8d0e2a, 0x1f6d, 0x4b2c,
            { 0x9a, 0x0e, 0x3c, 0x1d, 0x4a, 0x5b, 0x6c, 0x7d } };

        NOTIFYICONDATAW MakeData(HWND hwnd, HICON icon)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = hwnd;
            data.uID = 1;
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            data.uCallbackMessage = WM_APP_TRAY;
            data.hIcon = icon;
            data.uVersion = NOTIFYICON_VERSION_4;
            data.guidItem = kIconGuid;
            return data;
        }
    }

    TrayIcon::TrayIcon()
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &TrayIcon::WndProcThunk;
        wc.lpszClassName = L"OpenRungTrayMessageWindow";
        if (!::RegisterClassExW(&wc))
        {
            auto err = ::GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("RegisterClassExW(tray) failed");
        }
        m_msg = ::CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, nullptr, this);
        if (!m_msg)
            throw std::runtime_error("CreateWindowExW(tray) failed");
    }

    TrayIcon::~TrayIcon()
    {
        Remove();
        if (m_msg)
        {
            ::DestroyWindow(m_msg);
            m_msg = nullptr;
        }
    }

    void TrayIcon::Add(std::wstring const& tooltip)
    {
        auto data = MakeData(m_msg, LoadAppIcon());
        StringCchCopyNW(data.szTip, ARRAYSIZE(data.szTip), tooltip.c_str(), tooltip.size());
        data.uVersion = NOTIFYICON_VERSION_4;
        m_added = ::Shell_NotifyIconW(NIM_ADD, &data) || ::Shell_NotifyIconW(NIM_MODIFY, &data);
        ::Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void TrayIcon::SetTooltip(std::wstring const& tooltip)
    {
        if (!m_added) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_msg;
        data.uID = 1;
        data.uFlags = NIF_TIP;
        data.guidItem = kIconGuid;
        StringCchCopyNW(data.szTip, ARRAYSIZE(data.szTip), tooltip.c_str(), tooltip.size());
        ::Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void TrayIcon::Remove()
    {
        if (!m_added) return;
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_msg;
        data.uID = 1;
        data.guidItem = kIconGuid;
        ::Shell_NotifyIconW(NIM_DELETE, &data);
        m_added = false;
    }

    LRESULT CALLBACK TrayIcon::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<TrayIcon*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        if (msg == WM_APP_TRAY && self)
        {
            auto button = LOWORD(lp);
            if (button == WM_LBUTTONUP && self->LeftClick)
                self->LeftClick();
            else if (button == WM_RBUTTONUP && self->RightClick)
                self->RightClick();
            return 0;
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    HICON TrayIcon::LoadAppIcon()
    {
        wchar_t exePath[MAX_PATH];
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path = exePath;
        auto slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            auto iconPath = path.substr(0, slash + 1) + L"Assets\\app.ico";
            if (::GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                auto h = static_cast<HICON>(::LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                    0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
                if (h) return h;
            }
        }
        return ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    int ShowTrayMenu(HWND owner, std::wstring const& openLabel, std::wstring const& quitLabel)
    {
        HMENU menu = ::CreatePopupMenu();
        if (!menu) return 0;
        ::AppendMenuW(menu, MF_STRING, 1, openLabel.c_str());
        ::AppendMenuW(menu, MF_STRING, 2, quitLabel.c_str());
        POINT pt{};
        ::GetCursorPos(&pt);
        // Required so the menu dismisses on an outside click.
        ::SetForegroundWindow(owner);
        int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, owner, nullptr);
        ::DestroyMenu(menu);
        return cmd;
    }
}
