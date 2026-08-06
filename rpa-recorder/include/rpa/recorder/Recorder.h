#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "rpa/recorder/RecordedEvent.h"

namespace rpa::recorder {

struct RecorderConfig {
    /// Where per-click screenshots are written.
    std::string assetDirectory;
    /// Half-width/height of the neighbourhood captured around each click.
    int clickCaptureRadius = 120;
    /// Capture a cropped screenshot for every click.
    bool captureClickScreenshots = true;
    /// Query UI Automation for the element under the cursor on each click.
    /// Adds a few milliseconds per click and can stall on unresponsive apps,
    /// so it is easy to switch off.
    bool captureElementInfo = true;
    /// Keystrokes arriving within this window are merged into one text event.
    int textCoalesceMs = 1200;
};

/// Records desktop input using low-level Windows hooks.
///
/// The hooks must be installed on a thread that pumps messages, so the recorder
/// owns a dedicated thread with its own message loop. Hook callbacks do almost
/// nothing beyond pushing onto a queue; screenshots and UI Automation queries
/// happen on a second worker so a slow capture can never delay the input chain.
class Recorder {
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    /// Fired on the worker thread each time an event is finalised.
    void setEventCallback(std::function<void(const RecordedEvent&)> callback);

    bool start(const RecorderConfig& config, std::string& error);
    void stop();
    bool isRecording() const;

    /// Suspend capture without tearing the hooks down. Events arriving while
    /// paused are dropped, not queued — the point is to let the user do
    /// something they do not want in the recording.
    void setPaused(bool paused);
    bool isPaused() const;

    /// Snapshot of everything captured so far.
    RecordingSession session() const;

    /// Discard captured events; keeps the recorder running.
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rpa::recorder
