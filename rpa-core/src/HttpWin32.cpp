#include "rpa/core/Http.h"

#include <algorithm>
#include <cctype>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace rpa::core {

#ifdef _WIN32

namespace {

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), size);
    return wide;
}

/// RAII for the three WinHTTP handle types, which all close the same way.
class Handle {
public:
    explicit Handle(HINTERNET h = nullptr) : h_(h) {}
    ~Handle() { if (h_) WinHttpCloseHandle(h_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HINTERNET get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

private:
    HINTERNET h_;
};

}  // namespace

HttpResponse httpRequest(const std::string& method,
                         const std::string& url,
                         const std::map<std::string, std::string>& headers,
                         const std::string& body,
                         int timeoutMs) {
    HttpResponse response;

    const std::wstring wideUrl = utf8ToWide(url);

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    wchar_t hostName[256] = {};
    wchar_t urlPath[4096] = {};
    components.lpszHostName = hostName;
    components.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
    components.lpszUrlPath = urlPath;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(urlPath));

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
        response.error = "cannot parse url: " + url;
        return response;
    }

    Handle session(WinHttpOpen(L"RPA-Block/1.0",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        response.error = "WinHttpOpen failed (error " + std::to_string(GetLastError()) + ")";
        return response;
    }
    WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    Handle connect(WinHttpConnect(session.get(), hostName, components.nPort, 0));
    if (!connect) {
        response.error = "WinHttpConnect failed (error " + std::to_string(GetLastError()) + ")";
        return response;
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    const std::wstring wideMethod = utf8ToWide(method.empty() ? "GET" : method);

    Handle request(WinHttpOpenRequest(connect.get(), wideMethod.c_str(), urlPath, nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        response.error = "WinHttpOpenRequest failed (error " + std::to_string(GetLastError()) + ")";
        return response;
    }

    std::wstring headerBlock;
    for (const auto& [key, value] : headers) {
        headerBlock += utf8ToWide(key) + L": " + utf8ToWide(value) + L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        request.get(),
        headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
        headerBlock.empty() ? 0 : static_cast<DWORD>(headerBlock.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        response.error = "WinHttpSendRequest failed (error " + std::to_string(GetLastError()) + ")";
        return response;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        response.error = "WinHttpReceiveResponse failed (error " +
                         std::to_string(GetLastError()) + ")";
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = static_cast<int>(statusCode);

    std::string payload;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) break;
        if (available == 0) break;

        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), chunk.data(), available, &read)) break;
        if (read == 0) break;
        payload.append(chunk.data(), read);
    }

    response.body = std::move(payload);
    response.ok = true;
    return response;
}

#else

HttpResponse httpRequest(const std::string&,
                         const std::string&,
                         const std::map<std::string, std::string>&,
                         const std::string&,
                         int) {
    HttpResponse response;
    response.error = "http_request is only implemented on Windows";
    return response;
}

#endif  // _WIN32

}  // namespace rpa::core
