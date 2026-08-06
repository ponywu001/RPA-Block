#include "rpa/vision/ScreenCapture.h"

#include <algorithm>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rpa::vision {

#ifdef _WIN32

namespace {

/// Handle wrappers so an early return can't leak a DC or bitmap.
struct ScreenDc {
    HDC dc = GetDC(nullptr);
    ~ScreenDc() { if (dc) ReleaseDC(nullptr, dc); }
};

struct MemoryDc {
    HDC dc;
    explicit MemoryDc(HDC source) : dc(CreateCompatibleDC(source)) {}
    ~MemoryDc() { if (dc) DeleteDC(dc); }
};

struct Bitmap {
    HBITMAP handle;
    explicit Bitmap(HBITMAP h) : handle(h) {}
    ~Bitmap() { if (handle) DeleteObject(handle); }
};

}  // namespace

core::Rect ScreenCapture::virtualDesktopBounds() {
    core::Rect bounds;
    bounds.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds;
}

std::vector<core::Rect> ScreenCapture::monitors() {
    std::vector<core::Rect> found;

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM user) -> BOOL {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info)) {
                auto* list = reinterpret_cast<std::vector<core::Rect>*>(user);
                // rcMonitor, not rcWork: the picker overlays the taskbar too.
                list->push_back(core::Rect{info.rcMonitor.left, info.rcMonitor.top,
                                           info.rcMonitor.right - info.rcMonitor.left,
                                           info.rcMonitor.bottom - info.rcMonitor.top});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));

    // Sorted so callers can pair these up with a toolkit's own screen list, which
    // describes the same physical arrangement in its own units.
    std::sort(found.begin(), found.end(), [](const core::Rect& a, const core::Rect& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    return found;
}

cv::Mat ScreenCapture::grab(const std::optional<core::Rect>& region, std::string& error) {
    const core::Rect bounds = region.value_or(virtualDesktopBounds());
    if (bounds.width <= 0 || bounds.height <= 0) {
        error = "capture region has zero area";
        return {};
    }

    ScreenDc screen;
    if (!screen.dc) {
        error = "GetDC(nullptr) failed";
        return {};
    }

    MemoryDc memory(screen.dc);
    if (!memory.dc) {
        error = "CreateCompatibleDC failed";
        return {};
    }

    Bitmap bitmap(CreateCompatibleBitmap(screen.dc, bounds.width, bounds.height));
    if (!bitmap.handle) {
        error = "CreateCompatibleBitmap failed";
        return {};
    }

    HGDIOBJ previous = SelectObject(memory.dc, bitmap.handle);

    // CAPTUREBLT is what makes layered windows (tooltips, some Electron chrome)
    // show up in the grab instead of appearing as holes.
    const BOOL blitted = BitBlt(memory.dc, 0, 0, bounds.width, bounds.height,
                                screen.dc, bounds.x, bounds.y, SRCCOPY | CAPTUREBLT);
    const DWORD blitError = GetLastError();

    // Deselect before reading the bits: GetDIBits requires that the bitmap not
    // be selected into any DC. Leaving it selected happens to work on current
    // Windows but is undefined per the API contract.
    SelectObject(memory.dc, previous);

    if (!blitted) {
        error = "BitBlt failed (error " + std::to_string(blitError) + ")";
        return {};
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.width;
    // Negative height requests a top-down DIB, which matches cv::Mat's row order.
    info.bmiHeader.biHeight = -bounds.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    cv::Mat bgra(bounds.height, bounds.width, CV_8UC4);
    const int scanned = GetDIBits(memory.dc, bitmap.handle, 0,
                                  static_cast<UINT>(bounds.height),
                                  bgra.data, &info, DIB_RGB_COLORS);
    if (scanned == 0) {
        error = "GetDIBits failed";
        return {};
    }

    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

#else

core::Rect ScreenCapture::virtualDesktopBounds() {
    return {};
}

std::vector<core::Rect> ScreenCapture::monitors() {
    return {};
}

cv::Mat ScreenCapture::grab(const std::optional<core::Rect>&, std::string& error) {
    error = "screen capture is only implemented on Windows";
    return {};
}

#endif  // _WIN32

bool ScreenCapture::grabToFile(const std::string& path,
                               const std::optional<core::Rect>& region,
                               std::string& error) {
    const cv::Mat image = grab(region, error);
    if (image.empty()) return false;

    if (!cv::imwrite(path, image)) {
        error = "cv::imwrite failed for: " + path;
        return false;
    }
    return true;
}

}  // namespace rpa::vision
