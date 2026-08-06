#include "rpa/core/Geometry.h"

#include <algorithm>
#include <cmath>

namespace rpa::core {

namespace {

int scaled(int value, double factor) {
    return static_cast<int>(std::lround(value * factor));
}

}  // namespace

double ScaleMapping::scaleX() const {
    if (logicalWidth <= 0) return 1.0;
    return static_cast<double>(physical.width) / logicalWidth;
}

double ScaleMapping::scaleY() const {
    if (logicalHeight <= 0) return 1.0;
    return static_cast<double>(physical.height) / logicalHeight;
}

Rect ScaleMapping::toPhysical(int x, int y, int width, int height) const {
    return Rect{physical.x + scaled(x, scaleX()), physical.y + scaled(y, scaleY()),
                scaled(width, scaleX()), scaled(height, scaleY())};
}

Rect ScaleMapping::toCapture(int x, int y, int width, int height) const {
    // Relative to the capture, so no desktop origin is added -- the capture's own
    // top-left pixel already is the desktop's top-left.
    int left = scaled(x, scaleX());
    int top = scaled(y, scaleY());
    int right = left + scaled(width, scaleX());
    int bottom = top + scaled(height, scaleY());

    left = std::clamp(left, 0, physical.width);
    top = std::clamp(top, 0, physical.height);
    right = std::clamp(right, 0, physical.width);
    bottom = std::clamp(bottom, 0, physical.height);

    return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

ScaleMapping ScreenPairing::mapping() const {
    return ScaleMapping{physical, logical.width, logical.height};
}

std::vector<ScreenPairing> pairScreens(const std::vector<Rect>& logical,
                                       const std::vector<Rect>& physical) {
    if (logical.empty() || logical.size() != physical.size()) return {};

    auto byPosition = [](const Rect& a, const Rect& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    };

    std::vector<Rect> sortedLogical = logical;
    std::vector<Rect> sortedPhysical = physical;
    std::sort(sortedLogical.begin(), sortedLogical.end(), byPosition);
    std::sort(sortedPhysical.begin(), sortedPhysical.end(), byPosition);

    std::vector<ScreenPairing> pairs;
    pairs.reserve(sortedLogical.size());
    for (std::size_t i = 0; i < sortedLogical.size(); ++i) {
        // A physical monitor is never smaller than its logical description: the
        // toolkit divides by the scale factor. If one comes back smaller the
        // lists do not describe the same desktop, so refuse rather than guess.
        if (sortedPhysical[i].width < sortedLogical[i].width ||
            sortedPhysical[i].height < sortedLogical[i].height) {
            return {};
        }
        pairs.push_back(ScreenPairing{sortedLogical[i], sortedPhysical[i]});
    }
    return pairs;
}

}  // namespace rpa::core
