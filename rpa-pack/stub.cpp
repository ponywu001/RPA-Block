/// Self-extracting launcher stub.
///
/// The packaged exe is laid out as:
///
///     [ this stub ][ payload.zip ][ uint64 payload size ][ 8-byte magic ]
///
/// On launch the stub finds its own appended payload, unpacks it into a
/// per-version folder under %LOCALAPPDATA%, and starts the real application.
/// The extract directory is keyed by payload size + a checksum, so the second
/// and later launches skip unpacking entirely and start immediately.
///
/// Extraction shells out to the `tar` that ships with Windows 10 1803 and newer
/// (bsdtar, which reads zip) rather than linking an archive library, keeping the
/// stub small and dependency-free.

// WIN32_LEAN_AND_MEAN / NOMINMAX come from rpa_set_target_defaults().
#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

/// Kept at its original spelling through the rename to RPA-Block: this
/// identifies the container format, not the product. Changing it would make an
/// older packaged exe fail in a way that reads as file corruption, for no gain.
constexpr char kMagic[8] = {'P', 'R', 'A', 'P', 'A', 'C', 'K', '1'};
constexpr wchar_t kAppExe[] = L"rpa-studio.exe";
constexpr wchar_t kProductDir[] = L"RPA-Block";

void fail(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"RPA-Block", MB_ICONERROR | MB_OK);
}

std::wstring ownPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0) return {};
        if (written < path.size()) {
            path.resize(written);
            return path;
        }
        path.resize(path.size() * 2);  // truncated; try again with more room
    }
}

std::wstring localAppData() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        return {};
    }
    std::wstring path(raw);
    CoTaskMemFree(raw);
    return path;
}

bool readAll(HANDLE file, void* buffer, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD chunk = 0;
        if (!ReadFile(file, static_cast<char*>(buffer) + done, bytes - done, &chunk, nullptr)) {
            return false;
        }
        if (chunk == 0) return false;  // unexpected EOF
        done += chunk;
    }
    return true;
}

/// Cheap content key. Not a cryptographic digest — it only has to change when
/// the payload changes, so a rebuilt package re-extracts instead of reusing a
/// stale folder.
uint64_t checksum(const std::vector<char>& data) {
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a offset basis
    for (char byte : data) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool runAndWait(const std::wstring& commandLine, const std::wstring& workingDir, DWORD& exitCode) {
    std::wstring mutableCommand = commandLine;  // CreateProcessW may write to this

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &process)) {
        return false;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void removeDirectoryTree(const std::wstring& path) {
    // Best effort: used only to clean up a half-written extraction.
    DWORD exitCode = 0;
    runAndWait(L"cmd.exe /c rd /s /q \"" + path + L"\"", {}, exitCode);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring self = ownPath();
    if (self.empty()) {
        fail(L"Could not determine the location of this executable.");
        return 1;
    }

    HANDLE file = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        fail(L"Could not open this executable to read its payload.");
        return 1;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 16) {
        CloseHandle(file);
        fail(L"This executable carries no payload.");
        return 1;
    }

    // Footer: 8 bytes payload size, then the magic.
    LARGE_INTEGER footerAt{};
    footerAt.QuadPart = size.QuadPart - 16;
    if (!SetFilePointerEx(file, footerAt, nullptr, FILE_BEGIN)) {
        CloseHandle(file);
        fail(L"Could not seek to the payload footer.");
        return 1;
    }

    uint64_t payloadSize = 0;
    char magic[8] = {};
    if (!readAll(file, &payloadSize, sizeof(payloadSize)) || !readAll(file, magic, sizeof(magic))) {
        CloseHandle(file);
        fail(L"Could not read the payload footer.");
        return 1;
    }
    if (memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        CloseHandle(file);
        fail(L"This executable is not a valid RPA-Block package.");
        return 1;
    }
    if (payloadSize == 0 || static_cast<int64_t>(payloadSize) > size.QuadPart - 16) {
        CloseHandle(file);
        fail(L"The payload size recorded in this package is not plausible.");
        return 1;
    }

    LARGE_INTEGER payloadAt{};
    payloadAt.QuadPart = size.QuadPart - 16 - static_cast<int64_t>(payloadSize);
    if (!SetFilePointerEx(file, payloadAt, nullptr, FILE_BEGIN)) {
        CloseHandle(file);
        fail(L"Could not seek to the payload.");
        return 1;
    }

    std::vector<char> payload(static_cast<size_t>(payloadSize));
    const bool read = readAll(file, payload.data(), static_cast<DWORD>(payload.size()));
    CloseHandle(file);
    if (!read) {
        fail(L"Could not read the payload.");
        return 1;
    }

    const std::wstring base = localAppData();
    if (base.empty()) {
        fail(L"Could not locate the local application data folder.");
        return 1;
    }

    wchar_t key[32] = {};
    swprintf_s(key, L"%016llx", static_cast<unsigned long long>(checksum(payload)));

    const std::wstring root = base + L"\\" + kProductDir;
    const std::wstring target = root + L"\\" + key;
    const std::wstring appExe = target + L"\\" + kAppExe;
    const std::wstring stamp = target + L"\\.unpacked";

    // A stamp file written only after a successful extraction is what makes the
    // cache safe: an interrupted unpack leaves no stamp, so the next launch
    // discards the folder and starts over rather than running a partial install.
    const bool cached = GetFileAttributesW(stamp.c_str()) != INVALID_FILE_ATTRIBUTES;

    if (!cached) {
        removeDirectoryTree(target);
        SHCreateDirectoryExW(nullptr, target.c_str(), nullptr);

        const std::wstring zipPath = target + L"\\payload.zip";
        HANDLE out = CreateFileW(zipPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            fail(L"Could not write the payload to:\n" + zipPath);
            return 1;
        }
        DWORD written = 0;
        const bool ok = WriteFile(out, payload.data(), static_cast<DWORD>(payload.size()),
                                  &written, nullptr) &&
                        written == payload.size();
        CloseHandle(out);
        if (!ok) {
            fail(L"Could not write the whole payload. Is the disk full?");
            return 1;
        }

        DWORD tarExit = 1;
        if (!runAndWait(L"tar.exe -xf \"" + zipPath + L"\"", target, tarExit) || tarExit != 0) {
            fail(L"Could not unpack the payload.\n\n"
                 L"This needs the tar.exe that ships with Windows 10 1803 and newer.");
            return 1;
        }
        DeleteFileW(zipPath.c_str());

        HANDLE marker = CreateFileW(stamp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (marker != INVALID_HANDLE_VALUE) CloseHandle(marker);
    }

    if (GetFileAttributesW(appExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fail(L"The package unpacked but does not contain " + std::wstring(kAppExe) + L".");
        return 1;
    }

    // Hand over without waiting: the launcher's job is done once the app starts,
    // and lingering would leave a stray process in Task Manager for the whole
    // session.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + appExe + L"\"";

    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        target.c_str(), &startup, &process)) {
        fail(L"Unpacked successfully but could not start:\n" + appExe);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
