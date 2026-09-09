#pragma once
#include "pch.h"
#include <winhttp.h>

namespace Services::Http
{
    struct Response
    {
        unsigned status = 0;
        std::wstring statusText;
        std::string body; // raw bytes; JSON is UTF-8
        /// Raw header lines (name: value), for 405 forensics (Allow/Date/Server/Via).
        std::vector<std::wstring> rawHeaders;

        std::wstring Header(std::wstring const& nameLower) const
        {
            for (auto const& line : rawHeaders)
            {
                auto colon = line.find(L':');
                if (colon == std::wstring::npos) continue;
                auto n = line.substr(0, colon);
                std::transform(n.begin(), n.end(), n.begin(), ::towlower);
                if (n == nameLower)
                {
                    auto v = line.substr(colon + 1);
                    auto start = v.find_first_not_of(L" \t");
                    return start == std::wstring::npos ? L"" : v.substr(start);
                }
            }
            return {};
        }
    };

    /// Thrown when no HTTP response exists at all (connect refused, reset,
    /// timeout). Distinct from CoreApiException which means the core replied.
    struct TransportError : std::runtime_error
    {
        explicit TransportError(std::string detail) : std::runtime_error(std::move(detail)) {}
    };

    /// One synchronous request to a loopback HTTP/1.1 server, always bypassing
    /// the OS proxy (the core IS the proxy; routing API traffic through it loops
    /// straight into its 405 rejections).
    ///
    /// timeoutMs caps the whole exchange; pass 0 for no cap (long-lived reads
    /// such as the SSE stream, which rely on server keepalives).
    Response Request(
        std::wstring const& host,
        unsigned short port,
        std::wstring const& verb,
        std::wstring const& path,
        std::wstring const& extraHeaders, // CRLF-terminated, may be empty
        std::string const& body,          // may be empty
        unsigned timeoutMs,
        bool secure = false);             // HTTPS (github API / release check)

    /// Open a streaming GET: returns connected handles; caller reads with
    /// StreamRead and closes with StreamClose. Throws TransportError.
    struct Stream
    {
        HINTERNET session = nullptr;
        HINTERNET connect = nullptr;
        HINTERNET request = nullptr;
    };

    Stream StreamOpen(std::wstring const& host, unsigned short port,
        std::wstring const& path, std::wstring const& extraHeaders);
    /// Appends up to `capacity` bytes; returns false on clean server close or
    /// error (inspect GetLastError: ERROR_WINHTTP_* / 0 == end of stream).
    bool StreamRead(Stream& s, std::string& buffer);
    void StreamClose(Stream& s);
}
