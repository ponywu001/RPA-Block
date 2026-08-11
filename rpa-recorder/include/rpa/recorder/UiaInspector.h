#pragma once

#include <string>
#include <vector>

#include "rpa/core/Script.h"
#include "rpa/core/Types.h"
#include "rpa/recorder/RecordedEvent.h"

namespace rpa::recorder {

/// One node of a window's control tree, flattened with its depth.
///
/// `labeledBy` is the interesting one for relative targeting: a well-built
/// Win32 form tells you which static text labels an edit box, which beats
/// working it out from coordinates.
struct UiaNode {
    int depth = 0;
    std::string name;
    std::string controlType;
    std::string automationId;
    std::string className;
    std::string labeledBy;
    /// Whether the element accepts keyboard focus -- the cheapest signal for
    /// "this is something a user types into or presses".
    bool keyboardFocusable = false;
    bool offscreen = false;
    core::Rect bounds;
};

/// Walk the control tree of the foreground window (or the first visible window
/// whose title contains `titleFilter`). Depth and node count are capped so a
/// pathological tree cannot hang the caller.
std::vector<UiaNode> dumpWindowTree(const std::string& titleFilter,
                                    int maxDepth,
                                    int maxNodes,
                                    std::string& error);

/// How a relative target was resolved, for the run log. Which strategy answered
/// says more about a flaky step than the coordinates do: a step that used to
/// match by name and now falls back to geometry is one release away from
/// breaking.
enum class UiaMatchStrategy {
    None,
    /// The control carries the label as its own accessible name. Standard Win32
    /// forms do this automatically for the static text preceding a field.
    ByName,
    /// A separate label element was found, then the nearest accepting control
    /// in the requested direction.
    ByGeometry,
};

struct UiaMatch {
    bool found = false;
    core::Rect bounds;
    std::string name;
    std::string controlType;
    UiaMatchStrategy strategy = UiaMatchStrategy::None;
    /// Populated on a miss: what the tree did contain, so the failure names a
    /// cause instead of only an absence.
    std::string diagnosis;
};

/// Resolve "the control `direction` of the label `anchorText`".
///
/// `windowTitle` empty means the foreground window, which is the right default
/// while a flow is running -- a step that targets an application has already
/// activated it. Naming a window is for testing a target without having to
/// arrange for it to be in front first.
UiaMatch findRelativeElement(const std::string& anchorText,
                             core::MatchMode match,
                             core::Direction direction,
                             core::ElementRole role,
                             int maxDistance,
                             const std::string& windowTitle = {});

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
