#pragma once
#include "pch.h"
#include "Http.h"

namespace Services
{
    /// One parsed SSE frame from the core's /api/events stream.
    struct EventItem
    {
        std::wstring name; // the "event:" line (trimmed)
        std::string data;  // the "data:" payload (UTF-8 JSON)
    };

    /// Streams GET /api/events (SSE) synchronously: parses event:/data: frames
    /// and yields them until the stream drops or `stop` is set. Returns
    /// normally on a clean server-side close; auto-reconnect is the caller's
    /// job (CoreSupervisor runs it with backoff).
    class CoreEventsClient
    {
    public:
        CoreEventsClient(unsigned short port, std::wstring token)
            : m_port(port), m_token(std::move(token)) {}

        void Run(std::function<void(EventItem const&)> onEvent, std::atomic<bool> const& stop) const;

    private:
        unsigned short m_port;
        std::wstring m_token;
    };
}
