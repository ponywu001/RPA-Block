#pragma once

#include <QString>

namespace rpa::studio {

/// Write the UI Automation control tree of a window to `path`.
///
/// A diagnostic, in the same spirit as --dump-geometry and `rpa-cli ocr`: what
/// an application exposes to automation is invisible from the outside, and
/// guessing at it produces locators that work on the developer's machine and
/// nowhere else. Look at the tree first, then write the target.
///
/// `titleFilter` empty means the foreground window, which is why `delaySeconds`
/// exists -- it gives the user time to bring the application they care about to
/// the front after launching this.
bool dumpUiaTree(const QString& path, const QString& titleFilter, int delaySeconds);

/// Resolve one relative target and print where it *would* be clicked.
///
/// Separate from actually running a flow on purpose: checking whether "the box
/// beside 客戶全稱" resolves should not require handing over the mouse, and a
/// target that cannot be tested without clicking something is a target nobody
/// will test.
bool probeRelativeTarget(const QString& anchor,
                         const QString& direction,
                         const QString& element,
                         int maxDistance,
                         int delaySeconds,
                         const QString& titleFilter);

}  // namespace rpa::studio
