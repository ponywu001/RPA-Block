/// Headless entry point for CI and for scripted use.
///
/// `validate` needs nothing beyond rpa-core, which is what makes it usable as a
/// pre-commit or CI check on a machine with no display and no vision models.

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "rpa/core/Executor.h"
#include "rpa/core/Input.h"
#include "rpa/core/Locator.h"
#include "rpa/core/ScriptIO.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef RPA_CLI_WITH_VISION
#include <chrono>
#include <filesystem>
#include <iomanip>

#include <opencv2/imgcodecs.hpp>

#include "rpa/core/TextMatch.h"
#include "rpa/vision/OcrEngine.h"
#endif

#ifdef RPA_CLI_WITH_SERVER
#include "rpa/server/ApiServer.h"
#include "rpa/server/RunStore.h"
#include "rpa/server/ScriptRepository.h"
#endif

namespace {

#ifdef _WIN32
/// Re-read the command line as UTF-8.
///
/// `main`'s argv arrives in the system ANSI code page, so on a machine whose ANSI
/// page is not UTF-8 every non-ASCII argument is already mangled before main
/// runs: `--find 搜尋` shows up as `--find \xE6\x90\x9C`-turned-mojibake, and so
/// does a Chinese variable value passed to `run`. The wide command line is the
/// only lossless source.
std::vector<std::string> commandLineUtf8() {
    std::vector<std::string> args;

    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return args;

    args.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int bytes =
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1) {
            args.emplace_back();
            continue;
        }
        std::string utf8(static_cast<std::size_t>(bytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8.data(), bytes, nullptr, nullptr);
        args.push_back(std::move(utf8));
    }
    LocalFree(wide);
    return args;
}
#endif

std::atomic<bool> g_stopRequested{false};

void handleSignal(int) {
    g_stopRequested.store(true);
}

int usage() {
    std::cout <<
        R"(RPA-Block CLI

Usage:
  rpa-cli validate <flow.rpa.json>...     Parse and validate flows; non-zero on any issue
  rpa-cli show <flow.rpa.json>            Re-serialize a flow (canonical formatting)
  rpa-cli run <flow.rpa.json> [k=v ...]   Execute a flow, overriding variables
  rpa-cli serve <dir> [--port N] [--key K]  Serve a folder of flows over REST
  rpa-cli ocr <image> [--models DIR]      Read text from an image and print it
           [--find TEXT] [--match MODE]   ...and report whether TEXT would match

Notes:
  `run` drives the real mouse and keyboard. OCR and image targets need the
  vision module, which this binary does not link; they will report
  "vision module not available" and fail their step.

  `ocr` exists to check the OCR setup without the GUI: it loads the models,
  runs the full detect-then-recognise pipeline, and prints every line with its
  box and confidence. --models defaults to the `models` folder beside this exe.
  --find runs the executor's own matching against the result and, when it does
  not match, names the closest lines -- which is usually enough to see why.
  --match is exact | contains | regex, defaulting to contains.
)";
    return 2;
}

