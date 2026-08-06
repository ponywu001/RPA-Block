#pragma once

#include <optional>
#include <string>

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
