#include "rpa/vision/OcrEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <system_error>
#include <utility>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace rpa::vision {

namespace {

namespace fs = std::filesystem;

// ONNX Runtime takes wide paths on Windows and narrow paths elsewhere.
#ifdef _WIN32
std::wstring toOrtPath(const fs::path& path) { return path.wstring(); }
#else
std::string toOrtPath(const fs::path& path) { return path.string(); }
#endif

/// The two PP-OCR stages are trained with *different* normalisation, and using
/// the detector's statistics for recognition yields confident-looking garbage
/// text rather than an obvious failure — so the constants are named per stage.
///
/// Detection: ImageNet mean/std. Recognition: (x/255 - 0.5) / 0.5.
constexpr float kDetectionMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kDetectionStd[3] = {0.229f, 0.224f, 0.225f};
constexpr float kRecognitionMean[3] = {0.5f, 0.5f, 0.5f};
constexpr float kRecognitionStd[3] = {0.5f, 0.5f, 0.5f};

/// Convert an interleaved BGR image into the planar NCHW float tensor the
/// Paddle-exported graphs expect, applying the given channel normalisation.
std::vector<float> toNchwTensor(const cv::Mat& bgr, const float mean[3], const float stddev[3]) {
    CV_Assert(bgr.type() == CV_8UC3);
    const int height = bgr.rows;
    const int width = bgr.cols;

    std::vector<float> tensor(static_cast<size_t>(3) * height * width);
    for (int y = 0; y < height; ++y) {
        const cv::Vec3b* row = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            const cv::Vec3b& pixel = row[x];
            for (int c = 0; c < 3; ++c) {
                // OpenCV stores BGR; the models were trained on RGB.
                const float value = static_cast<float>(pixel[2 - c]) / 255.0f;
                tensor[static_cast<size_t>(c) * height * width +
                       static_cast<size_t>(y) * width + x] = (value - mean[c]) / stddev[c];
            }
        }
    }
    return tensor;
}

/// Round up to the next multiple of 32; the DB detector's decoder halves the
/// spatial dimensions five times and rejects sizes that don't divide evenly.
int roundUpTo32(int value) {
    const int rounded = (value + 31) / 32 * 32;
    return std::max(32, rounded);
}

struct DetectionBox {
    cv::RotatedRect rect;
    double score = 0.0;
};

/// Mean probability inside the candidate box; PaddleOCR uses this to drop
/// boxes that only clipped the edge of a blob.
double boxScore(const cv::Mat& probability, const cv::RotatedRect& rect) {
    cv::Rect bounds = rect.boundingRect() & cv::Rect(0, 0, probability.cols, probability.rows);
    if (bounds.width <= 0 || bounds.height <= 0) return 0.0;

    cv::Point2f corners[4];
    rect.points(corners);

    std::vector<cv::Point> polygon;
    polygon.reserve(4);
    for (const auto& corner : corners) {
        polygon.emplace_back(cvRound(corner.x - bounds.x), cvRound(corner.y - bounds.y));
    }

    cv::Mat mask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
    return cv::mean(probability(bounds), mask)[0];
}

/// Grow the box outward so the crop keeps the full glyph. PaddleOCR offsets the
/// polygon with Clipper; for a rotated rectangle the equivalent offset distance
/// is area * ratio / perimeter, applied to both sides of each dimension.
cv::RotatedRect unclip(const cv::RotatedRect& rect, double ratio) {
    const double area = static_cast<double>(rect.size.width) * rect.size.height;
    const double perimeter = 2.0 * (rect.size.width + rect.size.height);
    if (perimeter <= 0.0) return rect;

    const double distance = area * ratio / perimeter;
    cv::RotatedRect expanded = rect;
    expanded.size.width += static_cast<float>(distance * 2.0);
    expanded.size.height += static_cast<float>(distance * 2.0);
    return expanded;
}

