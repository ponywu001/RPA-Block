#include "rpa/vision/VisionLocator.h"

#include <filesystem>
#include <regex>

#include <opencv2/imgproc.hpp>

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

namespace {

/// Candidate rectangles that look like something you could click into.
///
/// Form fields are drawn either with a border or as a filled block that differs
/// from the page behind it, so edges plus contours finds both. Everything about
/// this is a heuristic -- which is why a miss reports how many candidates were
/// considered rather than only that nothing matched.
std::vector<cv::Rect> findBoxes(const cv::Mat& bgr) {
    cv::Mat grey;
    cv::cvtColor(bgr, grey, cv::COLOR_BGR2GRAY);

    // Low thresholds on purpose: modern flat interfaces outline their inputs in
    // a colour a shade away from the background, and the usual 100/200 pair
    // misses those outlines completely.
    cv::Mat edges;
    cv::Canny(grey, edges, 20, 60);

    // Close one-pixel gaps so a border broken by a caret or a placeholder glyph
    // is still one contour.
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(edges, edges, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Rect> boxes;
    boxes.reserve(contours.size());
    for (const auto& contour : contours) {
        const cv::Rect box = cv::boundingRect(contour);
        // A field is wider than it is tall and big enough to click. These bounds
        // drop character outlines and page-sized panels alike.
        if (box.width < 40 || box.height < 12 || box.height > 120) continue;
        if (box.width < box.height) continue;
        boxes.push_back(box);
    }
    return boxes;
}

/// Distance from anchor to candidate in the wanted direction, or -1 when the
/// candidate does not lie that way. Mirrors the UI Automation pass: the two must
/// overlap on the perpendicular axis, or a field from another row wins on
/// straight-line distance alone.
int directedDistance(const core::Rect& anchor, const core::Rect& candidate,
                     core::Direction direction) {
    const bool overlapsVertically =
        candidate.y < anchor.y + anchor.height && anchor.y < candidate.y + candidate.height;
    const bool overlapsHorizontally =
        candidate.x < anchor.x + anchor.width && anchor.x < candidate.x + candidate.width;

    switch (direction) {
        case core::Direction::Right:
            if (!overlapsVertically || candidate.x < anchor.x + anchor.width / 2) return -1;
            return candidate.x - (anchor.x + anchor.width);
        case core::Direction::Left:
            if (!overlapsVertically || candidate.x + candidate.width > anchor.x + anchor.width / 2)
                return -1;
            return anchor.x - (candidate.x + candidate.width);
        case core::Direction::Below:
            if (!overlapsHorizontally || candidate.y < anchor.y + anchor.height / 2) return -1;
            return candidate.y - (anchor.y + anchor.height);
        case core::Direction::Above:
            if (!overlapsHorizontally ||
                candidate.y + candidate.height > anchor.y + anchor.height / 2)
                return -1;
            return anchor.y - (candidate.y + candidate.height);
    }
    return -1;
}

}  // namespace

core::LocateResult VisionLocator::locateRelative(const core::Target& target) {
    core::LocateResult result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ocrReady_) {
            result.error = "OCR models are not loaded, so the anchor cannot be found on screen";
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

    const OcrLine* anchor = nullptr;
    for (const auto& line : lines) {
        if (!core::textMatches(line.text, target.text, target.match)) continue;
        if (!anchor || line.confidence > anchor->confidence) anchor = &line;
    }

    if (!anchor) {
        result.error = "no OCR line matched the anchor '" + target.text + "' (" +
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
        }
        return result;
    }

    const core::Rect anchorBox{anchor->box.x, anchor->box.y, anchor->box.width,
                               anchor->box.height};

    const std::vector<cv::Rect> boxes = findBoxes(image);

    int best = -1;
    cv::Rect chosen;
    for (const cv::Rect& box : boxes) {
        const core::Rect candidate{box.x, box.y, box.width, box.height};
        // The anchor's own text sits inside a box on many forms; a candidate
        // that swallows the label is the panel behind it, not the field.
        if (candidate.x <= anchorBox.x && candidate.y <= anchorBox.y &&
            candidate.x + candidate.width >= anchorBox.x + anchorBox.width &&
            candidate.y + candidate.height >= anchorBox.y + anchorBox.height) {
            continue;
        }

        const int distance = directedDistance(anchorBox, candidate, target.direction);
        if (distance < 0 || distance > target.maxDistance) continue;
        if (best >= 0 && distance >= best) continue;

        best = distance;
        chosen = box;
    }

    if (best < 0) {
        // Says which half of the job failed. The anchor was found -- so the
        // question is whether this interface draws anything box-shaped at all,
        // or just not in that direction.
        result.error = "found the anchor '" + anchor->text + "' but no box " +
                       core::toString(target.direction) + " of it within " +
                       std::to_string(target.maxDistance) + "px (" +
                       std::to_string(boxes.size()) +
                       " box-like shapes seen on screen). A borderless field cannot be found "
                       "this way -- use an image template for it.";
        return result;
    }

    result.found = true;
    // Lower than the OCR line's own score on purpose: the anchor was read, the
    // field was inferred, and a caller comparing confidences should see that
    // this is the weaker of the two ways to land the same click.
    result.confidence = anchor->confidence * 0.8;
    result.matchedText = anchor->text;
    result.box = core::Rect{chosen.x + searchArea.x, chosen.y + searchArea.y, chosen.width,
                            chosen.height};
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
        case core::TargetKind::Relative:
            return locateRelative(target);
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
