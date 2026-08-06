#include "rpa/server/ApiServer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "rpa/core/ScriptIO.h"

namespace rpa::server {

using json = nlohmann::json;

namespace {

/// Declared with the charset even though RFC 8259 fixes JSON as UTF-8: some
/// widely-used clients (.NET among them) fall back to Latin-1 without it, which
/// turns any Chinese flow name in a response into mojibake at the caller.
constexpr const char* kJsonContentType = "application/json; charset=utf-8";

json errorBody(const std::string& type, const std::string& message) {
    return json{{"error", json{{"type", type}, {"message", message}}}};
}

json dumpRunRecord(const RunRecord& record) {
    json j;
    j["run_id"] = record.runId;
    j["script_id"] = record.scriptId;
    j["status"] = core::toString(record.status);
    j["source"] = record.source;
    j["steps_executed"] = record.stepsExecuted;
    j["queued_at"] = toIso8601(record.queuedAt);
    j["started_at"] = record.startedAt ? json(toIso8601(*record.startedAt)) : json(nullptr);
    j["finished_at"] = record.finishedAt ? json(toIso8601(*record.finishedAt)) : json(nullptr);
    j["current_step"] =
        record.currentStepId.empty() ? json(nullptr) : json(record.currentStepId);
    if (!record.failedStepId.empty()) j["failed_step"] = record.failedStepId;
    if (!record.error.empty()) j["error"] = record.error;
    return j;
}

json dumpPublishedScript(const PublishedScript& published) {
    json j;
    j["id"] = published.id;
    j["name"] = published.script.name;
    j["description"] = published.script.description;
    j["published_at"] = published.publishedAt;
    j["step_count"] = published.script.steps.size();

    // The inputs this flow accepts, with the defaults it will use when the caller
    // sends nothing. Without these a caller has no way to know what to put in the
    // `variables` object short of reading the flow file.
    j["variables"] = json::object();
    for (const auto& [name, value] : published.script.variables) {
        j["variables"][name] = value;
    }
    return j;
}

}  // namespace

struct ApiServer::Impl {
    ScriptRepository* repository;
    RunStore* runStore;
    ApiServer::RunHandler runHandler;

    httplib::Server server;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<int> boundPort{0};

    mutable std::mutex configMutex;
    ApiServerConfig config;

    // --- The run queue ------------------------------------------------------
    // The desktop has one mouse and one keyboard, so runs are strictly
    // serialised. A dedicated dispatcher thread owns that serialisation: it is
    // the only place runHandler is called, which keeps the HTTP threads and the
    // completion callback out of the business of deciding what runs next.
    mutable std::mutex queueMutex;
    std::condition_variable queueSignal;
    std::deque<RunRequest> queue;
    /// When each queued run gives up waiting.
    std::map<std::string, std::chrono::steady_clock::time_point> deadlines;
    bool inFlight = false;
    std::string inFlightRunId;
    std::thread dispatcher;
    std::atomic<bool> stopping{false};

    Impl(ScriptRepository* repo, RunStore* store) : repository(repo), runStore(store) {}

    void dispatchLoop();
    /// Mark every waiting run cancelled. Called on shutdown so a caller polling a
    /// queued run gets a terminal answer instead of waiting on a server that is
    /// no longer there.
    void drainQueue(const std::string& reason);
    std::vector<QueuedRun> snapshot() const;
    bool cancel(const std::string& runId);

    bool authorize(const httplib::Request& request, httplib::Response& response) const {
        std::set<std::string> keys;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            keys = config.apiKeys;
        }

        if (keys.empty()) {
            response.status = 401;
            response.set_content(
                errorBody("authentication_error",
                          "No API keys are configured on the server. Generate one in the "
                          "API panel before calling this endpoint.")
                    .dump(),
                kJsonContentType);
            return false;
        }

        const std::string provided = request.get_header_value("X-API-Key");
        if (provided.empty() || keys.find(provided) == keys.end()) {
            response.status = 401;
            response.set_content(
                errorBody("authentication_error", "Missing or invalid X-API-Key header.").dump(),
                kJsonContentType);
            return false;
        }
        return true;
    }

    void installRoutes();
};