/// Order four corners as top-left, top-right, bottom-right, bottom-left.
///
/// Derived from the geometry rather than read off fixed indices of
/// RotatedRect::points(), whose corner sequence rotates with the rect's angle:
/// minAreaRect describes one horizontal text line as (w, h, 0) and the next as
/// (h, w, 90) depending on its contour, so a fixed index yields a crop turned 90
/// degrees for half the lines on a page. The portrait heuristic below then turns
/// that crop another 90, handing the recogniser upside-down text -- which comes
/// back as low confidence rather than as an error, so nothing points at the cause.
std::array<cv::Point2f, 4> orderCorners(const cv::Point2f corners[4]) {
    std::array<cv::Point2f, 4> points = {corners[0], corners[1], corners[2], corners[3]};

    std::sort(points.begin(), points.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    // Two leftmost and two rightmost, each split by y, which grows downward.
    const bool leftInOrder = points[0].y <= points[1].y;
    const bool rightInOrder = points[2].y <= points[3].y;
    return {leftInOrder ? points[0] : points[1],    // top-left
            rightInOrder ? points[2] : points[3],   // top-right
            rightInOrder ? points[3] : points[2],   // bottom-right
            leftInOrder ? points[1] : points[0]};   // bottom-left
}

/// Warp a rotated box to an upright crop. A crop that came out clearly portrait
/// holds vertically-stacked text, so it is turned a further 90 degrees to read
/// left to right. The threshold and direction match PaddleOCR's
/// `get_rotate_crop_image` (rotate when height/width >= 1.5, via `np.rot90`,
/// which is counter-clockwise) — deviating here would feed the recogniser crops
/// it was never trained on.
cv::Mat cropRotated(const cv::Mat& image, const cv::RotatedRect& rect) {
    cv::Point2f corners[4];
    rect.points(corners);

    const std::array<cv::Point2f, 4> source = orderCorners(corners);

    auto distance = [](const cv::Point2f& a, const cv::Point2f& b) {
        return static_cast<int>(std::round(cv::norm(a - b)));
    };
    int width = std::max(distance(source[0], source[1]), distance(source[3], source[2]));
    int height = std::max(distance(source[0], source[3]), distance(source[1], source[2]));
    width = std::max(width, 1);
    height = std::max(height, 1);

    const std::array<cv::Point2f, 4> destination = {
        cv::Point2f(0, 0),
        cv::Point2f(static_cast<float>(width), 0),
        cv::Point2f(static_cast<float>(width), static_cast<float>(height)),
        cv::Point2f(0, static_cast<float>(height)),
    };

    const cv::Mat transform = cv::getPerspectiveTransform(source.data(), destination.data());
    cv::Mat crop;
    cv::warpPerspective(image, crop, transform, cv::Size(width, height),
                        cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    if (crop.cols > 0 && static_cast<double>(crop.rows) / crop.cols >= 1.5) {
        cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
    }
    return crop;
}

std::vector<std::string> readDictionary(const fs::path& path) {
    std::vector<std::string> entries;
    std::ifstream in(path, std::ios::binary);
    if (!in) return entries;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        entries.push_back(line);
    }
    return entries;
}

}  // namespace

struct OcrEngine::Impl {
    OcrConfig config;
    bool loaded = false;

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "rpa-vision"};
    std::unique_ptr<Ort::Session> detection;
    std::unique_ptr<Ort::Session> recognition;

    std::string detectionInputName;
    std::string detectionOutputName;
    std::string recognitionInputName;
    std::string recognitionOutputName;

    std::vector<std::string> dictionary;
    OcrStats stats;

    Ort::SessionOptions makeOptions() const {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (config.threadCount > 0) {
            options.SetIntraOpNumThreads(config.threadCount);
        }
        return options;
    }

    static std::string firstInputName(Ort::Session& session, Ort::AllocatorWithDefaultOptions& alloc) {
        return session.GetInputNameAllocated(0, alloc).get();
    }

    static std::string firstOutputName(Ort::Session& session, Ort::AllocatorWithDefaultOptions& alloc) {
        return session.GetOutputNameAllocated(0, alloc).get();
    }

    /// Stage 1: probability map -> rotated boxes in original-image coordinates.
    std::vector<DetectionBox> detect(const cv::Mat& bgr, std::string& error);

    /// Stage 2: crop -> CRNN -> greedy CTC decode.
    bool recognizeCrop(const cv::Mat& crop, std::string& text, double& confidence,
                       std::string& error);
};

