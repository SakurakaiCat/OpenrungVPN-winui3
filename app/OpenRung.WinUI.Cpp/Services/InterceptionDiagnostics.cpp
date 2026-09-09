#include "pch.h"
#include "InterceptionDiagnostics.h"
#include "AppLog.h"
#include <iphlpapi.h>
#include <psapi.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace Services::InterceptionDiagnostics
{
    namespace
    {
        constexpr int kTcpTableOwnerPidAll = 5;

        std::wstring DescribeProcess(int pid)
        {
            std::wstring name = L"进程已退出或无权访问";
            HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                static_cast<DWORD>(pid));
            if (process)
            {
                wchar_t path[MAX_PATH]{};
                DWORD size = MAX_PATH;
                if (::QueryFullProcessImageNameW(process, 0, path, &size))
                {
                    auto* base = wcsrchr(path, L'\\');
                    name = base ? base + 1 : path;
                }
                else
                {
                    wchar_t base[MAX_PATH]{};
                    if (::GetProcessImageFileNameW(process, base, MAX_PATH))
                    {
                        auto* tail = wcsrchr(base, L'\\');
                        name = tail ? tail + 1 : base;
                    }
                }
                ::CloseHandle(process);
            }
            return L"PID " + std::to_wstring(pid) + L" (" + name + L")";
        }

        std::wstring StateName(DWORD state)
        {
            switch (state)
            {
            case MIB_TCP_STATE_LISTEN: return L"LISTEN";
            case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
            case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
            case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
            case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
            default: return L"state=" + std::to_wstring(state);
            }
        }

        void ReportResponseShape(Http::Response const& resp)
        {
            try
            {
                auto allow = resp.Header(L"allow");
                auto server = resp.Header(L"server");
                auto date = resp.Header(L"date");
                auto via = resp.Header(L"via");
                auto shape = !allow.empty()
                    ? L"带 Allow 头→确实出自核心的 methodOnly,请求到达核心时方法已不是 POST"
                    : L"不带 Allow 头→405 出自别的程序(sing-box 入站等)";
                AppLog::Write(L"[诊断] 405 响应形态: Allow='" + allow + L"' Server='" + server +
                    L"' Date='" + date + L"' Via='" + via + L"' → " + shape);
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] 响应形态读取失败: " + Utf8ToWide(ex.what())); }
        }

        void ReportTcpOwners(int port, int expectedCorePid)
        {
            try
            {
                ULONG size = 0;
                auto rc = ::GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                    static_cast<TCP_TABLE_CLASS>(kTcpTableOwnerPidAll), 0);
                if (rc != ERROR_INSUFFICIENT_BUFFER) return;
                std::vector<BYTE> buffer(size);
                rc = ::GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET,
                    static_cast<TCP_TABLE_CLASS>(kTcpTableOwnerPidAll), 0);
                if (rc != NO_ERROR)
                {
                    AppLog::Write(L"[诊断] TCP 表查询失败: rc=" + std::to_wstring(rc));
                    return;
                }
                auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i)
                {
                    auto const& row = table->table[i];
                    // Ports are stored big-endian in the u_short fields.
                    int localPort = ntohs(static_cast<u_short>(row.dwLocalPort & 0xFFFF));
                    int remotePort = ntohs(static_cast<u_short>(row.dwRemotePort & 0xFFFF));
                    // The address dwords are network-order; 127.0.0.1 appears as
                    // first byte 127 on both ends for loopback traffic.
                    bool loopLocal = (row.dwLocalAddr & 0xFF) == 127;
                    bool loopRemote = (row.dwRemoteAddr & 0xFF) == 127;
                    bool localIsPort = (localPort == port) && loopLocal;
                    bool remoteIsPort = (remotePort == port) && loopRemote;
                    if (!localIsPort && !remoteIsPort) continue;

                    auto who = DescribeProcess(static_cast<int>(row.dwOwningPid));
                    if (localIsPort && row.dwState == MIB_TCP_STATE_LISTEN)
                    {
                        auto alarm = (expectedCorePid != 0 &&
                                static_cast<int>(row.dwOwningPid) != expectedCorePid)
                            ? L" —— 它不是核心进程,监听被劫持!"
                            : L"";
                        AppLog::Write(L"[诊断] 端口 " + std::to_wstring(port) + L" 监听者: " + who + alarm);
                    }
                    else
                    {
                        AppLog::Write(L"[诊断]   连接 127.0.0.1:" + std::to_wstring(localPort) +
                            L" → 127.0.0.1:" + std::to_wstring(remotePort) +
                            L" (" + StateName(row.dwState) + L"): " + who);
                    }
                }
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] TCP 表查询失败: " + Utf8ToWide(ex.what())); }
        }

        void ReportInjectedModules()
        {
            try
            {
                wchar_t windowsDir[MAX_PATH]{};
                ::GetWindowsDirectoryW(windowsDir, MAX_PATH);
                std::wstring windows = windowsDir;
                std::transform(windows.begin(), windows.end(), windows.begin(), ::towlower);

                wchar_t exePath[MAX_PATH]{};
                ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                std::wstring appDir = exePath;
                auto slash = appDir.find_last_of(L"\\/");
                appDir = slash == std::wstring::npos ? L"." : appDir.substr(0, slash + 1);
                std::transform(appDir.begin(), appDir.end(), appDir.begin(), ::towlower);

                HMODULE modules[512];
                DWORD needed = 0;
                if (!::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &needed))
                    return;
                DWORD count = (std::min)(needed, static_cast<DWORD>(sizeof(modules))) / sizeof(HMODULE);
                std::vector<std::wstring> suspects;
                for (DWORD i = 0; i < count; ++i)
                {
                    wchar_t modPath[MAX_PATH]{};
                    if (!::GetModuleFileNameExW(::GetCurrentProcess(), modules[i], modPath, MAX_PATH))
                        continue;
                    std::wstring path = modPath;
                    std::wstring lower = path;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                    if (lower.rfind(windows, 0) == 0 || lower.rfind(appDir, 0) == 0)
                        continue;
                    // Skip the Windows App SDK / VC++ runtime that travels with us
                    // outside the app dir (self-contained unpackaged deployment).
                    auto fn = path.substr(path.find_last_of(L"\\/") + 1);
                    std::wstring fnLower = fn;
                    std::transform(fnLower.begin(), fnLower.end(), fnLower.begin(), ::towlower);
                    suspects.push_back(fn + L" @ " + path);
                }
                if (suspects.empty())
                    AppLog::Write(L"[诊断] 本进程无可疑注入 DLL(均在系统/运行时/应用目录下)");
                else
                {
                    AppLog::Write(L"[诊断] 本进程加载了 " + std::to_wstring(suspects.size()) +
                        L" 个非系统/非应用 DLL(注入嫌疑):");
                    for (auto const& s : suspects)
                        AppLog::Write(L"[诊断]    " + s);
                }
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] 进程模块枚举失败: " + Utf8ToWide(ex.what())); }
        }

        void ReportSystemProxy()
        {
            try
            {
                HKEY key = nullptr;
                if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                        0, KEY_READ, &key) != ERROR_SUCCESS)
                    return;
                auto readSz = [key](wchar_t const* name) -> std::wstring {
                    DWORD type = 0, size = 0;
                    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
                        type != REG_SZ)
                        return {};
                    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
                    if (::RegQueryValueExW(key, name, nullptr, nullptr,
                            reinterpret_cast<BYTE*>(value.data()), &size) != ERROR_SUCCESS)
                        return {};
                    value.resize(wcslen(value.c_str()));
                    return value;
                };
                DWORD enable = 0, dwordSize = sizeof(enable);
                bool haveEnable = ::RegQueryValueExW(key, L"ProxyEnable", nullptr, nullptr,
                    reinterpret_cast<BYTE*>(&enable), &dwordSize) == ERROR_SUCCESS;
                auto server = readSz(L"ProxyServer");
                auto pac = readSz(L"AutoConfigURL");
                ::RegCloseKey(key);
                AppLog::Write(L"[诊断] 系统代理: enable=" +
                    std::to_wstring(haveEnable ? enable : 0) +
                    L", server=" + (server.empty() ? L"无" : server) +
                    L", pac=" + (pac.empty() ? L"无" : pac) +
                    L"(API 客户端本就不走系统代理,仅供核对)");
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] 注册表读取失败: " + Utf8ToWide(ex.what())); }
        }

        /// Raw-socket control POST: hand-written HTTP/1.1 over a plain Winsock
        /// socket from THIS process. Separates "below WinHTTP" (raw also 405)
        /// from "only the HTTP stack" (raw 204).
        void ReportRawSocketControl(int port, std::wstring const& token)
        {
            try
            {
                WSADATA wsa{};
                if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                    throw std::runtime_error("WSAStartup failed");
                SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (sock == INVALID_SOCKET)
                    throw std::runtime_error("socket() failed");
                DWORD timeout = 5000;
                ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(static_cast<u_short>(port));
                ::InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);
                if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
                {
                    ::closesocket(sock);
                    throw std::runtime_error("connect failed");
                }
                std::string tokenUtf8 = WideToUtf8(token);
                std::string request =
                    "POST /api/heartbeat HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                    "\r\nX-OpenRung-Token: " + tokenUtf8 +
                    "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                ::send(sock, request.data(), static_cast<int>(request.size()), 0);
                char buf[4096];
                int read = ::recv(sock, buf, sizeof(buf) - 1, 0);
                ::closesocket(sock);
                std::string text = read > 0 ? std::string(buf, buf + read) : std::string{};
                auto lineEnd = text.find('\n');
                auto firstLine = lineEnd == std::string::npos ? text : text.substr(0, lineEnd);
                while (!firstLine.empty() && (firstLine.back() == '\r' || firstLine.back() == ' '))
                    firstLine.pop_back();
                auto verdict = firstLine.find("204") != std::string::npos
                    ? L"原始 socket POST 正常 → 改写发生在 WinHTTP/托管层,可换原始 socket 绕过"
                    : L"原始 socket POST 同样被改写 → 拦截位于本进程 Winsock 钩子或 WFP 进程级 callout";
                AppLog::Write(L"[诊断] 本进程原始 socket 对照 → " + Utf8ToWide(firstLine) +
                    L" (" + verdict + L")");
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] 原始 socket 对照失败: " + Utf8ToWide(ex.what())); }
        }

        void ReportCurlControl(std::wstring const& baseAddress, std::wstring const& token)
        {
            try
            {
                // curl builds its own socket stack; if it succeeds from here,
                // the interception is confined to this process.
                std::wstring args =
                    L"-s -o NUL -w POST=%{http_code} -X POST -H \"X-OpenRung-Token: " +
                    token + L"\" " + baseAddress + L"/api/heartbeat";
                // A pipe captures curl's stdout ("POST=204"); CreateProcess,
                // not ShellExecute, because of that.
                HANDLE outRead = nullptr, outWrite = nullptr;
                ::CreatePipe(&outRead, &outWrite, nullptr, 0);
                ::SetHandleInformation(outWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                si.hStdOutput = outWrite;
                si.hStdError = outWrite;
                si.dwFlags = STARTF_USESTDHANDLES;
                PROCESS_INFORMATION pi{};
                std::wstring cmd = L"curl.exe " + args;
                if (!::CreateProcessW(nullptr, const_cast<wchar_t*>(cmd.c_str()), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                {
                    if (outRead) ::CloseHandle(outRead);
                    if (outWrite) ::CloseHandle(outWrite);
                    throw std::runtime_error("无法启动 curl.exe");
                }
                ::CloseHandle(outWrite);
                // Read until exit or 15s cap.
                std::string output;
                char buf[1024];
                auto deadline = ::GetTickCount64() + 15000;
                for (;;)
                {
                    DWORD avail = 0;
                    ::PeekNamedPipe(outRead, nullptr, 0, nullptr, &avail, nullptr);
                    if (avail > 0)
                    {
                        DWORD n = 0;
                        if (::ReadFile(outRead, buf, sizeof(buf), &n, nullptr) && n > 0)
                            output.append(buf, buf + n);
                    }
                    if (::WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
                        break;
                    if (::GetTickCount64() > deadline)
                    {
                        ::TerminateProcess(pi.hProcess, 1);
                        break;
                    }
                }
                // Drain what's left.
                for (;;)
                {
                    DWORD n = 0;
                    if (!::ReadFile(outRead, buf, sizeof(buf), &n, nullptr) || n == 0) break;
                    output.append(buf, buf + n);
                }
                ::CloseHandle(outRead);
                ::CloseHandle(pi.hProcess);
                ::CloseHandle(pi.hThread);
                // Trim whitespace.
                while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' '))
                    output.pop_back();
                auto codeOk = output.find("POST=204") != std::string::npos ||
                    output.find("POST=200") != std::string::npos;
                AppLog::Write(L"[诊断] curl.exe 对照实验 → " + Utf8ToWide(output) +
                    (codeOk
                        ? L" —— 系统级流量正常,改写被局限在本应用进程内"
                        : L" —— curl 也被拦截,改写属于系统级"));
            }
            catch (std::exception const& ex) { AppLog::Write(L"[诊断] curl 对照实验失败: " + Utf8ToWide(ex.what())); }
        }
    }

    void Report(std::wstring const& baseAddress, std::wstring const& token,
        int expectedCorePid, Http::Response const& resp)
    {
        auto colon = baseAddress.rfind(L':');
        int port = colon == std::wstring::npos ? 0 : std::stoi(baseAddress.substr(colon + 1));
        AppLog::Write(L"[诊断] POST 被改写(目标 " + baseAddress + L",期望核心 PID " +
            (expectedCorePid ? std::to_wstring(expectedCorePid) : L"未知") + L")");
        ReportResponseShape(resp);
        ReportTcpOwners(port, expectedCorePid);
        ReportInjectedModules();
        ReportSystemProxy();
        ReportRawSocketControl(port, token);
        ReportCurlControl(baseAddress, token);
    }
}
