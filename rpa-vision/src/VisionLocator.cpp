#include "rpa/vision/VisionLocator.h"

#include <filesystem>
#include <regex>

#include "rpa/core/TextMatch.h"
#include "rpa/vision/ScreenCapture.h"

namespace rpa::vision {

namespace {

namespace fs = std::filesystem;

}  // namespace

VisionLocator::VisionLocator() = default;
VisionLocator::~VisionLocator() = default;

bool VisionLocator::loadOcr(const OcrConfig& config, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    ocrReady_ = ocr_.load(config, error);
    return ocrReady_;
}

bool VisionLocator::ocrReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ocrReady_;
}

void VisionLocator::setWorkingDirectory(std::string directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    workingDirectory_ = std::move(directory);
}

std::string VisionLocator::resolvePath(const std::string& path) const {
    if (path.empty()) return path;
    const fs::path candidate(path);
    if (candidate.is_absolute() || workingDirectory_.empty()) return path;
    return (fs::path(workingDirectory_) / candidate).string();
}

std::vector<OcrLine> VisionLocator::readRegion(const core::Rect& region, std::string& error) {
    std::vector<OcrLine> lines;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ocrReady_) {
            error = "OCR models are not loaded";
            return lines;
        }
    }

    // Resolve the area explicitly rather than passing nullopt: an empty region
    // means the whole virtual desktop, whose origin is *not* (0,0) when a
    // monitor sits left of or above the primary one. Skipping the offset there
    // would hand back boxes that miss by exactly that origin.
    const core::Rect area =
        region.empty() ? ScreenCapture::virtualDesktopBounds() : region;

    const cv::Mat image = ScreenCapture::grab(area, error);
    if (image.empty()) return lines;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines = ocr_.recognize(image, error);
    }

    // Report boxes in screen coordinates so callers can click them directly.
    for (auto& line : lines) {
        line.box.x += area.x;
        line.box.y += area.y;
    }
    return lines;
}

std::vector<OcrLine> VisionLocator::readImage(const cv::Mat& bgr, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ocrReady_) {
        error = "OCR models are not loaded";
        return {};
    }
    return ocr_.recognize(bgr, error);
}

core::LocateResult VisionLocator::locateByText(const core::Target& target) {
    core::LocateResult result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ocrReady_) {
            result.error = "OCR models are not loaded";
            return result;
        }
    }

    const core::Rect searchArea =
        target.region.value_or(ScreenCapture::virtualDesktopBounds());

    std::string error;
    const cv::Mat image = ScreenCapture::grab(searchArea, error);
    if (image.empty()) {
        result.error = error;
        return result;
    }

    std::vector<OcrLine> lines;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines = ocr_.recognize(image, error);
    }
    if (!error.empty() && lines.empty()) {
        result.error = error;
        return result;
    }

    const OcrLine* best = nullptr;
    for (const auto& line : lines) {
        if (!core::textMatches(line.text, target.text, target.match)) continue;
        if (!best || line.confidence > best->confidence) best = &line;
    }

    if (!best) {
        // Naming the near misses is the difference between a dead end and an
        // obvious answer. The usual cause is an adjacent glyph swept into the
        // line -- a magnifier icon reads as "Q", so a search box comes back as
        // "Q 搜尋" and an exact match on "搜尋" fails while the text is plainly
        // on screen.
        result.error = "no OCR line matched '" + target.text + "' (" +
                       std::to_string(lines.size()) + " lines read)";

        std::vector<std::string> texts;
        texts.reserve(lines.size());
        for (const OcrLine& line : lines) texts.push_back(line.text);
        const std::vector<std::string> near = core::nearestTexts(texts, target.text);
        if (!near.empty()) {
            result.error += ". Closest: ";
            for (std::size_t i = 0; i < near.size(); ++i) {
                if (i) result.error += ", ";
                result.error += "'" + near[i] + "'";
            }
            result.error += ". Try the \"contains\" match mode if the wanted text is in there.";
        }
        return result;
    }

    result.found = true;
    result.confidence = best->confidence;
    result.matchedText = best->text;
    result.box = core::Rect{best->box.x + searchArea.x, best->box.y + searchArea.y,
                            best->box.width, best->box.height};
    const core::Point centre = result.box.center();
    result.point = core::Point{centre.x + target.offsetX, centre.y + target.offsetY};
    return result;
}

core::LocateResult VisionLocator::locateByTemplate(const core::Target& target) {
    core::LocateResult result;

    const core::Rect searchArea =
        target.region.value_or(ScreenCapture::virtualDesktopBounds());

    std::string error;
    const cv::Mat image = ScreenCapture::grab(searchArea, error);
    if (image.empty()) {
        result.error = error;
        return result;
    }

    std::string resolved;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resolved = resolvePath(target.templatePath);
    }

    TemplateMatchOptions options;
    options.threshold = target.threshold;
    const TemplateMatch match = TemplateMatcher::matchFile(image, resolved, options);

    if (!match.found) {
        result.error = match.error.empty() ? "template not found on screen" : match.error;
        result.confidence = match.score;
        return result;
    }

    result.found = true;
    result.confidence = match.score;
    result.matchedText = target.templatePath;
    result.box = core::Rect{match.box.x + searchArea.x, match.box.y + searchArea.y,
                            match.box.width, match.box.height};
    const core::Point centre = result.box.center();
    result.point = core::Point{centre.x + target.offsetX, centre.y + target.offsetY};
    return result;
}

core::LocateResult VisionLocator::locate(const core::Target& target) {
    switch (target.kind) {
        case core::TargetKind::Ocr:
            return locateByText(target);
        case core::TargetKind::Image:
            return locateByTemplate(target);
        case core::TargetKind::Point: {
            core::LocateResult result;
            result.found = true;
            result.confidence = 1.0;
            result.box = core::Rect{target.point.x, target.point.y, 1, 1};
            result.point = core::Point{target.point.x + target.offsetX,
                                       target.point.y + target.offsetY};
            return result;
        }
    }
    core::LocateResult result;
    result.error = "unknown target kind";
    return result;
}

bool VisionLocator::captureToFile(const std::string& path,
                                  const std::optional<core::Rect>& region,
                                  std::string& error) {
    std::string resolved;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resolved = resolvePath(path);
    }

    std::error_code ec;
    const fs::path parent = fs::path(resolved).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);

    return ScreenCapture::grabToFile(resolved, region, error);
}

}  // namespace rpa::vision
