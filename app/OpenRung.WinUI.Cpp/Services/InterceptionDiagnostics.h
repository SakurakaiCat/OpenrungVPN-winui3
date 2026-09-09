#pragma once
#include "pch.h"
#include "Http.h"

namespace Services::InterceptionDiagnostics
{
    /// When the loopback core answers a POST with 405 while GETs to the same
    /// core succeed, the request method was rewritten in transit (the API
    /// client is proxy-bypass, so only a transparent local interceptor can do
    /// it). This snapshot pins the culprit where it can:
    ///   1. the verbatim 405 response (a genuine core 405 carries Allow: POST
    ///      and a Go Date header; anything else didn't come from the mux);
    ///   2. TCP table ownership via iphlpapi (which PID owns the core port's
    ///      listener and the app's connections — a WFP redirect shows up here);
    ///   3. DLLs injected into THIS process (LSP/hook-style writers);
    ///   4. the system proxy registry state (cross-check only);
    ///   5. an in-process raw-socket control POST (splits "below WinHTTP" from
    ///      "below the process");
    ///   6. a curl.exe control POST (the system-wide baseline).
    ///
    /// expectedCorePid: 0 = unknown. Runs synchronously; call from a worker
    /// thread.
    void Report(std::wstring const& baseAddress, std::wstring const& token,
        int expectedCorePid, Http::Response const& resp);
}
