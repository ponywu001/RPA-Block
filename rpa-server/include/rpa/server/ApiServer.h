#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "rpa/core/Executor.h"
#include "rpa/server/RunStore.h"
#include "rpa/server/ScriptRepository.h"

namespace rpa::server {

struct ApiServerConfig {
    /// Loopback by default. Binding 0.0.0.0 exposes the desktop's input to the
    /// network, so the UI requires an explicit confirmation for it.
    std::string bindAddress = "127.0.0.1";
    int port = 8420;
    /// Accepted X-API-Key values. An empty set rejects every authenticated
    /// endpoint, which is safer than defaulting to open.
    std::set<std::string> apiKeys;

    /// Answer 409 immediately instead of queueing. For callers that would rather
    /// know now than have a run start ten minutes later.
    bool rejectWhenBusy = false;

    /// How many runs may wait to start. Past this, /run answers 429 rather than
    /// growing without bound -- every queued run eventually seizes the mouse and
    /// keyboard, so an unbounded backlog is a way to lose the machine for hours.
    size_t maxQueueDepth = 32;

    /// How long a run may sit waiting before it is abandoned as cancelled. A
    /// desktop left busy by a manual run would otherwise leave callers polling
    /// something that never starts.
    int queueTimeoutMs = 600000;
};

/// A run waiting its turn.
struct QueuedRun {
    std::string runId;
    std::string scriptId;
    /// 0 is the run currently executing; 1 is next to start.
    size_t position = 0;
};

/// A request to execute a flow, handed from the HTTP thread to whoever owns the
/// executor. The desktop app services these on its executor thread; a headless
/// build can service them inline.
struct RunRequest {
    std::string runId;
    std::string scriptId;
    core::Script script;
    std::map<std::string, std::string> variables;
};

/// Embedded REST API. HTTP handling runs on its own thread; execution is
/// delegated through `setRunHandler` so the server never drives the desktop
/// directly.
class ApiServer {
public:
    ApiServer(ScriptRepository* repository, RunStore* runStore);
    ~ApiServer();
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    /// Invoked on the server's dispatcher thread, one run at a time, never
    /// concurrently. Return true if the run has been started; the host reports
    /// its outcome through RunStore::complete(), which releases the queue.
    ///
    /// Returning false means "not now" -- typically the desktop is busy with a
    /// run the user started in the UI, which the queue does not control. The run
    /// stays at the head of the queue and is retried until it starts or hits
    /// `queueTimeoutMs`.
    using RunHandler = std::function<bool(const RunRequest&, std::string& reason)>;
    void setRunHandler(RunHandler handler);

    /// The executing run followed by those waiting, in order.
    std::vector<QueuedRun> queueSnapshot() const;

    /// Drop a run that has not started yet. False when it is unknown or already
    /// running -- a running flow has the mouse, and stopping it is the executor's
    /// job, not the queue's.
    bool cancelQueued(const std::string& runId);

    bool start(const ApiServerConfig& config, std::string& error);
    void stop();
    bool isRunning() const;

    ApiServerConfig config() const;
    /// Actual bound port, which differs from the configured one when port 0
    /// was requested.
    int boundPort() const;

    /// Generate a new key, register it, and return it.
    static std::string generateApiKey();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rpa::server
