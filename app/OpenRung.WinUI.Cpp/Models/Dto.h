#pragma once
// Every shape below mirrors API-CONTRACT.md field-for-field. Do not rename
// JSON keys without updating the contract.
#include "pch.h"
#include <winrt/Windows.Data.Json.h>

namespace Services
{
    // --- encoding helpers --------------------------------------------------
    std::wstring Utf8ToWide(std::string_view utf8);
    std::string WideToUtf8(std::wstring_view wide);

    // --- RFC3339 ------------------------------------------------------------
    // Parses "2026-09-09T12:34:56.789+08:00" / "...Z"; returns false on junk.
    bool TryParseRfc3339(std::wstring const& text, std::chrono::system_clock::time_point& out);

    // --- contract DTOs ------------------------------------------------------
    struct ProxyInfo
    {
        std::wstring host = L"127.0.0.1";
        int port = 0;
    };

    struct ConnectionSnapshot
    {
        std::wstring relayId;
        std::wstring label;
        std::wstring countryCode;
        std::wstring transport; // direct | punch | wss
        std::wstring frontId;
        std::optional<std::chrono::system_clock::time_point> startedAt;
    };

    struct StateSnapshot
    {
        std::wstring status = L"disconnected";
        std::optional<std::wstring> relayLabel;
        std::optional<std::wstring> lastError;
        std::wstring mode = L"proxy"; // proxy | tun
        std::optional<ProxyInfo> proxy;
        std::optional<ConnectionSnapshot> connection;
        bool elevated = false;
        std::wstring coreVersion;
        std::wstring systemProxy; // "host:port" / PAC / "" when none
    };

    struct RelayInfo
    {
        std::wstring id;
        std::wstring label;
        std::wstring country;
        std::wstring countryCode;
        std::wstring city;
        std::wstring nodeClass; // foundation | volunteer
        bool punchCapable = false;
        int maxMbps = 0;
        std::optional<long> latencyMs; // null: not probed / probe failed
        // Client-side latency measurements (POST /api/tcping, /api/real-delay);
        // present only after a test ran. -1 marks a failed rung.
        std::optional<long> tcpingMs;
        std::optional<long> realMs;
    };

    // One relay's TCP handshake measurement (POST /api/tcping).
    struct TcpingResult
    {
        std::wstring relayId;
        std::wstring host;
        int port = 0;
        std::optional<long> avgMs; // null: every sample failed
        int loss = 0;
        int samples = 0;
    };

    // One relay's through-tunnel HTTP latency (POST /api/real-delay).
    struct RealDelayResult
    {
        std::wstring relayId;
        std::optional<long> ms;
        std::wstring error;
    };

    struct LogLine
    {
        std::wstring time;
        std::wstring line;
    };

    struct VersionInfo
    {
        std::wstring core;
        std::wstring engine;
        std::wstring os;
        std::wstring arch;
        bool elevated = false;
    };

    struct CoreEndpoint
    {
        int port = 0;
        std::wstring token;
        int pid = 0;
    };

    // --- JSON (Windows.Data.Json) --------------------------------------------
    StateSnapshot ParseState(winrt::Windows::Data::Json::JsonObject const& json);
    VersionInfo ParseVersion(winrt::Windows::Data::Json::JsonObject const& json);
    std::vector<RelayInfo> ParseRelays(winrt::Windows::Data::Json::JsonObject const& json,
        std::wstring& serverTimeOut);
    std::vector<LogLine> ParseLogs(winrt::Windows::Data::Json::JsonObject const& json);
    CoreEndpoint ParseEndpoint(winrt::Windows::Data::Json::JsonObject const& json);

    // Error envelope {"error": "...", "code": "..."}; either may be absent.
    void ParseErrorEnvelope(std::wstring const& body, std::wstring& errorOut, std::wstring& codeOut);

    // --- typed API errors -----------------------------------------------------
    class CoreApiException : public std::runtime_error
    {
    public:
        CoreApiException(unsigned status, std::wstring code, std::wstring message)
            : std::runtime_error(WideToUtf8(message)), m_status(status), m_code(std::move(code)) {}

        unsigned Status() const noexcept { return m_status; }
        std::wstring const& Code() const noexcept { return m_code; }
        std::wstring WideMessage() const { return Utf8ToWide(what()); }

    private:
        unsigned m_status;
        std::wstring m_code;
    };

    // HTTP 428 with code "elevation_required": restart the core elevated.
    class ElevationRequiredException : public CoreApiException
    {
    public:
        explicit ElevationRequiredException(std::wstring message)
            : CoreApiException(428, L"elevation_required", std::move(message)) {}
    };

    // Connection-level failure (core dead / restarting; no HTTP response).
    class HttpTransportException : public std::runtime_error
    {
    public:
        explicit HttpTransportException(std::wstring message)
            : std::runtime_error(WideToUtf8(message)) {}
        std::wstring WideMessage() const { return Utf8ToWide(what()); }
    };
}
