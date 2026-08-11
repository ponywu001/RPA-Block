#include "rpa/server/RunStore.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

namespace rpa::server {

using json = nlohmann::json;

namespace {

std::string randomSuffix() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    static const char* kAlphabet = "0123456789abcdef";
    std::uniform_int_distribution<int> pick(0, 15);

    std::string suffix(8, '0');
    for (char& c : suffix) c = kAlphabet[pick(engine)];
    return suffix;
}

json dumpRecord(const RunRecord& record) {
    json j;
    j["run_id"] = record.runId;
    j["script_id"] = record.scriptId;
    j["status"] = core::toString(record.status);
    j["source"] = record.source;
    j["steps_executed"] = record.stepsExecuted;
    j["queued_at"] = toIso8601(record.queuedAt);
    if (record.startedAt) j["started_at"] = toIso8601(*record.startedAt);
    if (record.finishedAt) j["finished_at"] = toIso8601(*record.finishedAt);
    if (!record.currentStepId.empty()) j["current_step"] = record.currentStepId;
    if (!record.failedStepId.empty()) j["failed_step"] = record.failedStepId;
    if (!record.error.empty()) j["error"] = record.error;
    if (!record.variables.empty()) j["variables"] = record.variables;
    if (!record.failureScreenshotPath.empty()) {
        j["failure_screenshot"] = record.failureScreenshotPath;
    }
    return j;
}

}  // namespace

std::string toIso8601(std::chrono::system_clock::time_point when) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

RunStore::RunStore(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

void RunStore::setPersistencePath(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        persistencePath_ = path;
    }
    loadFromDisk();
}

std::string RunStore::createRun(const std::string& scriptId,
                                const std::string& source,
                                const std::map<std::string, std::string>& variables) {
    std::lock_guard<std::mutex> lock(mutex_);

    RunRecord record;
    record.runId = "run_" + randomSuffix();
    record.scriptId = scriptId;
    record.source = source;
    record.variables = variables;
    record.status = core::RunStatus::Queued;

    records_.push_back(record);
    while (records_.size() > capacity_) records_.pop_front();
    ++sequence_;

    return record.runId;
}

void RunStore::updateStatus(const std::string& runId, core::RunStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        if (record.runId != runId) continue;
        record.status = status;
        if (status == core::RunStatus::Running && !record.startedAt) {
            record.startedAt = std::chrono::system_clock::now();
        }
        return;
    }
}

void RunStore::setCurrentStep(const std::string& runId, const std::string& stepId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        if (record.runId == runId) {
            record.currentStepId = stepId;
            return;
        }
    }
}

void RunStore::complete(const std::string& runId,
                        const core::RunResult& result,
                        const std::string& failureScreenshotPath) {
    RunRecord snapshot;
    bool found = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& record : records_) {
            if (record.runId != runId) continue;
            record.status = result.status;
            record.failedStepId = result.failedStepId;
            record.error = result.error;
            record.failureScreenshotPath = failureScreenshotPath;
            record.stepsExecuted = result.stepsExecuted;
            record.currentStepId.clear();
            record.finishedAt = std::chrono::system_clock::now();
            // A run cancelled from the queue never started, so leave started_at
            // null rather than backfilling a time it was never running at. Any
            // other terminal status implies it did start, even if nothing
            // reported the transition.
            if (!record.startedAt && result.status != core::RunStatus::Cancelled) {
                record.startedAt = record.queuedAt;
            }
            snapshot = record;
            found = true;
            break;
        }
    }

    // Written outside the lock so a slow disk can't stall in-flight API calls.
    if (found) appendToDisk(snapshot);

    // Also outside the lock: the listener dispatches the next queued run, which
    // calls straight back into this store. Holding `mutex_` here would deadlock.
    if (found) {
        CompletionListener listener;
        {
            std::lock_guard<std::mutex> lock(listenerMutex_);
            listener = completionListener_;
        }
        if (listener) listener(runId);
    }
}

void RunStore::setCompletionListener(CompletionListener listener) {
    std::lock_guard<std::mutex> lock(listenerMutex_);
    completionListener_ = std::move(listener);
}

std::optional<RunRecord> RunStore::find(const std::string& runId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& record : records_) {
        if (record.runId == runId) return record;
    }
    return std::nullopt;
}

std::vector<RunRecord> RunStore::list(size_t limit, size_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<RunRecord> newestFirst(records_.rbegin(), records_.rend());
    if (offset >= newestFirst.size()) return {};

    const size_t end = std::min(newestFirst.size(), offset + limit);
    return std::vector<RunRecord>(newestFirst.begin() + static_cast<ptrdiff_t>(offset),
                                  newestFirst.begin() + static_cast<ptrdiff_t>(end));
}

size_t RunStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

void RunStore::appendToDisk(const RunRecord& record) const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = persistencePath_;
    }
    if (path.empty()) return;

    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out) return;
    out << dumpRecord(record).dump() << "\n";
}

void RunStore::loadFromDisk() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = persistencePath_;
    }
    if (path.empty()) return;

    std::ifstream in(path, std::ios::binary);
    if (!in) return;

    std::vector<RunRecord> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;

        RunRecord record;
        record.runId = j.value("run_id", "");
        record.scriptId = j.value("script_id", "");
        record.source = j.value("source", "");
        record.currentStepId = j.value("current_step", "");
        record.failedStepId = j.value("failed_step", "");
        record.error = j.value("error", "");
        record.failureScreenshotPath = j.value("failure_screenshot", "");
        record.stepsExecuted = j.value("steps_executed", 0);

        const std::string status = j.value("status", "succeeded");
        if (status == "failed") record.status = core::RunStatus::Failed;
        else if (status == "cancelled") record.status = core::RunStatus::Cancelled;
        else if (status == "running") record.status = core::RunStatus::Running;
        else if (status == "queued") record.status = core::RunStatus::Queued;
        else record.status = core::RunStatus::Succeeded;

        if (record.runId.empty()) continue;
        loaded.push_back(std::move(record));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // Keep only the tail; older entries stay on disk for offline inspection.
    const size_t keep = std::min(loaded.size(), capacity_);
    records_.assign(loaded.end() - static_cast<ptrdiff_t>(keep), loaded.end());
}

}  // namespace rpa::server