std::vector<DetectionBox> OcrEngine::Impl::detect(const cv::Mat& bgr, std::string& error) {
    std::vector<DetectionBox> boxes;

    const int longest = std::max(bgr.cols, bgr.rows);
    double scale = 1.0;
    if (longest > config.detectionMaxSideLength) {
        scale = static_cast<double>(config.detectionMaxSideLength) / longest;
    }

    const int resizedWidth = roundUpTo32(static_cast<int>(std::round(bgr.cols * scale)));
    const int resizedHeight = roundUpTo32(static_cast<int>(std::round(bgr.rows * scale)));

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(resizedWidth, resizedHeight), 0, 0, cv::INTER_LINEAR);

    std::vector<float> input = toNchwTensor(resized, kDetectionMean, kDetectionStd);
    const std::array<int64_t, 4> shape = {1, 3, resizedHeight, resizedWidth};

    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        shape.data(), shape.size());

    const char* inputNames[] = {detectionInputName.c_str()};
    const char* outputNames[] = {detectionOutputName.c_str()};

    std::vector<Ort::Value> outputs;
    try {
        outputs = detection->Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
    } catch (const Ort::Exception& e) {
        error = std::string("detection inference failed: ") + e.what();
        return boxes;
    }
    if (outputs.empty()) {
        error = "detection produced no output";
        return boxes;
    }

    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> outShape = info.GetShape();
    if (outShape.size() != 4) {
        error = "unexpected detection output rank";
        return boxes;
    }
    const int mapHeight = static_cast<int>(outShape[2]);
    const int mapWidth = static_cast<int>(outShape[3]);

    const cv::Mat probability(mapHeight, mapWidth, CV_32FC1,
                              const_cast<float*>(outputs[0].GetTensorData<float>()));

    cv::Mat binary;
    cv::threshold(probability, binary, config.detectionThreshold, 255.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // Map from the (possibly padded) network output back to the source image.
    const double scaleX = static_cast<double>(bgr.cols) / mapWidth;
    const double scaleY = static_cast<double>(bgr.rows) / mapHeight;

    for (const auto& contour : contours) {
        if (contour.size() < 4) continue;

        cv::RotatedRect rect = cv::minAreaRect(contour);
        if (std::min(rect.size.width, rect.size.height) < 3.0f) continue;

        const double score = boxScore(probability, rect);
        if (score < config.boxThreshold) continue;

        cv::RotatedRect expanded = unclip(rect, config.unclipRatio);
        expanded.center.x = static_cast<float>(expanded.center.x * scaleX);
        expanded.center.y = static_cast<float>(expanded.center.y * scaleY);
        expanded.size.width = static_cast<float>(expanded.size.width * scaleX);
        expanded.size.height = static_cast<float>(expanded.size.height * scaleY);

        boxes.push_back({expanded, score});
    }

    // Reading order: top to bottom, then left to right within a line.
    //
    // Quantise y into row bands rather than comparing it against a tolerance.
    // A tolerance comparator is intransitive — with a 10px window, y=0, y=5 and
    // y=15 give C<B, B<A but A<C — and std::sort on a comparator that is not a
    // strict weak ordering is undefined behaviour, which MSVC's debug iterators
    // catch as "invalid comparator" and release builds can turn into an
    // out-of-bounds read.
    constexpr double kRowBandPx = 10.0;
    auto sortKey = [](const DetectionBox& box) {
        return std::pair<long long, float>{
            static_cast<long long>(std::floor(box.rect.center.y / kRowBandPx)),
            box.rect.center.x};
    };
    std::sort(boxes.begin(), boxes.end(),
              [&sortKey](const DetectionBox& a, const DetectionBox& b) {
                  return sortKey(a) < sortKey(b);
              });

    return boxes;
}

