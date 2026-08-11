#include "rpa/core/Process.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
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

std::wstring expandEnvironment(const std::wstring& value) {
    if (value.empty()) return {};
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) return value;
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
    if (written == 0) return value;
    expanded.resize(written > 0 ? written - 1 : 0);  // drop the trailing NUL
    return expanded;
}

/// ShellExecuteEx wants COM up on the calling thread, and the executor runs on a
/// worker thread that has not initialised it. Tolerates a thread that already
/// picked a different apartment: that is someone else's initialisation to undo.
class ComScope {
public:
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owns_ = SUCCEEDED(hr);
    }
    ~ComScope() { if (owns_) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool owns_ = false;
};

std::string describeShellError(DWORD code, const std::string& path) {
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
            return "application not found: " + path;
        case ERROR_PATH_NOT_FOUND:
            return "path not found: " + path;
        case ERROR_ACCESS_DENIED:
            return "access denied: " + path;
        case ERROR_CANCELLED:
            return "launch cancelled (elevation prompt declined): " + path;
        case SE_ERR_NOASSOC:
            return "no application is associated with: " + path;
        default:
            return "cannot launch " + path + " (error " + std::to_string(code) + ")";
    }
}

}  // namespace

LaunchResult launchApplication(const std::string& path,
                               const std::string& arguments,
                               const std::string& workingDirectory) {
    LaunchResult result;

    if (path.empty()) {
        result.error = "launch_app has no path";
        return result;
    }

    const ComScope com;

    const std::wstring file = expandEnvironment(utf8ToWide(path));
    const std::wstring params = utf8ToWide(arguments);
    const std::wstring directory = expandEnvironment(utf8ToWide(workingDirectory));

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // NOCLOSEPROCESS so the pid survives the call; NO_UI so a bad path comes back
    // as an error code instead of a modal box nobody is there to dismiss.
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpParameters = params.empty() ? nullptr : params.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        result.error = describeShellError(GetLastError(), path);
        return result;
    }

    if (info.hProcess) {
        result.processId = GetProcessId(info.hProcess);
        CloseHandle(info.hProcess);
    }

    result.ok = true;
    return result;
}

#else

LaunchResult launchApplication(const std::string&, const std::string&, const std::string&) {
    LaunchResult result;
    result.error = "launch_app is only implemented on Windows";
    return result;
}

#endif  // _WIN32

}  // namespace rpa::core
