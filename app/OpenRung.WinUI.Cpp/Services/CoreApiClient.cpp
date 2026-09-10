#include "pch.h"
#include "../Models/Dto.h"
#include "CoreApiClient.h"
#include "AppLog.h"
#include "InterceptionDiagnostics.h"
#include "Localization.h"

using namespace winrt::Windows::Data::Json;
using namespace Services;

namespace Services
{
    namespace
    {
        constexpr unsigned DefaultTimeoutMs = 100000; // C# parity: 100s

        std::wstring JsonOf(std::initializer_list<std::pair<std::wstring, std::wstring>> fields)
        {
            JsonObject obj;
            for (auto const& [key, value] : fields)
                obj.SetNamedValue(key, JsonValue::CreateStringValue(value));
            return std::wstring{ obj.Stringify() };
        }

        std::wstring ErrorMessage(unsigned status, std::wstring const& statusText,
            std::wstring const& bodyWide)
        {
            std::wstring error, code;
            ParseErrorEnvelope(bodyWide, error, code);
            if (!error.empty()) return error;
            if (!code.empty()) return code;
            wchar_t buf[64];
            _snwprintf_s(buf, _TRUNCATE, L"core returned HTTP %u %s", status, statusText.c_str());
            return buf;
        }
    }

    Http::Response CoreApiClient::Send(std::wstring const& verb, std::wstring const& path,
        std::string const& body, unsigned timeoutMs)
    {
        std::wstring headers = L"X-OpenRung-Token: " + m_token + L"\r\n";
        return Http::Request(L"127.0.0.1", m_port, verb, path, headers, body, timeoutMs);
    }

    void CoreApiClient::EnsureSuccess(Http::Response const& resp, std::wstring const& method,
        std::wstring const& path)
    {
        if (resp.status >= 200 && resp.status < 300)
        {
            // One compact line per dispatch (skip heartbeat's 5s cadence).
            if (method == L"POST" && path != L"/api/heartbeat")
            {
                wchar_t buf[64];
                _snwprintf_s(buf, _TRUNCATE, L"API POST %s -> %u", path.c_str(), resp.status);
                AppLog::Write(buf);
            }
            return;
        }

        auto bodyWide = Utf8ToWide(resp.body);
        std::wstring message, code;
        ParseErrorEnvelope(bodyWide, message, code);

        // A 405 from the core on a POST-only endpoint is never the app's own
        // doing: the request reached the core with a rewritten method, which
        // only a transparent loopback interceptor (another proxy/VPN client's
        // TUN or WFP redirect mode) can produce. Report it, once per client.
        if (resp.status == 405 && method == L"POST" && !m_405DiagDone.exchange(true))
        {
            InterceptionDiagnostics::Report(L"http://127.0.0.1:" + std::to_wstring(m_port),
                m_token, ExpectedPid, resp);
        }

        wchar_t buf[192];
        _snwprintf_s(buf, _TRUNCATE, L"API %s %s -> %u %s: %s", method.c_str(), path.c_str(),
            resp.status, resp.statusText.c_str(),
            (!message.empty() ? message : code).c_str());
        AppLog::Write(buf);

        if (message.empty())
        {
            wchar_t fallback[64];
            _snwprintf_s(fallback, _TRUNCATE, L"core returned HTTP %u", resp.status);
            message = fallback;
        }
        if (resp.status == 428 || code == L"elevation_required")
            throw ElevationRequiredException(message);

        if (resp.status == 405 && method == L"POST")
        {
            message = I18n::Tr(L"dlg.intercepted405", method + L" " + path);
        }

        throw CoreApiException(resp.status, code, message);
    }

    std::wstring CoreApiClient::GetJson(std::wstring const& path)
    {
        Http::Response resp;
        try
        {
            resp = Send(L"GET", path, {}, DefaultTimeoutMs);
        }
        catch (Http::TransportError const& e)
        {
            AppLog::Write(L"API GET " + path + L" failed: " + Utf8ToWide(e.what()));
            throw;
        }
        EnsureSuccess(resp, L"GET", path);
        return Utf8ToWide(resp.body);
    }

    Http::Response CoreApiClient::Post(std::wstring const& path, std::string const& body,
        unsigned timeoutMs)
    {
        Http::Response resp;
        try
        {
            resp = Send(L"POST", path, body, timeoutMs);
        }
        catch (Http::TransportError const& e)
        {
            AppLog::Write(L"API POST " + path + L" failed: " + Utf8ToWide(e.what()));
            throw;
        }
        EnsureSuccess(resp, L"POST", path);
        return resp;
    }

    VersionInfo CoreApiClient::GetVersion()
    {
        JsonObject obj;
        if (!JsonObject::TryParse(GetJson(L"/api/version"), obj))
            throw CoreApiException(200, {}, L"invalid JSON from /api/version");
        return ParseVersion(obj);
    }

    StateSnapshot CoreApiClient::GetState()
    {
        JsonObject obj;
        if (!JsonObject::TryParse(GetJson(L"/api/state"), obj))
            throw CoreApiException(200, {}, L"invalid JSON from /api/state");
        return ParseState(obj);
    }

    std::vector<LogLine> CoreApiClient::GetLogs(int tail)
    {
        JsonObject obj;
        if (!JsonObject::TryParse(GetJson(L"/api/logs?tail=" + std::to_wstring(tail)), obj))
            return {};
        return ParseLogs(obj);
    }