int validate(const std::vector<std::string>& paths) {
    int failures = 0;

    for (const auto& path : paths) {
        const rpa::core::ParseResult parsed = rpa::core::loadScriptFile(path);

        if (!parsed.ok) {
            std::cout << "FAIL  " << path << "\n      " << parsed.error << "\n";
            ++failures;
            continue;
        }

        if (parsed.issues.empty()) {
            std::cout << "ok    " << path << "  (" << parsed.script.steps.size() << " steps)\n";
            continue;
        }

        std::cout << "FAIL  " << path << "  (" << parsed.issues.size() << " issue(s))\n";
        for (const auto& issue : parsed.issues) {
            std::cout << "      ";
            if (!issue.stepId.empty()) std::cout << "[" << issue.stepId << "] ";
            std::cout << issue.message << "\n";
        }
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}

int show(const std::string& path) {
    const rpa::core::ParseResult parsed = rpa::core::loadScriptFile(path);
    if (!parsed.ok) {
        std::cerr << "error: " << parsed.error << "\n";
        return 1;
    }
    std::cout << rpa::core::serializeScript(parsed.script, true) << "\n";
    return 0;
}

int run(const std::string& path, const std::vector<std::string>& assignments) {
    const rpa::core::ParseResult parsed = rpa::core::loadScriptFile(path);
    if (!parsed.ok) {
        std::cerr << "error: " << parsed.error << "\n";
        return 1;
    }
    for (const auto& issue : parsed.issues) {
        std::cerr << "warning: ";
        if (!issue.stepId.empty()) std::cerr << "[" << issue.stepId << "] ";
        std::cerr << issue.message << "\n";
    }

    std::map<std::string, std::string> overrides;
    for (const auto& assignment : assignments) {
        const size_t equals = assignment.find('=');
        if (equals == std::string::npos) {
            std::cerr << "error: expected key=value, got: " << assignment << "\n";
            return 2;
        }
        overrides[assignment.substr(0, equals)] = assignment.substr(equals + 1);
    }

#ifdef _WIN32
    auto input = rpa::core::makeWin32InputBackend();
    auto window = rpa::core::makeWin32WindowBackend();
#else
    std::cerr << "error: running flows is only implemented on Windows\n";
    return 2;
#endif

    rpa::core::NullTargetLocator locator;
    rpa::core::Executor executor(input.get(), window.get(), &locator);

    rpa::core::ExecutorCallbacks callbacks;
    callbacks.onLog = [](const rpa::core::LogEntry& entry) {
        static const char* kLevels[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
        std::cout << kLevels[static_cast<int>(entry.level)] << "  ";
        if (!entry.stepId.empty()) std::cout << "[" << entry.stepId << "] ";
        std::cout << entry.message << "\n";
    };
    executor.setCallbacks(std::move(callbacks));

    std::signal(SIGINT, handleSignal);

    // Watch for Ctrl+C on a helper thread so a flow that grabs the pointer can
    // still be stopped from the terminal.
    std::thread watchdog([&executor] {
        while (!g_stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (executor.status() == rpa::core::RunStatus::Succeeded ||
                executor.status() == rpa::core::RunStatus::Failed ||
                executor.status() == rpa::core::RunStatus::Cancelled) {
                return;
            }
        }
        executor.requestStop();
    });

    const rpa::core::RunResult result = executor.run(parsed.script, overrides);

    g_stopRequested.store(true);
    watchdog.join();

    std::cout << "\nresult: " << rpa::core::toString(result.status) << "  (" << result.stepsExecuted
              << " steps executed)\n";
    if (!result.error.empty()) {
        std::cout << "error:  " << result.error;
        if (!result.failedStepId.empty()) std::cout << "  at step " << result.failedStepId;
        std::cout << "\n";
    }

    return result.status == rpa::core::RunStatus::Succeeded ? 0 : 1;
}

#ifdef RPA_CLI_WITH_SERVER

int serve(const std::string& directory, int port, const std::string& apiKey) {
    rpa::server::ScriptRepository repository;
    repository.setDirectory(directory);

    std::vector<std::string> errors;
    const size_t loaded = repository.reload(&errors);
    for (const auto& error : errors) std::cerr << "warning: " << error << "\n";
    std::cout << "loaded " << loaded << " flow(s) from " << directory << "\n";

    rpa::server::RunStore runStore;
    rpa::server::ApiServer api(&repository, &runStore);

#ifdef _WIN32
    auto input = rpa::core::makeWin32InputBackend();
    auto window = rpa::core::makeWin32WindowBackend();
    rpa::core::NullTargetLocator locator;
    rpa::core::Executor executor(input.get(), window.get(), &locator);

    std::atomic<bool> busy{false};

    api.setRunHandler([&](const rpa::server::RunRequest& request, std::string& reason) {
        if (busy.exchange(true)) {
            reason = "a flow is already running";
            return false;
        }
        // Detached so the dispatcher is not blocked for the length of the run;
        // the caller polls GET /runs/{id} for the outcome.
        std::thread([&, request] {
            const rpa::core::RunResult result = executor.run(request.script, request.variables);
            // Cleared before complete(), not after: complete() is what releases
            // the server's queue, and the dispatcher can hand us the next run
            // before the next statement would have run.
            busy.store(false);
            runStore.complete(request.runId, result);
        }).detach();
        return true;
    });
#else
    std::cerr << "warning: flow execution is Windows-only; this server will accept "
                 "requests but reject every run\n";
#endif

    rpa::server::ApiServerConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = port;
    if (!apiKey.empty()) config.apiKeys.insert(apiKey);

    std::string error;
    if (!api.start(config, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }

    std::cout << "listening on http://127.0.0.1:" << api.boundPort() << "/api/v1\n";
    if (apiKey.empty()) {
        std::cerr << "warning: no --key was given, so every authenticated endpoint will "
                     "return 401\n";
    }
    std::cout << "press Ctrl+C to stop\n";

    std::signal(SIGINT, handleSignal);
    while (!g_stopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    api.stop();
    std::cout << "stopped\n";
    return 0;
}

#endif  // RPA_CLI_WITH_SERVER

#ifdef RPA_CLI_WITH_VISION

int ocr(const std::string& imagePath, std::string modelDirectory,
        const std::string& cropDirectory, const std::string& executablePath,
        const std::string& needle, rpa::core::MatchMode mode) {
    if (modelDirectory.empty()) {
        // Beside the executable, matching where the studio looks and where the
        // packaged exe unpacks them.
        const std::filesystem::path exe(executablePath);
        modelDirectory = (exe.parent_path() / "models").string();
    }

    const cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "error: cannot read image: " << imagePath << "\n";
        return 1;
    }
    std::cout << "image  " << imagePath << "  (" << image.cols << "x" << image.rows << ")\n";
    std::cout << "models " << modelDirectory << "\n";

    rpa::vision::OcrConfig config;
    config.modelDirectory = modelDirectory;
    config.debugCropDirectory = cropDirectory;

    rpa::vision::OcrEngine engine;
    std::string error;

    const auto loadStart = std::chrono::steady_clock::now();
    if (!engine.load(config, error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    const auto loaded = std::chrono::steady_clock::now();

    const std::vector<rpa::vision::OcrLine> lines = engine.recognize(image, error);
    const auto recognised = std::chrono::steady_clock::now();

    const auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    };
    std::cout << "load   " << ms(loadStart, loaded) << " ms\n"
              << "read   " << ms(loaded, recognised) << " ms\n";

    if (!error.empty()) std::cout << "note   " << error << "\n";

    const rpa::vision::OcrStats& stats = engine.lastStats();
    std::cout << "\npipeline:\n"
              << "  boxes detected      " << stats.boxesDetected << "\n"
              << "  crops empty         " << stats.cropsEmpty << "\n"
              << "  recognition failed  " << stats.recognitionFailed << "\n"
              << "  decoded to nothing  " << stats.textEmpty << "\n"
              << "  below confidence    " << stats.belowConfidence << "\n"
              << "  box outside image   " << stats.boxOutsideImage << "\n"
              << "  accepted            " << stats.accepted << "\n"
              << "  best confidence     " << std::fixed << std::setprecision(3)
              << stats.bestConfidence << "\n";

    if (lines.empty()) {
        std::cout << "\nno text found\n";
        return 1;
    }

    std::cout << "\n" << lines.size() << " line(s):\n";
    for (const rpa::vision::OcrLine& line : lines) {
        std::cout << "  [" << std::fixed << std::setprecision(2) << line.confidence << "]  ("
                  << line.box.x << "," << line.box.y << " " << line.box.width << "x"
                  << line.box.height << ")  " << line.text << "\n";
    }

    if (needle.empty()) return 0;

    // Same matching the executor uses, so this answers "why does my anchor not
    // match?" without having to reproduce it through the GUI.
    std::cout << "\nlooking for '" << needle << "' (" << rpa::core::toString(mode) << "):\n";

    const rpa::vision::OcrLine* best = nullptr;
    for (const rpa::vision::OcrLine& line : lines) {
        if (!rpa::core::textMatches(line.text, needle, mode)) continue;
        if (!best || line.confidence > best->confidence) best = &line;
    }

    if (best) {
        const int cx = best->box.x + best->box.width / 2;
        const int cy = best->box.y + best->box.height / 2;
        std::cout << "  MATCH  '" << best->text << "'  confidence " << std::fixed
                  << std::setprecision(2) << best->confidence << "  would click (" << cx << ","
                  << cy << ")\n";
        return 0;
    }

    std::cout << "  no match.\n";
    std::vector<std::string> texts;
    texts.reserve(lines.size());
    for (const rpa::vision::OcrLine& line : lines) texts.push_back(line.text);
    for (const std::string& candidate : rpa::core::nearestTexts(texts, needle)) {
        std::cout << "  closest: '" << candidate << "'\n";
    }
    return 1;
}

#endif  // RPA_CLI_WITH_VISION

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Stdout too: without this, printing a recognised Chinese line to the console
    // produces the same mojibake the arguments used to arrive as.
    SetConsoleOutputCP(CP_UTF8);

    const std::vector<std::string> args = commandLineUtf8();
    const std::vector<std::string> arguments =
        args.empty() ? std::vector<std::string>(argv, argv + argc) : args;
#else
    const std::vector<std::string> arguments(argv, argv + argc);
#endif

    if (arguments.size() < 2) return usage();

    const std::string command = arguments[1];
    std::vector<std::string> rest(arguments.begin() + 2, arguments.end());

    if (command == "validate") {
        if (rest.empty()) return usage();
        return validate(rest);
    }
    if (command == "show") {
        if (rest.size() != 1) return usage();
        return show(rest[0]);
    }
    if (command == "run") {
        if (rest.empty()) return usage();
        return run(rest[0], std::vector<std::string>(rest.begin() + 1, rest.end()));
    }
#ifdef RPA_CLI_WITH_SERVER
    if (command == "serve") {
        if (rest.empty()) return usage();

        int port = 8420;
        std::string apiKey;
        for (size_t i = 1; i < rest.size(); ++i) {
            if (rest[i] == "--port" && i + 1 < rest.size()) {
                port = std::stoi(rest[++i]);
            } else if (rest[i] == "--key" && i + 1 < rest.size()) {
                apiKey = rest[++i];
            } else {
                std::cerr << "error: unexpected argument: " << rest[i] << "\n";
                return usage();
            }
        }
        return serve(rest[0], port, apiKey);
    }
#endif
#ifdef RPA_CLI_WITH_VISION
    if (command == "ocr") {
        if (rest.empty()) return usage();

        std::string models;
        std::string crops;
        std::string needle;
        rpa::core::MatchMode mode = rpa::core::MatchMode::Contains;
        for (std::size_t i = 1; i < rest.size(); ++i) {
            if (rest[i] == "--models" && i + 1 < rest.size()) {
                models = rest[++i];
            } else if (rest[i] == "--dump-crops" && i + 1 < rest.size()) {
                crops = rest[++i];
            } else if (rest[i] == "--find" && i + 1 < rest.size()) {
                needle = rest[++i];
            } else if (rest[i] == "--match" && i + 1 < rest.size()) {
                if (!rpa::core::parseMatchMode(rest[++i], mode)) {
                    std::cerr << "error: --match must be exact, contains or regex\n";
                    return usage();
                }
            } else {
                std::cerr << "error: unexpected argument: " << rest[i] << "\n";
                return usage();
            }
        }
        return ocr(rest[0], models, crops, arguments[0], needle, mode);
    }
#endif

    std::cerr << "error: unknown command: " << command << "\n";
    return usage();
}
