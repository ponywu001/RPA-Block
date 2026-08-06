#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace rpa::vision {

struct OcrLine {
    std::string text;
    /// Axis-aligned bounding box in the coordinate space of the image passed
    /// to `recognize()`.
    cv::Rect box;
    double confidence = 0.0;
};

struct OcrConfig {
    /// Directory holding `det.onnx`, `rec.onnx`, and `keys.txt` (the PP-OCR
    /// character dictionary, one entry per line).
    std::string modelDirectory;

    /// Probability above which a detection pixel counts as text.
    double detectionThreshold = 0.3;
    /// Mean score a candidate box must reach to survive.
    double boxThreshold = 0.5;
    /// How far each detected box is expanded before cropping, so ascenders and
    /// descenders are not clipped off the recognition input.
    double unclipRatio = 1.6;
    /// Longest side the detection input is resized to. Larger finds smaller
    /// text at proportionally higher cost.
    int detectionMaxSideLength = 960;
    /// Recognition input height; PP-OCRv4 recognition models are trained at 48.
    int recognitionHeight = 48;
    /// Discard recognised lines below this confidence.
    double minConfidence = 0.5;
    /// Use more CPU threads for inference; 0 lets ONNX Runtime decide.
    int threadCount = 0;

    /// When set, every crop handed to the recogniser is written here as a PNG.
    /// Recognition failures are otherwise invisible -- the pipeline just returns
    /// fewer lines -- and the crop is the one intermediate worth seeing.
    std::string debugCropDirectory;
};

/// Where a frame's text went. Every stage of the pipeline drops candidates
/// silently -- which is correct at run time, but leaves "no text found" with no
/// way to tell a detector that saw nothing from a recogniser whose output was all
/// below the confidence floor.
struct OcrStats {
    int boxesDetected = 0;
    int cropsEmpty = 0;
    int recognitionFailed = 0;
    int textEmpty = 0;
    int belowConfidence = 0;
    int boxOutsideImage = 0;
    int accepted = 0;
    /// Highest confidence seen, including lines that were rejected for it.
    double bestConfidence = 0.0;
};

/// PP-OCR (detection + recognition) running on ONNX Runtime.
///
/// The two-stage pipeline mirrors PaddleOCR: a DB detector produces a text
/// probability map that is turned into rotated boxes, then each box is cropped,
/// rectified, and fed to a CRNN recogniser decoded with greedy CTC.
class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    /// Load the models. Returns false and fills `error` when a file is missing
    /// or the graph cannot be initialised.
    bool load(const OcrConfig& config, std::string& error);

    bool isLoaded() const;
    const OcrConfig& config() const;

    /// Run the full pipeline on a BGR image.
    std::vector<OcrLine> recognize(const cv::Mat& bgr, std::string& error);

    /// Breakdown of the most recent `recognize()` call.
    const OcrStats& lastStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rpa::vision