void ApiServer::Impl::dispatchLoop() {
    std::unique_lock<std::mutex> lock(queueMutex);

    while (!stopping.load()) {
        // The timeout matters: a run can be refused because the desktop is busy
        // with a UI-initiated run, and that finishing is not something the queue
        // is notified about for a run it does not own.
        queueSignal.wait_for(lock, std::chrono::milliseconds(200), [this] {
            return stopping.load() || (!queue.empty() && !inFlight);
        });
        if (stopping.load()) break;
        if (inFlight || queue.empty()) continue;

        RunRequest request = queue.front();
        queue.pop_front();
        inFlight = true;
        inFlightRunId = request.runId;

        RunHandler handler = runHandler;
        lock.unlock();

        // Marked running before the handler is called: without this the record
        // sits at "queued" for the whole execution, which is indistinguishable
        // from actually waiting in line now that waiting is a real state.
        runStore->updateStatus(request.runId, core::RunStatus::Running);

        std::string reason;
        const bool accepted = handler ? handler(request, reason) : false;
        if (!handler) reason = "no execution backend is attached to this server";

        lock.lock();

        if (accepted) {
            // RunStore's completion listener clears inFlight. It may already
            // have fired, if the handler ran the flow synchronously.
            deadlines.erase(request.runId);
            continue;
        }

        inFlight = false;
        inFlightRunId.clear();

        const auto deadline = deadlines.find(request.runId);
        const bool expired = deadline == deadlines.end() ||
                             std::chrono::steady_clock::now() >= deadline->second;
        if (expired) {
            deadlines.erase(request.runId);
            lock.unlock();
            core::RunResult giveUp;
            giveUp.status = core::RunStatus::Cancelled;
            giveUp.error = reason.empty() ? "timed out waiting for the desktop" : reason;
            runStore->complete(request.runId, giveUp);
            lock.lock();
            continue;
        }

        // Back to the head of the line, ahead of anything queued behind it, and
        // pause briefly so a persistently busy desktop is not spun on.
        runStore->updateStatus(request.runId, core::RunStatus::Queued);
        queue.push_front(request);
        queueSignal.wait_for(lock, std::chrono::milliseconds(250));
    }
}

std::vector<QueuedRun> ApiServer::Impl::snapshot() const {
    std::lock_guard<std::mutex> lock(queueMutex);

    std::vector<QueuedRun> entries;
    entries.reserve(queue.size() + 1);
    // Position 0 is whatever holds the desktop right now, so a caller reading
    // this sees the same ordering the dispatcher will follow.
    if (inFlight) {
        QueuedRun current;
        current.runId = inFlightRunId;
        current.position = 0;
        if (const auto record = runStore->find(inFlightRunId)) current.scriptId = record->scriptId;
        entries.push_back(current);
    }
    for (size_t i = 0; i < queue.size(); ++i) {
        entries.push_back(QueuedRun{queue[i].runId, queue[i].scriptId, i + (inFlight ? 1 : 0)});
    }
    return entries;
}

bool ApiServer::Impl::cancel(const std::string& runId) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        auto it = std::find_if(queue.begin(), queue.end(),
                               [&](const RunRequest& r) { return r.runId == runId; });
        if (it == queue.end()) return false;
        queue.erase(it);
        deadlines.erase(runId);
    }

    core::RunResult cancelled;
    cancelled.status = core::RunStatus::Cancelled;
    cancelled.error = "cancelled while waiting in the queue";
    runStore->complete(runId, cancelled);
    queueSignal.notify_all();
    return true;
}

void ApiServer::Impl::drainQueue(const std::string& reason) {
    std::deque<RunRequest> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pending.swap(queue);
        deadlines.clear();
    }
    for (const RunRequest& request : pending) {
        core::RunResult cancelled;
        cancelled.status = core::RunStatus::Cancelled;
        cancelled.error = reason;
        runStore->complete(request.runId, cancelled);
    }
}

