#include "pch.h"
#include "UpdateCheck.h"
#include "Http.h"
#include "../Models/Dto.h"

namespace Services
{
    namespace
    {
        /// Numeric compare of two v-prefixed version strings; a pre-release
        /// suffix (-alpha/-rc/...) ranks BELOW the bare release. Returns
        /// negative/zero/positive like std::wcscmp semantics.
        int CompareVersions(std::wstring a, std::wstring b)
        {
            auto strip = [](std::wstring& v) {
                if (!v.empty() && (v[0] == L'v' || v[0] == L'V'))
                    v.erase(0, 1);
            };
            strip(a);
            strip(b);

            // Pre-release suffix: everything from the first '-'.
            auto suffix = [](std::wstring const& v) {
                auto p = v.find(L'-');
                return p == std::wstring::npos ? L"" : v.substr(p);
            };
            auto base = [](std::wstring const& v) {
                auto p = v.find(L'-');
                return p == std::wstring::npos ? v : v.substr(0, p);
            };

            auto parseTriple = [](std::wstring const& v) {
                std::array<unsigned long, 3> parts{};
                std::wstringstream ss(v);
                std::wstring item;
                for (auto& part : parts)
                {
                    if (!std::getline(ss, item, L'.'))
                        break;
                    part = std::wcstoul(item.c_str(), nullptr, 10);
                }
                return parts;
            };

            auto pa = parseTriple(base(a));
            auto pb = parseTriple(base(b));
            for (size_t i = 0; i < pa.size(); ++i)
                if (pa[i] != pb[i])
                    return pa[i] < pb[i] ? -1 : 1;

            // Equal numerics: bare release outranks pre-release.
            auto sa = suffix(a);
            auto sb = suffix(b);
            if (sa.empty() && sb.empty()) return 0;
            if (sa.empty()) return 1;
            if (sb.empty()) return -1;
            return sa.compare(sb); // best effort among pre-releases
        }

        /// Minimal extractor for "key": "value" at any nesting — enough for
        /// tag_name / html_url in the GitHub release JSON.
        std::wstring JsonString(std::string const& json, char const* key)
        {
            std::string needle = "\"" + std::string(key) + "\"";
            auto k = json.find(needle);
            if (k == std::string::npos) return {};
            auto colon = json.find(':', k + needle.size());
            if (colon == std::string::npos) return {};
            auto open = json.find('"', colon);
            if (open == std::string::npos) return {};
            std::string value;
            for (auto i = open + 1; i < json.size() && json[i] != '"'; ++i)
            {
                if (json[i] == '\\' && i + 1 < json.size())
                {
                    ++i;
                    if (json[i] == 'u')
                        i += 4; // skip \uXXXX; release URLs/tags are ASCII
                    continue;
                }
                value += json[i];
            }
            return Utf8ToWide(value);
        }
    }

    std::optional<UpdateInfo> CheckForUpdate()
    {
        std::wstring path = std::wstring(L"/repos/") + UpdateRepo + L"/releases/latest";
        auto resp = Http::Request(
            L"api.github.com", 443, L"GET", path,
            L"User-Agent: OpenRung-WinUI\r\n"
            L"Accept: application/vnd.github+json\r\n",
            {}, 10000, /*secure*/ true);

        if (resp.status == 404)
            return std::nullopt; // no releases yet
        if (resp.status != 200)
            throw std::runtime_error("GitHub API HTTP " + std::to_string(resp.status));

        auto tag = JsonString(resp.body, "tag_name");
        auto page = JsonString(resp.body, "html_url");
        if (tag.empty())
            throw std::runtime_error("GitHub API response missing tag_name");

        if (CompareVersions(tag, AppVersion) <= 0)
            return std::nullopt;

        return UpdateInfo{ tag, page.empty() ? std::wstring(L"https://github.com/") + UpdateRepo + L"/releases/latest"
                                             : page };
    }
}