    std::wstring CoreApiClient::GetRelays(std::vector<RelayInfo>& relaysOut)
    {
        JsonObject obj;
        if (!JsonObject::TryParse(GetJson(L"/api/relays?ranked=1"), obj))
            throw CoreApiException(200, {}, L"invalid JSON from /api/relays");
        std::wstring serverTime;
        relaysOut = ParseRelays(obj, serverTime);
        return serverTime;
    }

    std::vector<TcpingResult> CoreApiClient::Tcping(std::vector<std::wstring> const& relayIds,
        int samples)
    {
        std::wstring ids;
        for (size_t i = 0; i < relayIds.size(); ++i)
        {
            if (i > 0) ids += L",";
            ids += L"\"" + relayIds[i] + L"\"";
        }
        std::wstring body = L"{\"samples\":" + std::to_wstring(samples);
        if (!relayIds.empty())
            body += L",\"relayIds\":[" + ids + L"]";
        body += L"}";
        // Parallel dials on the core: ~samples x one 1.5 s probe timeout.
        auto resp = Post(L"/api/tcping", WideToUtf8(body), 35000);
        JsonObject obj;
        if (!JsonObject::TryParse(Utf8ToWide(resp.body), obj))
            throw CoreApiException(200, {}, L"invalid JSON from /api/tcping");
        std::vector<TcpingResult> out;
        if (auto arr = obj.TryLookup(L"results"); arr && arr.ValueType() == JsonValueType::Array)
        {
            for (auto const& v : arr.GetArray())
            {
                auto const& o = v.GetObjectW();
                TcpingResult r;
                if (auto s = o.TryLookup(L"relayId"); s && s.ValueType() == JsonValueType::String)
                    r.relayId = std::wstring{ s.GetString() };
                if (auto s = o.TryLookup(L"host"); s && s.ValueType() == JsonValueType::String)
                    r.host = std::wstring{ s.GetString() };
                if (auto n = o.TryLookup(L"port"); n && n.ValueType() == JsonValueType::Number)
                    r.port = static_cast<int>(n.GetNumber());
                if (auto n = o.TryLookup(L"avgMs"); n && n.ValueType() == JsonValueType::Number)
                    r.avgMs = static_cast<long>(n.GetNumber());
                if (auto n = o.TryLookup(L"loss"); n && n.ValueType() == JsonValueType::Number)
                    r.loss = static_cast<int>(n.GetNumber());
                if (auto n = o.TryLookup(L"samples"); n && n.ValueType() == JsonValueType::Number)
                    r.samples = static_cast<int>(n.GetNumber());
                out.push_back(std::move(r));
            }
        }
        return out;
    }

    RealDelayResult CoreApiClient::RealDelay(std::wstring const& relayId)
    {
        RealDelayResult out;
        out.relayId = relayId;
        Http::Response resp;
        try
        {
            std::wstring body;
            if (!relayId.empty())
                body = JsonOf({ {L"relayId", relayId} });
            resp = Send(L"POST", L"/api/real-delay", WideToUtf8(body), 35000);
        }
        catch (CoreApiException const& ex)
        {
            out.error = ex.WideMessage();
            return out;
        }
        EnsureSuccess(resp, L"POST", L"/api/real-delay");
        JsonObject obj;
        if (!JsonObject::TryParse(Utf8ToWide(resp.body), obj))
            throw CoreApiException(200, {}, L"invalid JSON from /api/real-delay");
        if (auto s = obj.TryLookup(L"relayId"); s && s.ValueType() == JsonValueType::String)
            out.relayId = std::wstring{ s.GetString() };
        if (auto n = obj.TryLookup(L"ms"); n && n.ValueType() == JsonValueType::Number)
            out.ms = static_cast<long>(n.GetNumber());
        return out;
    }

    void CoreApiClient::Connect(std::wstring const& brokerUrl, std::wstring const& relayId,
        std::wstring const& country)
    {
        auto body = JsonOf({ {L"brokerUrl", brokerUrl}, {L"relayId", relayId}, {L"country", country} });
        Post(L"/api/connect", WideToUtf8(body), DefaultTimeoutMs);
    }

    void CoreApiClient::Disconnect()
    {
        Post(L"/api/disconnect", {}, DefaultTimeoutMs);
    }

    std::optional<std::wstring> CoreApiClient::SetMode(std::wstring const& mode)
    {
        auto resp = Post(L"/api/mode", WideToUtf8(JsonOf({ {L"mode", mode} })), DefaultTimeoutMs);
        JsonObject obj;
        if (JsonObject::TryParse(Utf8ToWide(resp.body), obj))
        {
            auto val = obj.TryLookup(L"mode");
            if (val && val.ValueType() == JsonValueType::String)
                return std::wstring{ val.GetString() };
        }
        return std::nullopt;
    }

    void CoreApiClient::Heartbeat()
    {
        Post(L"/api/heartbeat", {}, 15000);
    }

    void CoreApiClient::Shutdown()
    {
        Post(L"/api/shutdown", {}, 3000);
    }

    void CoreApiClient::ClearSystemProxy()
    {
        JsonObject obj;
        obj.SetNamedValue(L"clear", JsonValue::CreateBooleanValue(true));
        std::string body = WideToUtf8(obj.Stringify().c_str());
        Post(L"/api/proxy", body, DefaultTimeoutMs);
    }
}
