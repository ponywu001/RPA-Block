#pragma once

#include <map>
#include <string>

namespace rpa::core {

struct HttpResponse {
    bool ok = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

/// Minimal outbound HTTP used by the `http_request` step. Backed by WinHTTP on
/// Windows so rpa-core needs no TLS dependency of its own.
HttpResponse httpRequest(const std::string& method,
                         const std::string& url,
                         const std::map<std::string, std::string>& headers,
                         const std::string& body,
                         int timeoutMs = 30000);

}  // namespace rpa::core
