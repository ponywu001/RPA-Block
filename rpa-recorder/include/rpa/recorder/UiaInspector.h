#pragma once

#include <string>

#include "rpa/core/Types.h"
#include "rpa/recorder/RecordedEvent.h"

namespace rpa::recorder {

/// COM apartment setup for the thread that will call the inspect functions.
/// Both are no-ops when UI Automation is unavailable.
void initializeUiaForThread();
void uninitializeUiaForThread();

/// Describe the UI element at a screen point. Returns an empty-ish ElementInfo
/// rather than failing, since element data is a hint, not a requirement.
ElementInfo inspectElementAt(const core::Point& point);

/// Describe whatever currently has keyboard focus.
ElementInfo inspectFocusedElement();

/// Bounding box of the whole virtual desktop.
core::Rect desktopBounds();

/// Write a PNG of `region` (empty region means the whole desktop). Implemented
/// with GDI + WIC so the recorder does not depend on OpenCV.
bool captureRegionToPng(const std::string& path, const core::Rect& region, std::string& error);

}  // namespace rpa::recorder
