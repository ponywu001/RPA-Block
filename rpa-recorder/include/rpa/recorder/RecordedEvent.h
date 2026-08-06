#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rpa/core/Types.h"

namespace rpa::recorder {

enum class RecordedEventType {
    MouseClick,
    MouseDoubleClick,
    TextInput,
    KeyCombo,
    WindowChange,
};

std::string toString(RecordedEventType type);

/// What UI Automation could tell us about the control under the cursor. Any
/// field may be empty; the AI treats them as hints for choosing an anchor.
struct ElementInfo {
    std::string name;
    std::string controlType;
    std::string automationId;
    std::string className;
    std::string windowTitle;
    std::string processName;
    core::Rect bounds;
};

struct RecordedEvent {
    RecordedEventType type = RecordedEventType::MouseClick;

    /// Milliseconds since recording started.
    int64_t timestampMs = 0;

    core::Point position;
    core::MouseButton button = core::MouseButton::Left;

    /// Consecutive character keystrokes are coalesced into one TextInput event.
    std::string text;
    /// `ctrl+shift+s` form for KeyCombo events.
    std::string keys;

    ElementInfo element;

    /// PNG file written next to the session, showing the neighbourhood of the
    /// click. Empty when capture was disabled or failed.
    std::string screenshotPath;
    /// Full-frame capture, written only for the first event and after each
    /// window change, to bound disk use on long recordings.
    std::string fullScreenshotPath;
};

struct RecordingSession {
    std::vector<RecordedEvent> events;
    /// Directory holding this session's screenshots.
    std::string assetDirectory;
    core::Rect desktopBounds;
};

/// Serialize a session to the compact JSON summary the AI prompt embeds.
std::string toJson(const RecordingSession& session, bool pretty = true);

/// Human-readable digest for the "recording result" list in the UI, and for
/// the text portion of the AI prompt.
std::string toSummaryText(const RecordingSession& session);

}  // namespace rpa::recorder