void ApiServer::Impl::installRoutes() {
    server.set_exception_handler(
        [](const httplib::Request&, httplib::Response& response, std::exception_ptr ep) {
            std::string detail = "unknown error";
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                detail = e.what();
            }
            response.status = 500;
            response.set_content(errorBody("api_error", detail).dump(), kJsonContentType);
        });

    // Health is deliberately unauthenticated so a load balancer or the UI's own
    // status light can poll it without holding a key.
    server.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(json{{"status", "ok"}, {"version", "0.1.0"}}.dump(),
                             kJsonContentType);
    });

    server.Get("/api/v1/scripts", [this](const httplib::Request& request,
                                         httplib::Response& response) {
        if (!authorize(request, response)) return;

        json list = json::array();
        for (const auto& published : repository->list()) {
            list.push_back(dumpPublishedScript(published));
        }
        response.set_content(list.dump(), kJsonContentType);
    });

    server.Get(R"(/api/v1/scripts/([A-Za-z0-9_\-]+))",
               [this](const httplib::Request& request, httplib::Response& response) {
                   if (!authorize(request, response)) return;

                   const std::string id = request.matches[1];
                   const auto published = repository->find(id);
                   if (!published) {
                       response.status = 404;
                       response.set_content(
                           errorBody("not_found_error", "No published flow with id: " + id).dump(),
                           kJsonContentType);
                       return;
                   }
                   response.set_content(core::serializeScript(published->script, true),
                                        kJsonContentType);
               });

    server.Post("/api/v1/scripts", [this](const httplib::Request& request,
                                          httplib::Response& response) {
        if (!authorize(request, response)) return;

        const core::ParseResult parsed = core::parseScript(request.body);
        if (!parsed.ok) {
            response.status = 400;
            response.set_content(errorBody("invalid_request_error", parsed.error).dump(),
                                 kJsonContentType);
            return;
        }

        json issues = json::array();
        for (const auto& issue : parsed.issues) {
            issues.push_back(json{{"step_id", issue.stepId}, {"message", issue.message}});
        }

        // parseScript() reports semantic problems while still returning a script,
        // which is right for the editor -- a half-finished flow should still be
        // visible on the canvas. It is wrong here. A published flow is triggered
        // by machines with nobody watching, so accepting one that failed
        // validation just defers the failure to a run that no human will see.
        if (!parsed.issues.empty()) {
            response.status = 400;
            json body = errorBody("invalid_request_error",
                                  "Flow failed validation; nothing was published.");
            body["error"]["issues"] = issues;
            response.set_content(body.dump(), kJsonContentType);
            return;
        }

        std::string error;
        const auto id = repository->publish(parsed.script, request.get_param_value("id"), error);
        if (!id) {
            response.status = 400;
            response.set_content(errorBody("invalid_request_error", error).dump(),
                                 kJsonContentType);
            return;
        }

        response.status = 201;
        response.set_content(json{{"id", *id}, {"issues", issues}}.dump(), kJsonContentType);
    });

    server.Delete(R"(/api/v1/scripts/([A-Za-z0-9_\-]+))",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      if (!authorize(request, response)) return;

                      const std::string id = request.matches[1];
                      std::string error;
                      if (!repository->unpublish(id, error)) {
                          response.status = 404;
                          response.set_content(errorBody("not_found_error", error).dump(),
                                               kJsonContentType);
                          return;
                      }
                      response.status = 204;
                  });

    server.Post(R"(/api/v1/scripts/([A-Za-z0-9_\-]+)/run)",
                [this](const httplib::Request& request, httplib::Response& response) {
                    if (!authorize(request, response)) return;

                    const std::string scriptId = request.matches[1];
                    const auto published = repository->find(scriptId);
                    if (!published) {
                        response.status = 404;
                        response.set_content(
                            errorBody("not_found_error",
                                      "No published flow with id: " + scriptId)
                                .dump(),
                            kJsonContentType);
                        return;
                    }

                    std::map<std::string, std::string> variables;
                    if (!request.body.empty()) {
                        json body = json::parse(request.body, nullptr, false);
                        if (body.is_discarded()) {
                            response.status = 400;
                            response.set_content(
                                errorBody("invalid_request_error",
                                          "Request body is not valid JSON.")
                                    .dump(),
                                kJsonContentType);
                            return;
                        }
                        auto vars = body.find("variables");
                        if (vars != body.end() && vars->is_object()) {
                            for (auto it = vars->begin(); it != vars->end(); ++it) {
                                variables[it.key()] = it.value().is_string()
                                                          ? it.value().get<std::string>()
                                                          : it.value().dump();
                            }
                        }
                    }

                    if (!runHandler) {
                        response.status = 503;
                        response.set_content(
                            errorBody("api_error",
                                      "No execution backend is attached to this server.")
                                .dump(),
                            kJsonContentType);
                        return;
                    }

                    ApiServerConfig settings;
                    {
                        std::lock_guard<std::mutex> lock(configMutex);
                        settings = config;
                    }

                    size_t position = 0;
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);

                        if (settings.rejectWhenBusy && (inFlight || !queue.empty())) {
                            response.status = 409;
                            response.set_content(
                                errorBody("invalid_request_error",
                                          "The desktop is busy with another run.")
                                    .dump(),
                                kJsonContentType);
                            return;
                        }

                        if (queue.size() >= settings.maxQueueDepth) {
                            response.status = 429;
                            response.set_content(
                                errorBody("rate_limit_error",
                                          "The run queue is full (" +
                                              std::to_string(settings.maxQueueDepth) +
                                              " waiting). Retry once it drains.")
                                    .dump(),
                                kJsonContentType);
                            return;
                        }

                        RunRequest runRequest;
                        runRequest.runId = runStore->createRun(scriptId, "api", variables);
                        runRequest.scriptId = scriptId;
                        runRequest.script = published->script;
                        runRequest.variables = variables;

                        deadlines[runRequest.runId] =
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(settings.queueTimeoutMs);
                        queue.push_back(runRequest);
                        position = queue.size() - 1 + (inFlight ? 1 : 0);

                        response.status = 202;
                        response.set_content(json{{"run_id", runRequest.runId},
                                                  {"script_id", scriptId},
                                                  {"queue_position", position}}
                                                 .dump(),
                                             kJsonContentType);
                    }
                    queueSignal.notify_all();
                });

    server.Get(R"(/api/v1/runs/([A-Za-z0-9_\-]+))",
               [this](const httplib::Request& request, httplib::Response& response) {
                   if (!authorize(request, response)) return;

                   const std::string runId = request.matches[1];
                   const auto record = runStore->find(runId);
                   if (!record) {
                       response.status = 404;
                       response.set_content(
                           errorBody("not_found_error", "No run with id: " + runId).dump(),
                           kJsonContentType);
                       return;
                   }
                   json body = dumpRunRecord(*record);
                   // Only meaningful while it is still waiting, and only the
                   // queue knows it -- the store records status, not order.
                   {
                       std::lock_guard<std::mutex> lock(queueMutex);
                       for (size_t i = 0; i < queue.size(); ++i) {
                           if (queue[i].runId != runId) continue;
                           body["queue_position"] = i + (inFlight ? 1 : 0);
                           break;
                       }
                   }
                   response.set_content(body.dump(), kJsonContentType);
               });

    server.Get("/api/v1/queue", [this](const httplib::Request& request,
                                       httplib::Response& response) {
        if (!authorize(request, response)) return;

        json list = json::array();
        for (const QueuedRun& entry : snapshot()) {
            list.push_back(json{{"run_id", entry.runId},
                                {"script_id", entry.scriptId},
                                {"position", entry.position},
                                {"state", entry.position == 0 ? "running" : "waiting"}});
        }
        response.set_content(json{{"data", list}, {"depth", list.size()}}.dump(),
                             kJsonContentType);
    });

    server.Delete(R"(/api/v1/runs/([A-Za-z0-9_\-]+))",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      if (!authorize(request, response)) return;

                      const std::string runId = request.matches[1];
                      if (!runStore->find(runId)) {
                          response.status = 404;
                          response.set_content(
                              errorBody("not_found_error", "No run with id: " + runId).dump(),
                              kJsonContentType);
                          return;
                      }
                      if (!cancel(runId)) {
                          // Either it already finished or it is the one holding
                          // the mouse. Stopping that is the executor's job.
                          response.status = 409;
                          response.set_content(
                              errorBody("invalid_request_error",
                                        "Run " + runId +
                                            " is not waiting in the queue; it has already "
                                            "started or finished.")
                                  .dump(),
                              kJsonContentType);
                          return;
                      }
                      response.set_content(json{{"run_id", runId}, {"status", "cancelled"}}.dump(),
                                           kJsonContentType);
                  });

    server.Get("/api/v1/runs", [this](const httplib::Request& request,
                                      httplib::Response& response) {
        if (!authorize(request, response)) return;

        size_t limit = 50;
        size_t offset = 0;
        if (request.has_param("limit")) {
            limit = std::clamp<size_t>(std::stoul(request.get_param_value("limit")), 1, 500);
        }
        if (request.has_param("offset")) {
            offset = std::stoul(request.get_param_value("offset"));
        }

        json list = json::array();
        for (const auto& record : runStore->list(limit, offset)) {
            list.push_back(dumpRunRecord(record));
        }
        response.set_content(json{{"data", list}, {"total", runStore->size()}}.dump(),
                             kJsonContentType);
    });
}

