#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "rpa/core/Executor.h"
#include "rpa/core/Input.h"
#include "rpa/core/Locator.h"

namespace rpa::studio {

/// Everything a finished run leaves behind.
///
/// The script travels with the outcome rather than being read back from the
/// editor afterwards: a run started over REST can finish while the user has an
/// entirely different flow open, and the diagnosis has to describe what ran.
struct RunOutcome {
    QString runId;
    core::RunResult result;
    core::Script script;
    /// Empty unless the run failed and the capture succeeded.
    QString failureScreenshotPath;
};

/// Owns the executor and runs it off the UI thread.
///
/// The executor blocks — a flow with waits and retries can run for minutes — so
/// it lives on its own thread and reports back through queued signals. This is
/// also the single point the REST server funnels run requests through, which is
/// what keeps "only one flow at a time" true regardless of who asked.
class ExecutionController : public QObject {
    Q_OBJECT

public:
    explicit ExecutionController(QObject* parent = nullptr);
    ~ExecutionController() override;

    /// Backends are owned by the caller and must outlive the controller.
    void setBackends(core::IInputBackend* input,
                     core::IWindowBackend* window,
                     core::ITargetLocator* locator);
    void setWorkingDirectory(const QString& directory);
    void setStepDelayMs(int delayMs);
    /// Where to drop a screenshot of the screen a failing run left behind.
    /// Empty disables the capture.
    void setFailureScreenshotDirectory(const QString& directory);

    /// Begin a run. Returns false with a reason when one is already in flight.
    bool start(const core::Script& script,
               const std::map<std::string, std::string>& overrides,
               const QString& runId,
               const QString& source,
               QString& reason);

    /// Execute one top-level step without starting a full run.
    bool stepOver(const core::Script& script, size_t index, QString& reason);

    void pause();
    void resume();
    void stop();

    bool isBusy() const { return busy_.load(); }
    core::RunStatus status() const;
    QString activeRunId() const;

signals:
    void statusChanged(rpa::core::RunStatus status);
    void stepStarted(const QString& stepId);
    void stepFinished(const QString& stepId, bool ok, const QString& error);
    void logged(int level, const QString& stepId, const QString& message);
    void runFinished(const rpa::studio::RunOutcome& outcome);

private:
    void joinWorker();

    std::unique_ptr<core::Executor> executor_;
    core::IInputBackend* input_ = nullptr;
    core::IWindowBackend* window_ = nullptr;
    core::ITargetLocator* locator_ = nullptr;

    std::thread worker_;
    std::atomic<bool> busy_{false};

    mutable std::mutex stateMutex_;
    QString activeRunId_;
    QString workingDirectory_;
    QString failureScreenshotDirectory_;
    int stepDelayMs_ = 0;
};

}  // namespace rpa::studio

Q_DECLARE_METATYPE(rpa::core::RunStatus)
Q_DECLARE_METATYPE(rpa::core::RunResult)
Q_DECLARE_METATYPE(rpa::studio::RunOutcome)
