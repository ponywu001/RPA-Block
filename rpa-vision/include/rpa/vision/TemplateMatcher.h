#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace rpa::vision {

struct TemplateMatch {
    bool found = false;
    cv::Rect box;
    double score = 0.0;
    double scale = 1.0;
    std::string error;
};

struct TemplateMatchOptions {
    /// Minimum normalised-correlation score to accept.
    double threshold = 0.85;
    /// Scales tried around 1.0, to absorb DPI differences between the machine
    /// that captured the template and the machine replaying it.
    std::vector<double> scales = {1.0, 0.9, 1.1, 0.8, 1.25};
    /// When true, matching runs on grayscale, which is faster and more tolerant
    /// of theme/accent-colour differences.
    bool grayscale = true;
};

class TemplateMatcher {
public:
    /// Locate `templateImage` inside `haystack`. Both must be BGR.
    static TemplateMatch match(const cv::Mat& haystack,
                               const cv::Mat& templateImage,
                               const TemplateMatchOptions& options = {});

    /// Convenience overload that loads the template from disk. Loaded images
    /// are cached by path plus last-write time, so a replay loop doesn't hit
    /// the filesystem on every attempt.
    static TemplateMatch matchFile(const cv::Mat& haystack,
                                   const std::string& templatePath,
                                   const TemplateMatchOptions& options = {});

    static void clearCache();
};

}  // namespace rpa::vision
