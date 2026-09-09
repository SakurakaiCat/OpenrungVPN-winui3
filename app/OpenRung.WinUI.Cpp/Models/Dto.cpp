#include "pch.h"
#include "Dto.h"

using namespace winrt::Windows::Data::Json;

namespace Services
{
    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
            static_cast<int>(utf8.size()), nullptr, 0);
        if (len <= 0)
        {
            // Tolerate malformed bytes rather than dropping the line entirely.
            len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                static_cast<int>(utf8.size()), nullptr, 0);
            if (len <= 0) return L"<invalid utf-8>";
            std::wstring out(len, L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                static_cast<int>(utf8.size()), out.data(), len);
            return out;
        }
        std::wstring out(len, L'\0');
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
            static_cast<int>(utf8.size()), out.data(), len);
        return out;
    }

    std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty()) return {};
        int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
            static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            out.data(), len, nullptr, nullptr);
        return out;
    }

    bool TryParseRfc3339(std::wstring const& text, std::chrono::system_clock::time_point& out)
    {
        // 2026-09-09T12:34:56(.fff...) (Z | +07:00 | -05:00)
        unsigned year, month, day, hour, minute, second;
        int consumed = 0;
        if (::swscanf_s(text.c_str(), L"%4u-%2u-%2uT%2u:%2u:%2u%n",
                &year, &month, &day, &hour, &minute, &second, &consumed) < 6 || consumed != 19)
            return false;

        SYSTEMTIME st{};
        st.wYear = static_cast<WORD>(year);
        st.wMonth = static_cast<WORD>(month);
        st.wDay = static_cast<WORD>(day);
        st.wHour = static_cast<WORD>(hour);
        st.wMinute = static_cast<WORD>(minute);
        st.wSecond = static_cast<WORD>(second);
        st.wMilliseconds = 0;

        size_t pos = consumed;
        long long fracTicks = 0; // 100ns units of the fractional part
        if (pos < text.size() && text[pos] == L'.')
        {
            ++pos;
            long long scale = 1000000; // first digit = 0.1s = 1,000,000 ticks
            bool any = false;
            while (pos < text.size() && text[pos] >= L'0' && text[pos] <= L'9')
            {
                if (scale > 0) { fracTicks += (text[pos] - L'0') * scale; scale /= 10; }
                ++pos;
                any = true;
            }
            if (!any) return false;
        }

        long long offsetSeconds = 0;
        if (pos < text.size() && (text[pos] == L'+' || text[pos] == L'-'))
        {
            int sign = text[pos] == L'+' ? 1 : -1;
            ++pos;
            unsigned oh, om;
            int n = 0;
            if (::swscanf_s(text.c_str() + pos, L"%2u:%2u%n", &oh, &om, &n) < 2)
                return false;
            offsetSeconds = sign * static_cast<long long>(oh * 3600 + om * 60);
            pos += n;
            if (pos != text.size()) return false;
        }
        else if (pos < text.size() && text[pos] == L'Z')
        {
            ++pos;
            if (pos != text.size()) return false;
        }
        else
        {
            return false; // RFC3339 mandates an offset
        }

        FILETIME ft{};
        if (!::SystemTimeToFileTime(&st, &ft)) return false;
        ULARGE_INTEGER ui{};
        ui.LowPart = ft.dwLowDateTime;
        ui.HighPart = ft.dwHighDateTime;
        // FILETIME ticks are 100ns since 1601 UTC; subtract the zone offset so
        // the value was originally local-time-with-offset.
        unsigned long long ticks = ui.QuadPart + fracTicks;
        ticks -= static_cast<unsigned long long>(offsetSeconds) * 10000000ULL;

        using namespace std::chrono;
        // 11644473600 seconds between 1601 and 1970.
        auto unix100ns = static_cast<long long>(ticks) - 11644473600LL * 10000000LL;
        out = system_clock::time_point{} + duration_cast<system_clock::duration>(nanoseconds{ unix100ns * 100 });
        return true;
    }

    // -- JSON helpers ----------------------------------------------------------

    static std::wstring OptionalString(JsonObject const& obj, winrt::hstring const& name)
    {
        auto val = obj.TryLookup(name);
        if (val && val.ValueType() == JsonValueType::String)
            return std::wstring{ val.GetString() };
        return {};
    }

    static bool HasString(JsonObject const& obj, winrt::hstring const& name)
    {
        auto val = obj.TryLookup(name);
        return val && val.ValueType() == JsonValueType::String;
    }

    static bool OptBool(JsonObject const& obj, winrt::hstring const& name, bool fallback = false)
    {
        auto val = obj.TryLookup(name);
        return val && val.ValueType() == JsonValueType::Boolean ? val.GetBoolean() : fallback;
    }

    static long OptNumber(JsonObject const& obj, winrt::hstring const& name, long fallback = 0)
    {
        auto val = obj.TryLookup(name);
        return val && val.ValueType() == JsonValueType::Number
            ? static_cast<long>(val.GetNumber()) : fallback;
    }

    StateSnapshot ParseState(JsonObject const& json)
    {
        StateSnapshot s;
        s.status = OptionalString(json, L"status");
        if (s.status.empty()) s.status = L"disconnected";
        if (HasString(json, L"relayLabel")) s.relayLabel = OptionalString(json, L"relayLabel");
        if (HasString(json, L"lastError")) s.lastError = OptionalString(json, L"lastError");
        s.mode = OptionalString(json, L"mode");
        if (s.mode.empty()) s.mode = L"proxy";
        s.systemProxy = OptionalString(json, L"systemProxy");
        s.coreVersion = OptionalString(json, L"coreVersion");
        s.elevated = OptBool(json, L"elevated");

        if (auto val = json.TryLookup(L"proxy");
            val && val.ValueType() == JsonValueType::Object)
        {
            auto obj = val.GetObject();
            ProxyInfo p;
            p.host = OptionalString(obj, L"host");
            p.port = static_cast<int>(OptNumber(obj, L"port"));
            s.proxy = std::move(p);
        }

        if (auto val = json.TryLookup(L"connection");
            val && val.ValueType() == JsonValueType::Object)
        {
            auto obj = val.GetObject();
            ConnectionSnapshot c;
            c.relayId = OptionalString(obj, L"relayId");
            c.label = OptionalString(obj, L"label");
            c.countryCode = OptionalString(obj, L"countryCode");
            c.transport = OptionalString(obj, L"transport");
            c.frontId = OptionalString(obj, L"frontId");
            if (HasString(obj, L"startedAt"))
            {
                std::chrono::system_clock::time_point t;
                if (TryParseRfc3339(OptionalString(obj, L"startedAt"), t))
                    c.startedAt = t;
            }
            s.connection = std::move(c);
        }
        return s;
    }

    VersionInfo ParseVersion(JsonObject const& json)
    {
        VersionInfo v;
        v.core = OptionalString(json, L"core");
        v.engine = OptionalString(json, L"engine");
        v.os = OptionalString(json, L"os");
        v.arch = OptionalString(json, L"arch");
        v.elevated = OptBool(json, L"elevated");
        return v;
    }

    std::vector<RelayInfo> ParseRelays(JsonObject const& json, std::wstring& serverTimeOut)
    {
        serverTimeOut = OptionalString(json, L"serverTime");
        std::vector<RelayInfo> out;
        auto arr = json.TryLookup(L"relays");
        if (!arr || arr.ValueType() != JsonValueType::Array) return out;
        for (auto const& item : arr.GetArray())
        {
            if (item.ValueType() != JsonValueType::Object) continue;
            auto obj = item.GetObject();
            RelayInfo r;
            r.id = OptionalString(obj, L"id");
            r.label = OptionalString(obj, L"label");
            r.country = OptionalString(obj, L"country");
            r.countryCode = OptionalString(obj, L"countryCode");
            r.city = OptionalString(obj, L"city");
            r.nodeClass = OptionalString(obj, L"nodeClass");
            r.punchCapable = OptBool(obj, L"punchCapable");
            r.maxMbps = static_cast<int>(OptNumber(obj, L"maxMbps"));
            auto lat = obj.TryLookup(L"latencyMs");
            if (lat && lat.ValueType() == JsonValueType::Number)
                r.latencyMs = static_cast<long>(lat.GetNumber());
            out.push_back(std::move(r));
        }
        return out;
    }

    std::vector<LogLine> ParseLogs(JsonObject const& json)
    {
        std::vector<LogLine> out;
        auto arr = json.TryLookup(L"logs");
        if (!arr || arr.ValueType() != JsonValueType::Array) return out;
        for (auto const& item : arr.GetArray())
        {
            if (item.ValueType() != JsonValueType::Object) continue;
            auto obj = item.GetObject();
            LogLine l;
            l.time = OptionalString(obj, L"time");
            l.line = OptionalString(obj, L"line");
            out.push_back(std::move(l));
        }
        return out;
    }

    CoreEndpoint ParseEndpoint(JsonObject const& json)
    {
        CoreEndpoint ep;
        ep.port = static_cast<int>(OptNumber(json, L"port"));
        ep.token = OptionalString(json, L"token");
        ep.pid = static_cast<int>(OptNumber(json, L"pid"));
        return ep;
    }

    void ParseErrorEnvelope(std::wstring const& body, std::wstring& errorOut, std::wstring& codeOut)
    {
        errorOut.clear();
        codeOut.clear();
        JsonObject obj;
        if (!JsonObject::TryParse(body, obj)) return;
        errorOut = OptionalString(obj, L"error");
        codeOut = OptionalString(obj, L"code");
    }
}
