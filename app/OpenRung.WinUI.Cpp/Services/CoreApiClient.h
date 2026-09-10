#pragma once
#include "pch.h"
#include "Http.h"
#include "../Models/Dto.h"

namespace Services
{
    /// Typed client for the core's loopback JSON API (API-CONTRACT.md).
    /// Uses WinHTTP with WINHTTP_ACCESS_TYPE_NO_PROXY so loopback management
    /// traffic never traverses the OS proxy (the core IS the proxy; proxying
    /// API calls into it yields 405s). Blocking; call from worker threads.
    class CoreApiClient
    {
    public:
        CoreApiClient(unsigned short port, std::wstring token)
            : m_port(port), m_token(std::move(token)) {}

        unsigned short Port() const { return m_port; }
        /// PID of the core process this client talks to (for diagnostics).
        int ExpectedPid = 0;

        VersionInfo GetVersion();
        StateSnapshot GetState();
        std::vector<LogLine> GetLogs(int tail = 500);
        std::wstring GetRelays(std::vector<RelayInfo>& relaysOut); // returns serverTime
        // Parallel TCP handshake test (empty ids = every usable relay).
        std::vector<TcpingResult> Tcping(std::vector<std::wstring> const& relayIds, int samples = 3);
        // Through-tunnel HTTP probe; empty relayId = the live session's relay.
        // Never throws for a failed rung — the error lands in .error.
        RealDelayResult RealDelay(std::wstring const& relayId);
        void Connect(std::wstring const& brokerUrl, std::wstring const& relayId, std::wstring const& country);
        void Disconnect();
        std::optional<std::wstring> SetMode(std::wstring const& mode); // echoed mode when provided
        Services::DnsConfig GetDns();
        void SetDns(std::vector<std::wstring> const& servers, bool ipv6);
        void Heartbeat();
        void Shutdown();
        void ClearSystemProxy();
        Http::Response Post(std::wstring const& path, std::string const& body,
            unsigned timeoutMs);

    private:
        unsigned short m_port;
        std::wstring m_token;
        std::atomic<bool> m_405DiagDone{ false };

        Http::Response Send(std::wstring const& verb, std::wstring const& path,
            std::string const& body, unsigned timeoutMs);
        std::wstring GetJson(std::wstring const& path);
        void EnsureSuccess(Http::Response const& resp, std::wstring const& method, std::wstring const& path);
    };
}
