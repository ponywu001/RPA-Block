#include "rpa/vision/TemplateMatcher.h"

#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace rpa::vision {

namespace {

namespace fs = std::filesystem;

struct CacheEntry {
    cv::Mat image;
    fs::file_time_type writeTime{};
};

std::mutex& cacheMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, CacheEntry>& cache() {
    static std::map<std::string, CacheEntry> entries;
    return entries;
}

cv::Mat loadTemplate(const std::string& path, std::string& error) {
    std::error_code ec;
    const fs::path filePath(path);
    const fs::file_time_type writeTime = fs::last_write_time(filePath, ec);
    if (ec) {
        error = "cannot stat template: " + path;
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache().find(path);
        if (it != cache().end() && it->second.writeTime == writeTime) {
            return it->second.image;
        }
    }

    cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        error = "cannot read template image: " + path;
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cache()[path] = CacheEntry{image, writeTime};
    }
    return image;
}

}  // namespace

TemplateMatch TemplateMatcher::match(const cv::Mat& haystack,
                                     const cv::Mat& templateImage,
                                     const TemplateMatchOptions& options) {
    TemplateMatch result;

    if (haystack.empty()) {
        result.error = "screen image is empty";
        return result;
    }
    if (templateImage.empty()) {
        result.error = "template image is empty";
        return result;
    }

    cv::Mat scene = haystack;
    cv::Mat needle = templateImage;
    if (options.grayscale) {
        cv::cvtColor(haystack, scene, cv::COLOR_BGR2GRAY);
        cv::cvtColor(templateImage, needle, cv::COLOR_BGR2GRAY);
    }

    std::vector<double> scales = options.scales;
    if (scales.empty()) scales.push_back(1.0);

    // TM_CCOEFF_NORMED legitimately returns values in [-1, 1], so a sentinel
    // score cannot stand in for "no scale was tried": a genuine -0.4 match would
    // be misreported as "template larger than the search area". Track it
    // explicitly instead.
    bool haveBest = false;
    double bestScore = 0.0;

    for (double scale : scales) {
        if (scale <= 0.0) continue;

        cv::Mat scaled;
        if (std::abs(scale - 1.0) < 1e-9) {
            scaled = needle;
        } else {
            const int width = static_cast<int>(std::round(needle.cols * scale));
            const int height = static_cast<int>(std::round(needle.rows * scale));
            if (width < 4 || height < 4) continue;
            cv::resize(needle, scaled, cv::Size(width, height), 0, 0,
                       scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
        }

        if (scaled.cols > scene.cols || scaled.rows > scene.rows) continue;

        cv::Mat scores;
        cv::matchTemplate(scene, scaled, scores, cv::TM_CCOEFF_NORMED);

        double minScore = 0.0;
        double maxScore = 0.0;
        cv::Point minLocation;
        cv::Point maxLocation;
        cv::minMaxLoc(scores, &minScore, &maxScore, &minLocation, &maxLocation);

        if (!haveBest || maxScore > bestScore) {
            haveBest = true;
            bestScore = maxScore;
            result.box = cv::Rect(maxLocation, cv::Size(scaled.cols, scaled.rows));
            result.score = maxScore;
            result.scale = scale;
        }

        // A near-perfect hit at this scale won't be beaten; stop early rather
        // than paying for the remaining passes on every replay.
        if (maxScore >= 0.99) break;
    }

    if (!haveBest) {
        result.error = "template is larger than the search area at every scale";
        return result;
    }

    result.found = result.score >= options.threshold;
    if (!result.found) {
        result.error = "best score " + std::to_string(result.score) + " is below threshold " +
                       std::to_string(options.threshold);
    }
    return result;
}

TemplateMatch TemplateMatcher::matchFile(const cv::Mat& haystack,
                                         const std::string& templatePath,
                                         const TemplateMatchOptions& options) {
    TemplateMatch result;
    std::string error;
    const cv::Mat templateImage = loadTemplate(templatePath, error);
    if (templateImage.empty()) {
        result.error = error;
        return result;
    }
    return match(haystack, templateImage, options);
}

void TemplateMatcher::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    cache().clear();
}

}  // namespace rpa::vision
