#pragma once

#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "rpa/core/Types.h"

namespace rpa::vision {

/// Grabs pixels from the desktop. Uses GDI BitBlt, which works across the
/// virtual desktop (all monitors) and does not require elevation.
class ScreenCapture {
public:
    /// Bounding box of the whole virtual desktop, in the same coordinate space
    /// the executor uses for clicks.
    static core::Rect virtualDesktopBounds();

    /// Physical bounds of each monitor, left-to-right then top-to-bottom.
    ///
    /// A UI toolkit reports monitors in its own logical pixels, which on a
    /// mixed-DPI desktop are a different size *per monitor* -- a 1920x1080
    /// display next to a 2560x1440 one at 150% is 1920x1080 and 1707x960 to Qt.
    /// There is no single ratio between the two spaces, so anything that has to
    /// line up with the real screen needs the physical rect of each monitor
    /// separately rather than a scaled desktop bounding box.
    static std::vector<core::Rect> monitors();

    /// Capture `region` (or the full virtual desktop when empty) as BGR.
    /// Returns an empty Mat and fills `error` on failure.
    static cv::Mat grab(const std::optional<core::Rect>& region, std::string& error);

    static bool grabToFile(const std::string& path,
                           const std::optional<core::Rect>& region,
                           std::string& error);
};

}  // namespace rpa::vision
