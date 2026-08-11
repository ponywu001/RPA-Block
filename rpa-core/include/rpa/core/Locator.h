#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rpa/core/Script.h"
#include "rpa/core/Types.h"

namespace rpa::core {

struct LocateResult {
    bool found = false;
    Rect box;              // bounding box of the match, in screen coordinates
    Point point;           // click point (box centre plus the target's offset)
    double confidence = 0; // OCR confidence or template-match score
    std::string matchedText;
    std::string error;
};

/// Screen-search backend. `rpa-vision` provides the real implementation;
/// keeping it abstract lets rpa-core build and be tested without OpenCV or
/// ONNX Runtime present.
class ITargetLocator {
public:
    virtual ~ITargetLocator() = default;

    virtual LocateResult locate(const Target& target) = 0;

    /// Save a full-screen (or region) capture to `path`. Returns false and
    /// fills `error` on failure.
    virtual bool captureToFile(const std::string& path,
                               const std::optional<Rect>& region,
                               std::string& error) = 0;
};

/// Tries each backend in order and returns the first hit.
///
/// This is what makes "the field beside 客戶全稱" work across unrelated
/// applications. A well-behaved Win32 form names its edit boxes after the
/// label next to them, so UI Automation answers immediately and exactly; a
/// custom-drawn or remote-desktop window tells automation nothing, and the
/// same instruction has to be worked out from pixels. Neither backend covers
/// both, and the person writing the flow should not have to know which one
/// they are looking at.
///
/// A miss collects every backend's reason rather than only the last, because
/// "UIA saw no Edit controls at all / OCR never found the label" and "UIA found
/// the label but nothing beside it" call for completely different fixes.
class CompositeLocator : public ITargetLocator {
public:
    /// Backends are borrowed, in priority order. Null entries are skipped so a
    /// caller can pass an optional backend without branching.
    void addBackend(ITargetLocator* backend) {
        if (backend) backends_.push_back(backend);
    }

    LocateResult locate(const Target& target) override {
        LocateResult combined;
        std::string reasons;

        for (auto* backend : backends_) {
            LocateResult attempt = backend->locate(target);
            if (attempt.found) return attempt;
            if (!attempt.error.empty()) {
                if (!reasons.empty()) reasons += "; ";
                reasons += attempt.error;
            }
        }

        combined.error = reasons.empty() ? "no locator backend is available" : reasons;
        return combined;
    }

    bool captureToFile(const std::string& path,
                       const std::optional<Rect>& region,
                       std::string& error) override {
        for (auto* backend : backends_) {
            if (backend->captureToFile(path, region, error)) return true;
        }
        if (error.empty()) error = "no locator backend can capture the screen";
        return false;
    }

private:
    std::vector<ITargetLocator*> backends_;
};

/// Always-miss locator so a script containing OCR steps can still be loaded and
/// stepped through on a machine without the vision module.
class NullTargetLocator : public ITargetLocator {
public:
    LocateResult locate(const Target&) override {
        LocateResult r;
        r.error = "vision module not available";
        return r;
    }
    bool captureToFile(const std::string&, const std::optional<Rect>&, std::string& error) override {
        error = "vision module not available";
        return false;
    }
};

}  // namespace rpa::core
