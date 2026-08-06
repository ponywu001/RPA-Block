#pragma once

#include <vector>

#include "rpa/core/Script.h"

namespace rpa::core {

/// Maps between a UI toolkit's logical pixel space and the physical screen pixels
/// the executor clicks in.
///
/// The two spaces diverge the moment any display is scaled: Qt reports a monitor
/// Windows captures at 1920x1080 as 1536x864 at 125%. Anything that takes a rect
/// from a widget and hands it to the executor -- or crops a screen capture using
/// widget coordinates -- has to convert, or the click lands somewhere the user did
/// not point and a saved template is the wrong size to ever match.
///
/// Kept here, free of any toolkit type, so the arithmetic is testable at scale
/// factors the development machine cannot physically produce.
struct ScaleMapping {
    /// Absolute physical bounds of the virtual desktop, as the screen capture and
    /// the executor understand it. `x`/`y` are non-zero when a monitor sits above
    /// or to the left of the primary one.
    Rect physical{};
    /// Extent of the same desktop in the toolkit's logical pixels.
    int logicalWidth = 0;
    int logicalHeight = 0;

    /// Falls back to 1.0 rather than dividing by zero, which makes an
    /// unreported screen geometry behave like an unscaled display instead of
    /// producing NaN coordinates.
    double scaleX() const;
    double scaleY() const;

    /// Overlay-local logical rect -> absolute physical screen rect.
    Rect toPhysical(int x, int y, int width, int height) const;

    /// Overlay-local logical rect -> rect within a physical-resolution capture of
    /// the whole desktop. Clamped to the capture, so a rubber band dragged past
    /// the edge crops what exists instead of reading out of bounds.
    Rect toCapture(int x, int y, int width, int height) const;
};

/// One monitor described in both coordinate spaces.
struct ScreenPairing {
    /// Absolute rect in the toolkit's logical pixels.
    Rect logical{};
    /// Absolute rect in physical screen pixels.
    Rect physical{};

    /// Mapping from a rect local to this monitor's overlay to physical pixels.
    /// Correct because the scale is uniform *within* one monitor, which is the
    /// whole reason the picker needs one overlay per screen instead of one
    /// stretched across the desktop.
    ScaleMapping mapping() const;
};

/// Pair a toolkit's logical screen rects up with the physical monitor rects.
///
/// Both lists describe the same physical arrangement, so sorting each by position
/// and zipping them matches monitor to monitor. Returns empty when the counts
/// disagree -- better to fall back to a single-screen assumption than to pair the
/// wrong rectangles and send every click to another monitor.
std::vector<ScreenPairing> pairScreens(const std::vector<Rect>& logical,
                                       const std::vector<Rect>& physical);

}  // namespace rpa::core
