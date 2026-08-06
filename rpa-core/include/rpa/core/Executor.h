#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rpa/core/Input.h"
#include "rpa/core/Locator.h"
#include "rpa/core/Script.h"
#include "rpa/core/Variables.h"

namespace rpa::core {

enum class RunStatus {
    Queued,
    Running,
    Paused,
    Succeeded,
    Failed,
    Cancelled,
};

std::string toString(RunStatus status);

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string stepId;
    std::string message;
    std::chrono::system_clock::time_point at = std::chrono::system_clock::now();
};

struct StepOutcome {
    bool ok = false;
    bool skipped = false;
    std::string error;
    /// Set when the step's `onFail` policy redirects control flow.
    std::string gotoStepId;
};

struct RunResult {
    RunStatus status = RunStatus::Queued;
    std::string failedStepId;
    std::string error;
    int stepsExecuted = 0;
};

/// Callbacks fire on the executor thread. The Qt layer marshals them to the UI
/// thread; the REST server records them into the run store.
struct ExecutorCallbacks {
    std::function<void(const LogEntry&)> onLog;
    std::function<void(const std::string& stepId)> onStepStarted;
    std::function<void(const std::string& stepId, const StepOutcome&)> onStepFinished;
    std::function<void(RunStatus)> onStatusChanged;
};

struct ExecutorConfig {
    /// Delay inserted after every step; useful for demoing and for UIs that
    /// need time to repaint the highlighted card.
    int stepDelayMs = 0;
    /// Hard ceiling on total loop iterations, so a `while` condition that never
    /// flips can't wedge the run.
    int maxLoopIterations = 10000;
    /// Directory that relative template/screenshot paths resolve against.
    std::string workingDirectory;
};

class Executor {
public:
    Executor(IInputBackend* input, IWindowBackend* window, ITargetLocator* locator);

    void setCallbacks(ExecutorCallbacks callbacks) { callbacks_ = std::move(callbacks); }
    void setConfig(ExecutorConfig config) { config_ = std::move(config); }
    const ExecutorConfig& config() const { return config_; }

    /// Blocking run. `overrides` are merged over the script's own variables,
    /// which is how the REST API injects per-request values.
    RunResult run(const Script& script,
                  const std::map<std::string, std::string>& overrides = {});

    /// Execute exactly one step at `index` of the top-level step list. Used by
    /// the editor's single-step debugging.
    StepOutcome runSingleStep(const Script& script, size_t index);

    void requestPause();
    void requestResume();
    void requestStop();

    RunStatus status() const { return status_.load(); }
    VariableScope& variables() { return vars_; }
    const VariableScope& variables() const { return vars_; }

private:
    struct FlowSignal {
        bool stop = false;
        std::string gotoStepId;
    };

    bool executeList(const StepList& steps, FlowSignal& signal, RunResult& result);
    StepOutcome executeStep(const Step& step, FlowSignal& signal, RunResult& result);

    StepOutcome doClick(const Step& step);
    StepOutcome doTypeText(const Step& step);
    StepOutcome doKeyPress(const Step& step);
    StepOutcome doWait(const Step& step);
    StepOutcome doLocate(const Step& step);
    StepOutcome doWindowActivate(const Step& step);
    StepOutcome doScreenshot(const Step& step);
    StepOutcome doHttpRequest(const Step& step);

    /// Retry wrapper shared by every locating step and by click targets.
    LocateResult locateWithRetry(const Target& target);
    Target expandTarget(const Target& target) const;
    bool evaluate(const Condition& condition);

    /// Returns false when the run was stopped while paused or sleeping.
    bool waitWhilePausedOrSleep(int ms);

    void setStatus(RunStatus status);
    void log(LogLevel level, const std::string& stepId, const std::string& message);

    IInputBackend* input_;
    IWindowBackend* window_;
    ITargetLocator* locator_;

    ExecutorCallbacks callbacks_;
    ExecutorConfig config_;
    VariableScope vars_;

    std::atomic<RunStatus> status_{RunStatus::Queued};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<bool> stopRequested_{false};
};

}  // namespace rpa::core
