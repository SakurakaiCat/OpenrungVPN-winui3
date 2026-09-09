#include "pch.h"
#include "../Models/Dto.h"
#include "CoreEventsClient.h"

namespace Services
{
    namespace
    {
        /// Processes a single SSE line (without its terminator). Mirrors the
        /// C# StreamReader.ReadLine loop in CoreEventsClient.
        struct FrameParser
        {
            std::wstring eventName;
            std::string data;
            bool haveName = false;

            void ProcessLine(std::string_view line,
                std::function<void(std::wstring, std::string)> onFrame)
            {
                if (line.empty())
                {
                    if (haveName && !data.empty())
                    {
                        // Trim any trailing \n accumulated from joined parts.
                        while (!data.empty() && data.back() == '\n') data.pop_back();
                        onFrame(eventName, data);
                    }
                    haveName = false;
                    eventName.clear();
                    data.clear();
                    return;
                }
                if (line.front() == ':')
                    return; // comment/keepalive
                if (line.rfind("event:", 0) == 0)
                {
                    auto rest = line.substr(6);
                    eventName = Utf8ToWide(rest);
                    // Trim whitespace (C# .Trim()).
                    while (!eventName.empty() && (eventName.front() == L' ' || eventName.front() == L'\t'))
                        eventName.erase(0, 1);
                    while (!eventName.empty() && (eventName.back() == L' ' || eventName.back() == L'\t'))
                        eventName.pop_back();
                    haveName = true;
                }
                else if (line.rfind("data:", 0) == 0)
                {
                    if (!data.empty()) data.push_back('\n');
                    auto part = line.substr(5);
                    if (!part.empty() && part.front() == ' ') part.remove_prefix(1);
                    data.append(part);
                }
            }
        };
    }

    void CoreEventsClient::Run(std::function<void(EventItem const&)> onEvent,
        std::atomic<bool> const& stop) const
    {
        auto stream = Http::StreamOpen(L"127.0.0.1", m_port, L"/api/events",
            L"X-OpenRung-Token: " + m_token + L"\r\n");

        auto parser = FrameParser{};
        std::string chunk;
        std::string pending;

        for (;;)
        {
            if (stop) break;
            if (!Http::StreamRead(stream, chunk))
                break; // clean server close or error

            pending += chunk;
            chunk.clear();

            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos)
            {
                auto line = pending.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                pending.erase(0, pos + 1);

                parser.ProcessLine(line, [&onEvent](std::wstring name, std::string data) {
                    onEvent(EventItem{ std::move(name), std::move(data) });
                });
                // Reset the lambda's captures are per-call; parser state persists.
            }
        }

        Http::StreamClose(stream);
    }
}
