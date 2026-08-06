#include "ExecutionController.h"

#include <QMetaObject>

namespace rpa::studio {

ExecutionController::ExecutionController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<rpa::core::RunStatus>("rpa::core::RunStatus");
    qRegisterMetaType<rpa::core::RunResult>("rpa::core::RunResult");
}

ExecutionController::~ExecutionController() {
    stop();
    joinWorker();
}

void ExecutionController::setBackends(core::IInputBackend* input,
                                      core::IWindowBackend* window,
                                      core::ITargetLocator* locator) {
    input_ = input;
    window_ = window;
    locator_ = locator;

    executor_ = std::make_unique<core::Executor>(input_, window_, locator_);

    // Callbacks arrive on the executor thread. Emitting Qt signals from there
    // is safe because the connections are queued; the UI thread does the work.
    core::ExecutorCallbacks callbacks;
    callbacks.onStatusChanged = [this](core::RunStatus status) {
        emit statusChanged(status);
    };
    callbacks.onStepStarted = [this](const std::string& stepId) {
        emit stepStarted(QString::fromStdString(stepId));
    };
    callbacks.onStepFinished = [this](const std::string& stepId, const core::StepOutcome& outcome) {
        emit stepFinished(QString::fromStdString(stepId), outcome.ok,
                          QString::fromStdString(outcome.error));
    };
    callbacks.onLog = [this](const core::LogEntry& entry) {
        emit logged(static_cast<int>(entry.level), QString::fromStdString(entry.stepId),
                    QString::fromStdString(entry.message));
    };
    executor_->setCallbacks(std::move(callbacks));
}

void ExecutionController::setWorkingDirectory(const QString& directory) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    workingDirectory_ = directory;
}

void ExecutionController::setStepDelayMs(int delayMs) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    stepDelayMs_ = delayMs;
}

core::RunStatus ExecutionController::status() const {
    return executor_ ? executor_->status() : core::RunStatus::Queued;
}

QString ExecutionController::activeRunId() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return activeRunId_;
}

void ExecutionController::joinWorker() {
    if (worker_.joinable()) worker_.join();
}

bool ExecutionController::start(const core::Script& script,
                                const std::map<std::string, std::string>& overrides,
                                const QString& runId,
                                const QString& source,
                                QString& reason) {
    if (!executor_) {
        reason = QStringLiteral("尚未設定執行後端。");
        return false;
    }
    if (script.steps.empty()) {
        reason = QStringLiteral("這個流程沒有任何積木可以執行。");
        return false;
    }
    // exchange() is the gate: whoever flips false->true owns the run, so a UI
    // click and an API call racing here cannot both start.
    if (busy_.exchange(true)) {
        reason = QStringLiteral("已經有流程在執行中。");
        return false;
    }

    joinWorker();

    core::ExecutorConfig config = executor_->config();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        activeRunId_ = runId;
        config.workingDirectory = workingDirectory_.toStdString();
        config.stepDelayMs = stepDelayMs_;
    }
    executor_->setConfig(config);

    Q_UNUSED(source);

    worker_ = std::thread([this, script, overrides, runId]() {
        const core::RunResult result = executor_->run(script, overrides);
        busy_.store(false);
        emit runFinished(runId, result);
    });

    return true;
}

bool ExecutionController::stepOver(const core::Script& script, size_t index, QString& reason) {
    if (!executor_) {
        reason = QStringLiteral("尚未設定執行後端。");
        return false;
    }
    if (busy_.exchange(true)) {
        reason = QStringLiteral("已經有流程在執行中。");
        return false;
    }

    joinWorker();

    worker_ = std::thread([this, script, index]() {
        executor_->runSingleStep(script, index);
        busy_.store(false);
        // runSingleStep's own last act is a statusChanged emit, which happens
        // *before* the store above — so a UI handler reading isBusy() from it
        // can still see true and leave the Run/Step buttons disabled with
        // nothing following to correct them. Re-emit after clearing the latch.
        emit statusChanged(executor_->status());
    });

    return true;
}

void ExecutionController::pause() {
    if (executor_) executor_->requestPause();
}

void ExecutionController::resume() {
    if (executor_) executor_->requestResume();
}

void ExecutionController::stop() {
    if (executor_) executor_->requestStop();
}

}  // namespace rpa::studio
