#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rpa/core/Locator.h"
#include "rpa/vision/OcrEngine.h"
#include "rpa/vision/TemplateMatcher.h"

namespace rpa::vision {

/// Implements rpa-core's locator interface on top of screen capture, PP-OCR,
/// and OpenCV template matching.
class VisionLocator : public core::ITargetLocator {
public:
    VisionLocator();
    ~VisionLocator() override;

    /// OCR is optional: `image` targets work without it. Returns false and
    /// fills `error` when the models cannot be loaded.
    bool loadOcr(const OcrConfig& config, std::string& error);
    bool ocrReady() const;

    /// Root directory relative template paths resolve against.
    void setWorkingDirectory(std::string directory);

    /// Capture a region and OCR it. Boxes come back in screen coordinates.
    std::vector<OcrLine> readRegion(const core::Rect& region, std::string& error);

    /// OCR an image the caller already has. The target picker needs this: its
    /// overlay covers the screen, so a fresh capture would read the overlay
    /// instead of the application underneath.
    std::vector<OcrLine> readImage(const cv::Mat& bgr, std::string& error);

    core::LocateResult locate(const core::Target& target) override;
    bool captureToFile(const std::string& path,
                       const std::optional<core::Rect>& region,
                       std::string& error) override;

private:
    core::LocateResult locateByText(const core::Target& target);
    core::LocateResult locateByTemplate(const core::Target& target);
    std::string resolvePath(const std::string& path) const;

    mutable std::mutex mutex_;
    OcrEngine ocr_;
    bool ocrReady_ = false;
    std::string workingDirectory_;
};

}  // namespace rpa::vision