ApiServer::ApiServer(ScriptRepository* repository, RunStore* runStore)
    : impl_(std::make_unique<Impl>(repository, runStore)) {
    impl_->installRoutes();
}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::setRunHandler(RunHandler handler) {
    impl_->runHandler = std::move(handler);
}

bool ApiServer::isRunning() const {
    return impl_->running.load();
}

ApiServerConfig ApiServer::config() const {
    std::lock_guard<std::mutex> lock(impl_->configMutex);
    return impl_->config;
}

int ApiServer::boundPort() const {
    return impl_->boundPort.load();
}

std::string ApiServer::generateApiKey() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    static const char* kAlphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<int> pick(0, 61);

    std::string key = "sk-pra-";
    for (int i = 0; i < 32; ++i) key += kAlphabet[pick(engine)];
    return key;
}

bool ApiServer::start(const ApiServerConfig& config, std::string& error) {
    if (impl_->running.load()) {
        error = "server is already running";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->config = config;
    }

    // Port 0 means "any free port"; bind_to_port only reports success/failure,
    // so the ephemeral case has to go through bind_to_any_port to learn which
    // port the OS actually handed out.
    int port = -1;
    if (config.port == 0) {
        port = impl_->server.bind_to_any_port(config.bindAddress);
    } else if (impl_->server.bind_to_port(config.bindAddress, config.port)) {
        port = config.port;
    }

    if (port <= 0) {
        error = "cannot bind " + config.bindAddress + ":" + std::to_string(config.port) +
                " (port in use, or the address is not available)";
        return false;
    }

    impl_->boundPort.store(port);
    impl_->running.store(true);

    // Every host reports completion through the store, so subscribing here is
    // what lets the queue see the desktop free up without each host having to
    // call back into the server.
    impl_->runStore->setCompletionListener([this](const std::string& runId) {
        {
            std::lock_guard<std::mutex> lock(impl_->queueMutex);
            if (impl_->inFlight && impl_->inFlightRunId == runId) {
                impl_->inFlight = false;
                impl_->inFlightRunId.clear();
            }
        }
        // Notified even for a run the queue does not own: a flow the user started
        // in the UI finishing is exactly when a refused run can be retried.
        impl_->queueSignal.notify_all();
    });

    impl_->stopping.store(false);
    impl_->dispatcher = std::thread([this] { impl_->dispatchLoop(); });

    impl_->thread = std::thread([this] {
        impl_->server.listen_after_bind();
        impl_->running.store(false);
    });

    return true;
}

void ApiServer::stop() {
    const bool wasRunning = impl_->running.exchange(false);

    if (wasRunning) impl_->server.stop();
    if (impl_->thread.joinable()) impl_->thread.join();

    // Stop dispatching before unsubscribing, or the listener could fire against a
    // half-torn-down queue.
    impl_->stopping.store(true);
    impl_->queueSignal.notify_all();
    if (impl_->dispatcher.joinable()) impl_->dispatcher.join();
    impl_->runStore->setCompletionListener(nullptr);

    // Anything still waiting will never run now. Leaving those records at
    // "queued" would strand every caller polling them.
    impl_->drainQueue("server stopped before this run started");

    if (wasRunning) impl_->boundPort.store(0);
}

std::vector<QueuedRun> ApiServer::queueSnapshot() const {
    return impl_->snapshot();
}

bool ApiServer::cancelQueued(const std::string& runId) {
    return impl_->cancel(runId);
}

}  // namespace rpa::server
