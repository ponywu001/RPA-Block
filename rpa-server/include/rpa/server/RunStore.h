#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "rpa/core/Executor.h"

namespace rpa::server {

struct RunRecord {
    std::string runId;
    std::string scriptId;
    core::RunStatus status = core::RunStatus::Queued;
    std::string currentStepId;
    std::string failedStepId;
    std::string error;
    std::string source;  // "api" | "ui"
    std::map<std::string, std::string> variables;

    std::chrono::system_clock::time_point queuedAt = std::chrono::system_clock::now();
    std::optional<std::chrono::system_clock::time_point> startedAt;
    std::optional<std::chrono::system_clock::time_point> finishedAt;

    int stepsExecuted = 0;
};

/// Thread-safe, bounded history of runs. Backed by an in-memory ring plus an
/// append-only JSON Lines file so history survives a restart without pulling in
/// a database dependency.
class RunStore {
public:
    explicit RunStore(size_t capacity = 500);

    /// Enable durable append. Existing entries are loaded on the way in.
    void setPersistencePath(const std::string& path);

    std::string createRun(const std::string& scriptId,
                          const std::string& source,
                          const std::map<std::string, std::string>& variables);

    void updateStatus(const std::string& runId, core::RunStatus status);
    void setCurrentStep(const std::string& runId, const std::string& stepId);
    void complete(const std::string& runId, const core::RunResult& result);

    /// Called after a run reaches a terminal state, outside this store's lock.
    ///
    /// Every host already reports completion through complete(), so this is the
    /// one signal common to the desktop app, the headless CLI and the tests --
    /// which is what lets the queue notice the desktop is free without each host
    /// having to remember to tell it.
    using CompletionListener = std::function<void(const std::string& runId)>;
    void setCompletionListener(CompletionListener listener);

    std::optional<RunRecord> find(const std::string& runId) const;
    std::vector<RunRecord> list(size_t limit, size_t offset) const;
    size_t size() const;

private:
    void appendToDisk(const RunRecord& record) const;
    void loadFromDisk();

    mutable std::mutex mutex_;
    std::deque<RunRecord> records_;
    size_t capacity_;
    std::string persistencePath_;
    uint64_t sequence_ = 0;

    /// Guarded separately from the records: the listener is invoked with
    /// `mutex_` released, so it must not be read under it either.
    mutable std::mutex listenerMutex_;
    CompletionListener completionListener_;
};

std::string toIso8601(std::chrono::system_clock::time_point when);

}  // namespace rpa::server