bool OcrEngine::Impl::recognizeCrop(const cv::Mat& crop, std::string& text, double& confidence,
                                    std::string& error) {
    text.clear();
    confidence = 0.0;
    if (crop.empty()) return true;

    const int height = config.recognitionHeight;
    const int width = std::max(
        16, static_cast<int>(std::round(static_cast<double>(crop.cols) / crop.rows * height)));

    cv::Mat resized;
    cv::resize(crop, resized, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);

    std::vector<float> input = toNchwTensor(resized, kRecognitionMean, kRecognitionStd);
    const std::array<int64_t, 4> shape = {1, 3, height, width};

    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        shape.data(), shape.size());

    const char* inputNames[] = {recognitionInputName.c_str()};
    const char* outputNames[] = {recognitionOutputName.c_str()};

    std::vector<Ort::Value> outputs;
    try {
        outputs = recognition->Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
    } catch (const Ort::Exception& e) {
        error = std::string("recognition inference failed: ") + e.what();
        return false;
    }
    if (outputs.empty()) {
        error = "recognition produced no output";
        return false;
    }

    const std::vector<int64_t> outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (outShape.size() != 3) {
        error = "unexpected recognition output rank";
        return false;
    }
    const int timeSteps = static_cast<int>(outShape[1]);
    const int classes = static_cast<int>(outShape[2]);
    const float* data = outputs[0].GetTensorData<float>();

    // Greedy CTC: argmax per time step, drop blanks (index 0) and repeats.
    double scoreSum = 0.0;
    int scored = 0;
    int previousIndex = -1;

    for (int t = 0; t < timeSteps; ++t) {
        const float* row = data + static_cast<size_t>(t) * classes;
        const int best = static_cast<int>(std::distance(row, std::max_element(row, row + classes)));

        if (best != 0 && best != previousIndex) {
            const int dictIndex = best - 1;
            if (dictIndex >= 0 && dictIndex < static_cast<int>(dictionary.size())) {
                text += dictionary[static_cast<size_t>(dictIndex)];
            }
            scoreSum += row[best];
            ++scored;
        }
        previousIndex = best;
    }

    confidence = scored > 0 ? scoreSum / scored : 0.0;
    return true;
}

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}
OcrEngine::~OcrEngine() = default;

bool OcrEngine::isLoaded() const {
    return impl_->loaded;
}

const OcrConfig& OcrEngine::config() const {
    return impl_->config;
}

