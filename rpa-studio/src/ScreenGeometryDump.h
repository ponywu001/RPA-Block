#pragma once

#include <QString>

namespace rpa::studio {

/// Write a diagnostic describing both coordinate spaces this app straddles: Qt's
/// logical pixels and the physical pixels the screen capture and executor use.
///
/// A mismatch between the two makes the target picker's frozen image sit out of
/// register with the real screen and sends picked coordinates somewhere the user
/// did not point -- and none of that is legible in a screenshot, because the
/// screenshot is taken in one space or the other. Returns false if `path` could
/// not be written.
bool dumpScreenGeometry(const QString& path);

}  // namespace rpa::studio
