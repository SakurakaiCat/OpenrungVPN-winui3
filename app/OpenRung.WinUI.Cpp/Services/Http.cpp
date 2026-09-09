#include "pch.h"
#include "Http.h"
#include "../Models/Dto.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace Services::Http
{
    namespace
    {
        struct Handle
        {
            HINTERNET h = nullptr;
            ~Handle() { if (h) ::WinHttpCloseHandle(h); }
            operator HINTERNET() const { return h; }
            HINTERNET* out() { return &h; }
        };

        [[noreturn]] void ThrowLastError(char const* op)
        {
            DWORD err = ::GetLastError();
            throw TransportError(std::string(op) + " failed (WinHTTP error " + std::to_string(err) + ")");
        }

        std::wstring QueryStringHeader(HINTERNET request, DWORD infoLevel)
        {
            DWORD size = 0;
            ::WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
            if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
            std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
            if (!::WinHttpQueryHeaders(request, infoLevel, WINHTTP_HEADER_NAME_BY_INDEX,
                    buffer.data(), &size, WINHTTP_NO_HEADER_INDEX))
                return {};
            buffer.resize(size / sizeof(wchar_t));
            while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
            return buffer;
        }
    }

    Response Request(
        std::wstring const& host,
        unsigned short port,
        std::wstring const& verb,
        std::wstring const& path,
        std::wstring const& extraHeaders,
        std::string const& body,
        unsigned timeoutMs)
    {
        Handle session{ ::WinHttpOpen(L"OpenRung-WinUI/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session) ThrowLastError("WinHttpOpen");

        if (timeoutMs != 0)
        {
            // resolve/connect/send/receive all get the budget; the core is
            // loopback so connect is instant — the wait is almost always the body.
            ::WinHttpSetTimeouts(session, 5000, 5000, static_cast<int>(timeoutMs),
                static_cast<int>(timeoutMs));
        }

        Handle connect{ ::WinHttpConnect(session, host.c_str(), port, 0) };
        if (!connect) ThrowLastError("WinHttpConnect");

        Handle request{ ::WinHttpOpenRequest(connect, verb.c_str(), path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) };
        if (!request) ThrowLastError("WinHttpOpenRequest");

        if (!extraHeaders.empty() &&
            !::WinHttpAddRequestHeaders(request, extraHeaders.c_str(),
                static_cast<DWORD>(extraHeaders.size()), WINHTTP_ADDREQ_FLAG_ADD))
            ThrowLastError("WinHttpAddRequestHeaders");

        LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
        DWORD bodyLen = static_cast<DWORD>(body.size());
        if (!::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                bodyPtr, bodyLen, bodyLen, 0))
            ThrowLastError("WinHttpSendRequest");

        if (!::WinHttpReceiveResponse(request, nullptr))
            ThrowLastError("WinHttpReceiveResponse");

        Response resp;
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!::WinHttpQueryHeaders(request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
            ThrowLastError("WinHttpQueryHeaders(status)");
        resp.status = status;
        resp.statusText = QueryStringHeader(request, WINHTTP_QUERY_STATUS_TEXT);

        // Header block for the 405 forensics (Allow/Date/Server/Via).
        std::wstring raw = QueryStringHeader(request, WINHTTP_QUERY_RAW_HEADERS_CRLF);
        for (size_t pos = 0; pos < raw.size();)
        {
            size_t eol = raw.find(L"\r\n", pos);
            if (eol == std::wstring::npos) break;
            resp.rawHeaders.push_back(raw.substr(pos, eol - pos));
            pos = eol + 2;
        }

        // Body: fixed Content-Length or chunked; WinHttpReadData normalizes both.
        for (;;)
        {
            DWORD available = 0;
            if (!::WinHttpQueryDataAvailable(request, &available))
                ThrowLastError("WinHttpQueryDataAvailable");
            if (available == 0) break;
            size_t oldSize = resp.body.size();
            resp.body.resize(oldSize + available);
            DWORD read = 0;
            if (!::WinHttpReadData(request, resp.body.data() + oldSize, available, &read))
            {
                resp.body.resize(oldSize);
                ThrowLastError("WinHttpReadData");
            }
            resp.body.resize(oldSize + read);
            if (read == 0) break;
        }
        return resp;
    }

    Stream StreamOpen(std::wstring const& host, unsigned short port,
        std::wstring const& path, std::wstring const& extraHeaders)
    {
        Stream s;
        s.session = ::WinHttpOpen(L"OpenRung-WinUI/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s.session) { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpOpen"); }

        // SSE is a long-lived stream: connect is bounded (15s); the receive
        // timeout is 90s — the core's 15s keepalives always beat it, so a
        // timeout here means the core is wedged and the reconnect logic should
        // take over.
        ::WinHttpSetTimeouts(s.session, 15000, 15000, 30000, 90000);

        s.connect = ::WinHttpConnect(s.session, host.c_str(), port, 0);
        if (!s.connect) { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpConnect"); }

        s.request = ::WinHttpOpenRequest(s.connect, L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!s.request) { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpOpenRequest"); }

        if (!extraHeaders.empty() &&
            !::WinHttpAddRequestHeaders(s.request, extraHeaders.c_str(),
                static_cast<DWORD>(extraHeaders.size()), WINHTTP_ADDREQ_FLAG_ADD))
        { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpAddRequestHeaders"); }


        if (!::WinHttpSendRequest(s.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpSendRequest"); }


        if (!::WinHttpReceiveResponse(s.request, nullptr))
        { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpReceiveResponse"); }


        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!::WinHttpQueryHeaders(s.request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        { auto e = ::GetLastError(); StreamClose(s); ::SetLastError(e); ThrowLastError("WinHttpQueryHeaders"); }

        if (status < 200 || status > 299)
        {
            StreamClose(s);
            ::SetLastError(0);
            throw TransportError("GET " + WideToUtf8(path) + " -> HTTP " + std::to_string(status));
        }
        return s;
    }

    bool StreamRead(Stream& s, std::string& buffer)
    {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(s.request, &available)) return false;
        if (available == 0) return false; // clean end of stream
        size_t oldSize = buffer.size();
        buffer.resize(oldSize + available);
        DWORD read = 0;
        if (!::WinHttpReadData(s.request, buffer.data() + oldSize, available, &read))
        {
            buffer.resize(oldSize);
            return false;
        }
        buffer.resize(oldSize + read);
        return read > 0;
    }

    void StreamClose(Stream& s)
    {
        if (s.request) { ::WinHttpCloseHandle(s.request); s.request = nullptr; }
        if (s.connect) { ::WinHttpCloseHandle(s.connect); s.connect = nullptr; }
        if (s.session) { ::WinHttpCloseHandle(s.session); s.session = nullptr; }
    }
}