bool OcrEngine::load(const OcrConfig& config, std::string& error) {
    impl_->loaded = false;
    impl_->config = config;

    const fs::path root(config.modelDirectory);
    const fs::path detectionPath = root / "det.onnx";
    const fs::path recognitionPath = root / "rec.onnx";
    const fs::path dictionaryPath = root / "keys.txt";

    for (const auto& required : {detectionPath, recognitionPath, dictionaryPath}) {
        std::error_code ec;
        if (!fs::exists(required, ec)) {
            error = "missing OCR model file: " + required.string();
            return false;
        }
    }

    impl_->dictionary = readDictionary(dictionaryPath);
    if (impl_->dictionary.empty()) {
        error = "character dictionary is empty: " + dictionaryPath.string();
        return false;
    }

    try {
        const Ort::SessionOptions options = impl_->makeOptions();
        impl_->detection = std::make_unique<Ort::Session>(
            impl_->env, toOrtPath(detectionPath).c_str(), options);
        impl_->recognition = std::make_unique<Ort::Session>(
            impl_->env, toOrtPath(recognitionPath).c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        impl_->detectionInputName = Impl::firstInputName(*impl_->detection, allocator);
        impl_->detectionOutputName = Impl::firstOutputName(*impl_->detection, allocator);
        impl_->recognitionInputName = Impl::firstInputName(*impl_->recognition, allocator);
        impl_->recognitionOutputName = Impl::firstOutputName(*impl_->recognition, allocator);

        // Reconcile the dictionary against the recogniser's class count. A
        // dictionary that does not belong to the model still decodes -- into
        // confident-looking garbage -- so this has to fail loudly instead.
        const std::vector<int64_t> outShape =
            impl_->recognition->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (outShape.size() == 3 && outShape[2] > 0) {
            const auto classes = static_cast<std::size_t>(outShape[2]);
            const std::size_t entries = impl_->dictionary.size();

            if (classes == entries + 2) {
                // PaddleOCR appends a space to the charset when trained with
                // use_space_char, and does not write it into the dictionary file.
                // Without this the space class decodes to nothing and every
                // recognised line comes back with its words run together.
                impl_->dictionary.emplace_back(" ");
            } else if (classes != entries + 1) {
                error = "character dictionary does not match the recognition model: " +
                        std::to_string(entries) + " entries but the model emits " +
                        std::to_string(classes) +
                        " classes (expected entries + 1 for the CTC blank, or + 2 when the "
                        "charset also has a space). Check that keys.txt belongs to rec.onnx.";
                return false;
            }
        }
    } catch (const Ort::Exception& e) {
        error = std::string("cannot initialise ONNX Runtime session: ") + e.what();
        return false;
    }

    impl_->loaded = true;
    return true;
}

std::vector<OcrLine> OcrEngine::recognize(const cv::Mat& bgr, std::string& error) {
    std::vector<OcrLine> lines;

    if (!impl_->loaded) {
        error = "OCR engine is not loaded";
        return lines;
    }
    if (bgr.empty()) {
        error = "input image is empty";
        return lines;
    }

    impl_->stats = OcrStats{};

    const std::vector<DetectionBox> boxes = impl_->detect(bgr, error);
    if (!error.empty()) return lines;
    impl_->stats.boxesDetected = static_cast<int>(boxes.size());

    int cropIndex = 0;
    for (const auto& box : boxes) {
        const cv::Mat crop = cropRotated(bgr, box.rect);
        if (crop.empty()) {
            ++impl_->stats.cropsEmpty;
            continue;
        }

        if (!impl_->config.debugCropDirectory.empty()) {
            std::error_code ec;
            fs::create_directories(impl_->config.debugCropDirectory, ec);
            const fs::path out = fs::path(impl_->config.debugCropDirectory) /
                                 ("crop_" + std::to_string(cropIndex) + ".png");
            cv::imwrite(out.string(), crop);
        }
        ++cropIndex;

        std::string text;
        double confidence = 0.0;
        std::string cropError;
        if (!impl_->recognizeCrop(crop, text, confidence, cropError)) {
            // One bad crop shouldn't abandon the whole frame.
            ++impl_->stats.recognitionFailed;
            continue;
        }
        impl_->stats.bestConfidence = std::max(impl_->stats.bestConfidence, confidence);

        if (text.empty()) {
            ++impl_->stats.textEmpty;
            continue;
        }
        if (confidence < impl_->config.minConfidence) {
            ++impl_->stats.belowConfidence;
            continue;
        }

        OcrLine line;
        line.text = std::move(text);
        line.confidence = confidence;
        line.box = box.rect.boundingRect() & cv::Rect(0, 0, bgr.cols, bgr.rows);
        if (line.box.width <= 0 || line.box.height <= 0) {
            ++impl_->stats.boxOutsideImage;
            continue;
        }

        ++impl_->stats.accepted;
        lines.push_back(std::move(line));
    }

    return lines;
}

const OcrStats& OcrEngine::lastStats() const {
    return impl_->stats;
}

}  // namespace rpa::vision
