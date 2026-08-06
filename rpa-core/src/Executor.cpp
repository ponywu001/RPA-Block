#include "rpa/core/Executor.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "rpa/core/Http.h"

namespace rpa::core {

namespace {
constexpr int kPollIntervalMs = 25;
}  // namespace

std::string toString(RunStatus status) {
    switch (status) {
        case RunStatus::Queued: return "queued";
        case RunStatus::Running: return "running";
        case RunStatus::Paused: return "paused";
        case RunStatus::Succeeded: return "succeeded";
        case RunStatus::Failed: return "failed";
        case RunStatus::Cancelled: return "cancelled";
    }
    return "queued";
}

Executor::Executor(IInputBackend* input, IWindowBackend* window, ITargetLocator* locator)
    : input_(input), window_(window), locator_(locator) {}

void Executor::requestPause() {
    pauseRequested_.store(true);
}

void Executor::requestResume() {
    pauseRequested_.store(false);
}

void Executor::requestStop() {
    stopRequested_.store(true);
    pauseRequested_.store(false);
}

void Executor::setStatus(RunStatus status) {
    status_.store(status);
    if (callbacks_.onStatusChanged) callbacks_.onStatusChanged(status);
}

void Executor::log(LogLevel level, const std::string& stepId, const std::string& message) {
    if (!callbacks_.onLog) return;
    LogEntry entry;
    entry.level = level;
    entry.stepId = stepId;
    entry.message = message;
    callbacks_.onLog(entry);
}

bool Executor::waitWhilePausedOrSleep(int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    bool announcedPause = false;

    while (true) {
        if (stopRequested_.load()) return false;

        if (pauseRequested_.load()) {
            if (!announcedPause) {
                announcedPause = true;
                setStatus(RunStatus::Paused);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
            continue;
        }

        if (announcedPause) {
            announcedPause = false;
            setStatus(RunStatus::Running);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return true;

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(std::min<long long>(remaining, kPollIntervalMs)));
    }
}

Target Executor::expandTarget(const Target& target) const {
    Target t = target;
    t.text = vars_.expand(t.text);
    t.templatePath = vars_.expand(t.templatePath);
    return t;
}

LocateResult Executor::locateWithRetry(const Target& target) {
    const Target expanded = expandTarget(target);
    const int attempts = std::max(1, expanded.retry.times);

    LocateResult last;
    for (int i = 0; i < attempts; ++i) {
        if (stopRequested_.load()) {
            last.error = "stopped";
            return last;
        }
        last = locator_->locate(expanded);
        if (last.found) return last;
        if (i + 1 < attempts) {
            if (!waitWhilePausedOrSleep(expanded.retry.intervalMs)) {
                last.error = "stopped";
                return last;
            }
        }
    }
    return last;
}

bool Executor::evaluate(const Condition& condition) {
    switch (condition.kind) {
        case Condition::Kind::OcrFound:
        case Condition::Kind::ImageFound:
            return locateWithRetry(condition.target).found;
        case Condition::Kind::VarEquals: {
            auto value = vars_.get(condition.variable);
            return value.has_value() && *value == vars_.expand(condition.value);
        }
        case Condition::Kind::VarContains: {
            auto value = vars_.get(condition.variable);
            return value.has_value() &&
                   value->find(vars_.expand(condition.value)) != std::string::npos;
        }
    }
    return false;
}

StepOutcome Executor::doClick(const Step& step) {
    StepOutcome outcome;
    Point p;

    if (step.target.kind == TargetKind::Point) {
        p = step.target.point;
        p.x += step.target.offsetX;
        p.y += step.target.offsetY;
    } else {
        const LocateResult hit = locateWithRetry(step.target);
        if (!hit.found) {
            outcome.error = hit.error.empty() ? "target not found on screen" : hit.error;
            return outcome;
        }
        p = hit.point;
        vars_.set("last_match_x", std::to_string(hit.point.x));
        vars_.set("last_match_y", std::to_string(hit.point.y));
        if (!hit.matchedText.empty()) vars_.set("last_match", hit.matchedText);
    }

    const int count = step.type == StepType::DoubleClick ? 2 : std::max(1, step.clickCount);
    std::string error;
    if (!input_->click(p, step.button, count, error)) {
        outcome.error = error;
        return outcome;
    }

    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doTypeText(const Step& step) {
    StepOutcome outcome;
    std::vector<std::string> missing;
    const std::string text = vars_.expand(step.text, &missing);
    for (const auto& name : missing) {
        log(LogLevel::Warn, step.id, "undefined variable in text: {{" + name + "}}");
    }

    std::string error;
    if (!input_->typeText(text, step.intervalMs, error)) {
        outcome.error = error;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doKeyPress(const Step& step) {
    StepOutcome outcome;
    std::string error;
    if (!input_->pressKeys(step.keys, error)) {
        outcome.error = error;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doWait(const Step& step) {
    StepOutcome outcome;
    if (!waitWhilePausedOrSleep(step.waitMs)) {
        outcome.error = "stopped";
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doLocate(const Step& step) {
    StepOutcome outcome;

    Target target = step.target;
    target.kind = step.type == StepType::OcrFind ? TargetKind::Ocr : TargetKind::Image;

    const LocateResult hit = locateWithRetry(target);
    const std::string var = step.saveToVar.empty() ? "last_match" : step.saveToVar;

    if (!hit.found) {
        vars_.set(var + "_found", "false");
        outcome.error = hit.error.empty() ? "target not found on screen" : hit.error;
        return outcome;
    }

    vars_.set(var, hit.matchedText.empty() ? target.text : hit.matchedText);
    vars_.set(var + "_found", "true");
    vars_.set(var + "_x", std::to_string(hit.point.x));
    vars_.set(var + "_y", std::to_string(hit.point.y));
    vars_.set(var + "_confidence", std::to_string(hit.confidence));

    log(LogLevel::Info, step.id,
        "located at (" + std::to_string(hit.point.x) + ", " + std::to_string(hit.point.y) +
            ") confidence " + std::to_string(hit.confidence));

    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doWindowActivate(const Step& step) {
    StepOutcome outcome;
    std::string error;
    const std::string title = vars_.expand(step.titleMatch);
    if (!window_->activateWindow(title, step.titleMatchMode, error)) {
        outcome.error = error.empty() ? "window not found: " + title : error;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doScreenshot(const Step& step) {
    StepOutcome outcome;
    std::string error;
    const std::string path = vars_.expand(step.path);
    if (!locator_->captureToFile(path, step.target.region, error)) {
        outcome.error = error;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::doHttpRequest(const Step& step) {
    StepOutcome outcome;

    std::map<std::string, std::string> headers;
    for (const auto& [key, value] : step.headers) {
        headers[key] = vars_.expand(value);
    }

    const HttpResponse response = httpRequest(step.httpMethod,
                                              vars_.expand(step.url),
                                              headers,
                                              vars_.expand(step.body));
    if (!response.ok) {
        outcome.error = response.error;
        return outcome;
    }

    if (!step.saveToVar.empty()) {
        vars_.set(step.saveToVar, response.body);
        vars_.set(step.saveToVar + "_status", std::to_string(response.statusCode));
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        outcome.error = "HTTP " + std::to_string(response.statusCode);
        return outcome;
    }

    outcome.ok = true;
    return outcome;
}

StepOutcome Executor::executeStep(const Step& step, FlowSignal& signal, RunResult& result) {
    StepOutcome outcome;

    if (!step.enabled) {
        outcome.ok = true;
        outcome.skipped = true;
        return outcome;
    }

    if (callbacks_.onStepStarted) callbacks_.onStepStarted(step.id);

    switch (step.type) {
        case StepType::Click:
        case StepType::DoubleClick:
            outcome = doClick(step);
            break;
        case StepType::TypeText:
            outcome = doTypeText(step);
            break;
        case StepType::KeyPress:
            outcome = doKeyPress(step);
            break;
        case StepType::Wait:
            outcome = doWait(step);
            break;
        case StepType::OcrFind:
        case StepType::ImageFind:
            outcome = doLocate(step);
            break;
        case StepType::WindowActivate:
            outcome = doWindowActivate(step);
            break;
        case StepType::Screenshot:
            outcome = doScreenshot(step);
            break;
        case StepType::HttpRequest:
            outcome = doHttpRequest(step);
            break;
        case StepType::If: {
            const bool taken = step.condition ? evaluate(*step.condition) : false;
            log(LogLevel::Debug, step.id, taken ? "condition true" : "condition false");
            const StepList& branch = taken ? step.thenSteps : step.elseSteps;
            outcome.ok = executeList(branch, signal, result);
            if (!outcome.ok && signal.gotoStepId.empty() && !signal.stop) {
                outcome.error = "branch failed";
            }
            break;
        }
        case StepType::Loop: {
            outcome.ok = true;
            int iterations = 0;
            while (true) {
                if (stopRequested_.load()) {
                    signal.stop = true;
                    outcome.ok = false;
                    outcome.error = "stopped";
                    break;
                }
                if (iterations >= config_.maxLoopIterations) {
                    outcome.ok = false;
                    outcome.error = "loop exceeded maxLoopIterations (" +
                                    std::to_string(config_.maxLoopIterations) + ")";
                    break;
                }
                if (step.whileCondition) {
                    if (!evaluate(*step.whileCondition)) break;
                } else if (iterations >= step.loopCount) {
                    break;
                }

                vars_.set("loop_index", std::to_string(iterations));
                if (!executeList(step.loopSteps, signal, result)) {
                    outcome.ok = false;
                    if (outcome.error.empty()) outcome.error = "loop body failed";
                    break;
                }
                if (signal.stop || !signal.gotoStepId.empty()) break;
                ++iterations;
            }
            break;
        }
    }

    if (callbacks_.onStepFinished) callbacks_.onStepFinished(step.id, outcome);

    if (!outcome.skipped) ++result.stepsExecuted;

    if (!outcome.ok && !signal.stop) {
        log(LogLevel::Error, step.id, outcome.error);
        switch (step.onFail) {
            case FailurePolicy::Continue:
                log(LogLevel::Warn, step.id, "on_fail=continue, moving on");
                outcome.ok = true;
                break;
            case FailurePolicy::Goto:
                log(LogLevel::Warn, step.id, "on_fail=goto:" + step.onFailGoto);
                signal.gotoStepId = step.onFailGoto;
                outcome.gotoStepId = step.onFailGoto;
                outcome.ok = true;
                break;
            case FailurePolicy::Abort:
                result.failedStepId = step.id;
                result.error = outcome.error;
                break;
        }
    }

    return outcome;
}

bool Executor::executeList(const StepList& steps, FlowSignal& signal, RunResult& result) {
    size_t index = 0;
    while (index < steps.size()) {
        if (stopRequested_.load()) {
            signal.stop = true;
            return false;
        }
        if (!waitWhilePausedOrSleep(0)) {
            signal.stop = true;
            return false;
        }

        const Step& step = steps[index];
        const StepOutcome outcome = executeStep(step, signal, result);

        if (signal.stop) return false;

        if (!signal.gotoStepId.empty()) {
            // Resolve the jump within this list; otherwise propagate upward so
            // an enclosing list can handle it.
            auto it = std::find_if(steps.begin(), steps.end(), [&](const Step& s) {
                return s.id == signal.gotoStepId;
            });
            if (it == steps.end()) return true;
            signal.gotoStepId.clear();
            index = static_cast<size_t>(std::distance(steps.begin(), it));
            continue;
        }

        if (!outcome.ok) return false;

        if (config_.stepDelayMs > 0 && !waitWhilePausedOrSleep(config_.stepDelayMs)) {
            signal.stop = true;
            return false;
        }

        ++index;
    }
    return true;
}

RunResult Executor::run(const Script& script,
                        const std::map<std::string, std::string>& overrides) {
    RunResult result;

    stopRequested_.store(false);
    pauseRequested_.store(false);

    vars_.clear();
    for (const auto& [key, value] : script.variables) vars_.set(key, value);
    for (const auto& [key, value] : overrides) vars_.set(key, value);

    setStatus(RunStatus::Running);
    log(LogLevel::Info, "", "run started: " + script.name);

    FlowSignal signal;
    const bool ok = executeList(script.steps, signal, result);

    if (stopRequested_.load()) {
        result.status = RunStatus::Cancelled;
        if (result.error.empty()) result.error = "cancelled by request";
    } else if (ok) {
        result.status = RunStatus::Succeeded;
    } else {
        result.status = RunStatus::Failed;
        if (result.error.empty()) result.error = "run failed";
    }

    setStatus(result.status);
    log(result.status == RunStatus::Succeeded ? LogLevel::Info : LogLevel::Error, "",
        "run finished: " + toString(result.status) +
            (result.error.empty() ? "" : " (" + result.error + ")"));

    return result;
}

StepOutcome Executor::runSingleStep(const Script& script, size_t index) {
    StepOutcome outcome;
    if (index >= script.steps.size()) {
        outcome.error = "step index out of range";
        return outcome;
    }

    stopRequested_.store(false);
    pauseRequested_.store(false);

    // Seed declared variables on the first single step so a partially-executed
    // flow still sees them; subsequent calls keep whatever earlier steps wrote.
    for (const auto& [key, value] : script.variables) {
        if (!vars_.has(key)) vars_.set(key, value);
    }

    RunResult result;
    FlowSignal signal;
    setStatus(RunStatus::Running);
    outcome = executeStep(script.steps[index], signal, result);
    setStatus(outcome.ok ? RunStatus::Paused : RunStatus::Failed);
    return outcome;
}

}  // namespace rpa::core
