#include "TestHarness.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "rpa/server/ApiServer.h"
#include "rpa/server/RunStore.h"
#include "rpa/server/ScriptRepository.h"

using namespace rpa;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const char* kFlow = R"JSON({
  "name": "invoice-download",
  "version": 1,
  "description": "demo flow",
  "steps": [ { "id": "a", "type": "wait", "ms": 1 } ]
})JSON";

/// Boots a real ApiServer on an ephemeral port with a temp publish directory,
/// so the tests exercise the same code path an external caller hits.
struct ServerFixture {
    fs::path directory;
    server::ScriptRepository repository;
    server::RunStore runStore;
    server::ApiServer api{&repository, &runStore};
    std::string apiKey = server::ApiServer::generateApiKey();
    int port = 0;

    std::atomic<int> handledRuns{0};
    std::atomic<bool> acceptRuns{true};
    /// Accept runs but leave them open, so a test can control exactly when the
    /// desktop frees up and observe what the queue does meanwhile.
    std::atomic<bool> holdRuns{false};

    mutable std::mutex startedMutex;
    std::vector<std::string> startedRunIds;

    // Queue settings, applied by boot().
    bool rejectWhenBusy = false;
    size_t maxQueueDepth = 32;
    int queueTimeoutMs = 600000;

    /// Two-phase construction on purpose: an assertion thrown from a
    /// constructor skips the destructor, which would leave the listener thread
    /// running and hang the whole suite. `boot()` is called after the object
    /// exists, so ~ServerFixture always gets to stop the server.
    ServerFixture() = default;

    void boot() {
        directory = fs::temp_directory_path() /
                    ("pra-test-" + std::to_string(
                                       std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        repository.setDirectory(directory.string());

        api.setRunHandler([this](const server::RunRequest& request, std::string& reason) {
            if (!acceptRuns.load()) {
                reason = "a run is already in progress";
                return false;
            }
            ++handledRuns;
            {
                std::lock_guard<std::mutex> lock(startedMutex);
                startedRunIds.push_back(request.runId);
            }
            if (holdRuns.load()) return true;  // the test finishes it

            core::RunResult result;
            result.status = core::RunStatus::Succeeded;
            result.stepsExecuted = static_cast<int>(request.script.steps.size());
            runStore.complete(request.runId, result);
            return true;
        });

        server::ApiServerConfig config;
        config.bindAddress = "127.0.0.1";
        config.port = 0;  // let the OS pick a free port
        config.apiKeys = {apiKey};
        config.rejectWhenBusy = rejectWhenBusy;
        config.maxQueueDepth = maxQueueDepth;
        config.queueTimeoutMs = queueTimeoutMs;

        std::string error;
        CHECK(api.start(config, error));
        port = api.boundPort();
        CHECK(port > 0);

        // The socket is already bound, but listen_after_bind runs on the server
        // thread; a short wait avoids a race on the very first request.
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    ~ServerFixture() {
        api.stop();
        if (!directory.empty()) {
            std::error_code ec;
            fs::remove_all(directory, ec);
        }
    }

    httplib::Client client() {
        httplib::Client c("127.0.0.1", port);
        c.set_connection_timeout(2);
        c.set_read_timeout(5);
        return c;
    }

    httplib::Headers auth() const { return {{"X-API-Key", apiKey}}; }

    /// Release a run the handler is holding open.
    void finishRun(const std::string& runId) {
        core::RunResult result;
        result.status = core::RunStatus::Succeeded;
        runStore.complete(runId, result);
    }

    std::vector<std::string> started() const {
        std::lock_guard<std::mutex> lock(startedMutex);
        return startedRunIds;
    }

    /// Poll rather than sleep a fixed span: dispatch is asynchronous, and a
    /// fixed sleep is either flaky or slow.
    bool waitForStarted(size_t count, int timeoutMs = 3000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (started().size() >= count) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return started().size() >= count;
    }

    bool waitForStatus(httplib::Client& client, const std::string& runId,
                       const std::string& want, int timeoutMs = 3000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto r = client.Get(("/api/v1/runs/" + runId).c_str(), auth());
            if (r && r->status == 200 &&
                json::parse(r->body).value("status", std::string{}) == want) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        return false;
    }
};

}  // namespace

RPA_TEST(health_endpoint_needs_no_api_key) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Get("/api/v1/health");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 200);

    const json body = json::parse(response->body);
    CHECK_EQ(body.value("status", std::string{}), std::string{"ok"});
}

RPA_TEST(authenticated_endpoints_reject_a_missing_key) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Get("/api/v1/scripts");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 401);
}

RPA_TEST(authenticated_endpoints_reject_a_wrong_key) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Get("/api/v1/scripts", {{"X-API-Key", "sk-pra-not-a-real-key"}});
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 401);
}

RPA_TEST(publishing_then_listing_returns_the_flow) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto published = client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    CHECK(published != nullptr);
    CHECK_EQ(published->status, 201);

    const json created = json::parse(published->body);
    const std::string id = created.value("id", std::string{});
    CHECK_EQ(id, std::string{"invoice-download"});

    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);
    CHECK_EQ(listed->status, 200);

    const json list = json::parse(listed->body);
    CHECK_EQ(list.size(), size_t{1});
    CHECK_EQ(list[0].value("id", std::string{}), std::string{"invoice-download"});
    CHECK_EQ(list[0].value("step_count", 0), 1);
}

RPA_TEST(publishing_malformed_json_is_a_400) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Post("/api/v1/scripts", f.auth(), "{ not json", "application/json");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 400);
}

RPA_TEST(publishing_a_flow_that_fails_validation_is_a_400) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    // Valid JSON, invalid flow: no name and no steps. parseScript() returns a
    // script anyway so the editor can display it, but publishing it over HTTP
    // would put an unrunnable flow where machines trigger it unattended.
    auto response = client.Post("/api/v1/scripts", f.auth(), R"({"version":1})",
                                "application/json");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 400);
    // The caller needs to know what was wrong, not just that it failed.
    CHECK(response->body.find("issues") != std::string::npos);

    // And nothing was stored.
    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);
    CHECK_EQ(listed->status, 200);
    CHECK(listed->body.find("\"id\"") == std::string::npos);
}

RPA_TEST(a_published_flow_advertises_the_inputs_it_accepts) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    // Without this a caller has no way to learn what belongs in `variables`
    // short of reading the flow file off the server's disk.
    const std::string body =
        R"({"name":"invoice","version":1,)"
        R"("variables":{"order_id":"unset","month":"2026-08"},)"
        R"("steps":[{"id":"w","type":"wait","ms":10}]})";
    auto published = client.Post("/api/v1/scripts", f.auth(), body, "application/json");
    CHECK(published != nullptr);
    CHECK_EQ(published->status, 201);

    // The catalogue is where the metadata contract lives; GET /scripts/{id}
    // returns the flow document itself, which is a different thing.
    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);
    CHECK_EQ(listed->status, 200);

    const json record = json::parse(listed->body)[0];
    CHECK(record.contains("variables"));
    CHECK_EQ(record["variables"].size(), size_t{2});
    CHECK_EQ(record["variables"].value("order_id", std::string{}), std::string{"unset"});
    CHECK_EQ(record["variables"].value("month", std::string{}), std::string{"2026-08"});
}

RPA_TEST(a_flow_with_no_inputs_advertises_an_empty_object) {
    // An empty object, not a missing key: a caller checking `variables` should
    // not have to distinguish "takes no inputs" from "this server never says".
    ServerFixture f;
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);

    const json record = json::parse(listed->body)[0];
    CHECK(record.contains("variables"));
    CHECK(record["variables"].is_object());
    CHECK(record["variables"].empty());
}

RPA_TEST(running_an_unpublished_flow_is_a_404) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Post("/api/v1/scripts/nope/run", f.auth(), "{}", "application/json");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 404);
}

RPA_TEST(running_a_published_flow_returns_a_run_id) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");

    auto response = client.Post("/api/v1/scripts/invoice-download/run", f.auth(),
                                R"({"variables":{"who":"api"}})", "application/json");
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 202);

    const json body = json::parse(response->body);
    const std::string runId = body.value("run_id", std::string{});
    CHECK(!runId.empty());
    CHECK_EQ(f.handledRuns.load(), 1);

    auto queried = client.Get(("/api/v1/runs/" + runId).c_str(), f.auth());
    CHECK(queried != nullptr);
    CHECK_EQ(queried->status, 200);

    const json record = json::parse(queried->body);
    CHECK_EQ(record.value("status", std::string{}), std::string{"succeeded"});
    CHECK_EQ(record.value("script_id", std::string{}), std::string{"invoice-download"});
    CHECK_EQ(record.value("source", std::string{}), std::string{"api"});
}

RPA_TEST(rejectWhenBusy_restores_the_immediate_409) {
    ServerFixture f;
    f.rejectWhenBusy = true;  // opt back in to the pre-queue behaviour
    f.holdRuns.store(true);
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");

    auto first = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                             "application/json");
    CHECK(first != nullptr);
    CHECK_EQ(first->status, 202);
    CHECK(f.waitForStarted(1));

    auto second = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                              "application/json");
    CHECK(second != nullptr);
    CHECK_EQ(second->status, 409);

    f.finishRun(json::parse(first->body).value("run_id", std::string{}));
}

RPA_TEST(runs_queue_and_execute_one_at_a_time_in_order) {
    ServerFixture f;
    f.holdRuns.store(true);
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");

    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i) {
        auto r = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                             "application/json");
        CHECK(r != nullptr);
        CHECK_EQ(r->status, 202);
        const json body = json::parse(r->body);
        ids.push_back(body.value("run_id", std::string{}));
        // The position each caller is told is the number of runs ahead of it.
        CHECK_EQ(body.value("queue_position", size_t{999}), static_cast<size_t>(i));
    }

    // Only the first is allowed to start.
    CHECK(f.waitForStarted(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_EQ(f.started().size(), size_t{1});
    CHECK_EQ(f.started()[0], ids[0]);

    // Each completion releases exactly the next one, in submission order.
    f.finishRun(ids[0]);
    CHECK(f.waitForStarted(2));
    CHECK_EQ(f.started()[1], ids[1]);

    f.finishRun(ids[1]);
    CHECK(f.waitForStarted(3));
    CHECK_EQ(f.started()[2], ids[2]);

    f.finishRun(ids[2]);
    CHECK(f.waitForStatus(client, ids[2], "succeeded"));
}

RPA_TEST(queue_endpoint_reports_the_running_run_and_the_waiting_ones) {
    ServerFixture f;
    f.holdRuns.store(true);
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i) {
        auto r = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                             "application/json");
        ids.push_back(json::parse(r->body).value("run_id", std::string{}));
    }
    CHECK(f.waitForStarted(1));

    auto response = client.Get("/api/v1/queue", f.auth());
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 200);
    const json body = json::parse(response->body);
    CHECK_EQ(body.value("depth", size_t{0}), size_t{3});
    CHECK_EQ(body["data"][0].value("state", std::string{}), std::string{"running"});
    CHECK_EQ(body["data"][0].value("run_id", std::string{}), ids[0]);
    CHECK_EQ(body["data"][1].value("state", std::string{}), std::string{"waiting"});
    CHECK_EQ(body["data"][1].value("position", size_t{0}), size_t{1});

    for (const auto& id : ids) f.finishRun(id);
}

RPA_TEST(a_waiting_run_can_be_cancelled_but_a_running_one_cannot) {
    ServerFixture f;
    f.holdRuns.store(true);
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    auto first = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                             "application/json");
    auto second = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                              "application/json");
    const std::string runningId = json::parse(first->body).value("run_id", std::string{});
    const std::string waitingId = json::parse(second->body).value("run_id", std::string{});
    CHECK(f.waitForStarted(1));

    // The one holding the mouse is not the queue's to stop.
    auto refused = client.Delete(("/api/v1/runs/" + runningId).c_str(), f.auth());
    CHECK(refused != nullptr);
    CHECK_EQ(refused->status, 409);

    auto cancelled = client.Delete(("/api/v1/runs/" + waitingId).c_str(), f.auth());
    CHECK(cancelled != nullptr);
    CHECK_EQ(cancelled->status, 200);
    CHECK(f.waitForStatus(client, waitingId, "cancelled"));

    // It never ran, so it must not claim a start time.
    auto detail = client.Get(("/api/v1/runs/" + waitingId).c_str(), f.auth());
    CHECK(detail != nullptr);
    CHECK(json::parse(detail->body)["started_at"].is_null());

    // And it never runs once released.
    f.finishRun(runningId);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(f.started().size(), size_t{1});
}

RPA_TEST(a_full_queue_answers_429) {
    ServerFixture f;
    f.holdRuns.store(true);
    f.maxQueueDepth = 2;
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");

    // One is pulled out of the queue to run, so the depth limit applies to those
    // still waiting: two more fit before the third is refused.
    std::vector<std::string> accepted;
    for (int i = 0; i < 3; ++i) {
        auto r = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                             "application/json");
        CHECK(r != nullptr);
        CHECK_EQ(r->status, 202);
        accepted.push_back(json::parse(r->body).value("run_id", std::string{}));
        if (i == 0) CHECK(f.waitForStarted(1));
    }

    auto overflow = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                                "application/json");
    CHECK(overflow != nullptr);
    CHECK_EQ(overflow->status, 429);

    for (const auto& id : accepted) f.finishRun(id);
}

RPA_TEST(a_run_that_waits_past_its_deadline_is_cancelled) {
    ServerFixture f;
    f.acceptRuns.store(false);  // the desktop is busy with something we do not own
    f.queueTimeoutMs = 150;
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    auto r = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                         "application/json");
    CHECK(r != nullptr);
    CHECK_EQ(r->status, 202);  // queued, not rejected
    const std::string runId = json::parse(r->body).value("run_id", std::string{});

    // Rather than leaving the caller polling forever.
    CHECK(f.waitForStatus(client, runId, "cancelled"));
    CHECK_EQ(f.handledRuns.load(), 0);
}

RPA_TEST(a_refused_run_is_retried_once_the_desktop_frees_up) {
    ServerFixture f;
    f.acceptRuns.store(false);  // busy with a UI-initiated run
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
    auto r = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                         "application/json");
    const std::string runId = json::parse(r->body).value("run_id", std::string{});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(f.handledRuns.load(), 0);  // still waiting its turn

    f.acceptRuns.store(true);
    CHECK(f.waitForStarted(1));
    CHECK(f.waitForStatus(client, runId, "succeeded"));
}

RPA_TEST(stopping_the_server_cancels_whatever_is_still_waiting) {
    // Otherwise those records sit at "queued" forever and every caller polling
    // them waits on a server that is gone.
    std::string waitingId;
    {
        ServerFixture f;
        f.holdRuns.store(true);
        f.boot();
        auto client = f.client();

        client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");
        auto first = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                                 "application/json");
        auto second = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                                  "application/json");
        waitingId = json::parse(second->body).value("run_id", std::string{});
        CHECK(f.waitForStarted(1));

        f.api.stop();

        const auto record = f.runStore.find(waitingId);
        CHECK(record.has_value());
        CHECK(record->status == core::RunStatus::Cancelled);
    }
}

RPA_TEST(unknown_run_id_is_a_404) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto response = client.Get("/api/v1/runs/run_missing", f.auth());
    CHECK(response != nullptr);
    CHECK_EQ(response->status, 404);
}

RPA_TEST(unpublishing_removes_the_flow_from_the_api) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    client.Post("/api/v1/scripts", f.auth(), kFlow, "application/json");

    auto removed = client.Delete("/api/v1/scripts/invoice-download", f.auth());
    CHECK(removed != nullptr);
    CHECK_EQ(removed->status, 204);

    auto run = client.Post("/api/v1/scripts/invoice-download/run", f.auth(), "{}",
                           "application/json");
    CHECK(run != nullptr);
    CHECK_EQ(run->status, 404);
}

RPA_TEST(repository_reload_picks_up_files_written_out_of_band) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    // Simulate the desktop app saving a flow straight into the publish folder.
    const fs::path path = f.directory / "daily-report.rpa.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << kFlow;
    }

    CHECK_EQ(f.repository.reload(), size_t{1});

    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);
    const json list = json::parse(listed->body);
    CHECK_EQ(list.size(), size_t{1});
    CHECK_EQ(list[0].value("id", std::string{}), std::string{"daily-report"});
}

RPA_TEST(script_id_slugification_is_url_and_file_safe) {
    // ASCII names keep their readable id, unchanged.
    CHECK_EQ(server::ScriptRepository::makeId("Invoice Download"),
             std::string{"invoice-download"});
    CHECK_EQ(server::ScriptRepository::makeId("a__b..c"), std::string{"a-b-c"});

    // A name with nothing sluggable still falls back to "flow", but keeps the
    // digest so two such flows do not overwrite each other either.
    CHECK(server::ScriptRepository::makeId("  ").rfind("flow", 0) == 0);
    CHECK(server::ScriptRepository::makeId("  ") != server::ScriptRepository::makeId("   "));

    // Every character of the id is safe in a URL path and a filename.
    for (const char* name : {"報表 / Daily Report!", "客戶對帳單", "ERP 對帳"}) {
        const std::string id = server::ScriptRepository::makeId(name);
        CHECK(!id.empty());
        for (unsigned char c : id) {
            CHECK((std::isalnum(c) != 0) || c == '-' || c == '_');
        }
    }
}

RPA_TEST(distinct_names_never_collapse_onto_one_id) {
    // Chinese names slugify to nothing, so before this every one of them became
    // "flow" -- and publishing the second silently overwrote the first.
    const std::string a = server::ScriptRepository::makeId("客戶對帳單");
    const std::string b = server::ScriptRepository::makeId("每日報表匯出");
    const std::string c = server::ScriptRepository::makeId("庫存盤點");

    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);

    // Stable across calls, so a published URL does not move under the caller.
    CHECK_EQ(server::ScriptRepository::makeId("客戶對帳單"), a);

    // A name that is partly ASCII keeps the readable part as a prefix.
    const std::string mixed = server::ScriptRepository::makeId("ERP 對帳");
    CHECK(mixed.rfind("erp-", 0) == 0);
    CHECK(mixed != server::ScriptRepository::makeId("ERP 匯出"));
}

RPA_TEST(publishing_two_chinese_named_flows_keeps_both) {
    ServerFixture f;
    f.boot();
    auto client = f.client();

    auto publish = [&](const char* name) {
        const std::string body = std::string(R"({"name":")") + name +
                                 R"(","version":1,"steps":[{"id":"w","type":"wait","ms":10}]})";
        auto r = client.Post("/api/v1/scripts", f.auth(), body, "application/json");
        CHECK(r != nullptr);
        CHECK_EQ(r->status, 201);
        return json::parse(r->body).value("id", std::string{});
    };

    const std::string first = publish("客戶對帳單");
    const std::string second = publish("每日報表匯出");
    CHECK(first != second);

    auto listed = client.Get("/api/v1/scripts", f.auth());
    CHECK(listed != nullptr);
    CHECK_EQ(json::parse(listed->body).size(), size_t{2});

    // And each is separately runnable.
    for (const auto& id : {first, second}) {
        auto r = client.Post(("/api/v1/scripts/" + id + "/run").c_str(), f.auth(), "{}",
                             "application/json");
        CHECK(r != nullptr);
        CHECK_EQ(r->status, 202);
    }
}

RPA_TEST(generated_api_keys_are_prefixed_and_unique) {
    const std::string first = server::ApiServer::generateApiKey();
    const std::string second = server::ApiServer::generateApiKey();
    CHECK(first.rfind("sk-pra-", 0) == 0);
    CHECK_EQ(first.size(), size_t{39});
    CHECK(first != second);
}

RPA_TEST(failure_screenshot_path_survives_a_reload) {
    // The path is what an operator follows to see why an unattended run stopped,
    // so it has to outlive the process that recorded it.
    const fs::path file = fs::temp_directory_path() /
                          ("rpa-runs-" + std::to_string(::rand()) + ".jsonl");
    fs::remove(file);

    std::string runId;
    {
        server::RunStore store;
        store.setPersistencePath(file.string());
        runId = store.createRun("erp-login", "api", {});

        core::RunResult result;
        result.status = core::RunStatus::Failed;
        result.failedStepId = "find-login";
        result.error = "no OCR match";
        store.complete(runId, result, "C:/runs/run_1-failed.png");

        const auto record = store.find(runId);
        CHECK(record.has_value());
        CHECK_EQ(record->failureScreenshotPath, std::string("C:/runs/run_1-failed.png"));
    }

    server::RunStore reloaded;
    reloaded.setPersistencePath(file.string());
    const auto record = reloaded.find(runId);
    CHECK(record.has_value());
    CHECK_EQ(record->failureScreenshotPath, std::string("C:/runs/run_1-failed.png"));

    fs::remove(file);
}

RPA_TEST(a_successful_run_records_no_screenshot) {
    server::RunStore store;
    const std::string runId = store.createRun("erp-login", "ui", {});

    core::RunResult result;
    result.status = core::RunStatus::Succeeded;
    store.complete(runId, result);

    const auto record = store.find(runId);
    CHECK(record.has_value());
    CHECK(record->failureScreenshotPath.empty());
}

RPA_TEST(repeated_bad_keys_get_throttled_but_a_valid_one_still_works) {
    // Matters once the server is reachable from the internet: nothing else
    // limits how fast a key can be guessed at.
    ServerFixture f;
    f.boot();
    auto client = f.client();

    int unauthorised = 0;
    int throttled = 0;
    for (int attempt = 0; attempt < 30; ++attempt) {
        auto response = client.Get("/api/v1/scripts", {{"X-API-Key", "sk-pra-wrong"}});
        CHECK(response != nullptr);
        if (response->status == 401) ++unauthorised;
        if (response->status == 429) ++throttled;
    }

    CHECK(unauthorised > 0);
    CHECK(throttled > 0);

    // The throttle counts failures only, so whoever holds a real key is never
    // shut out by someone else's guessing -- otherwise this would be a way to
    // take the owner's own automation offline.
    auto allowed = client.Get("/api/v1/scripts", f.auth());
    CHECK(allowed != nullptr);
    CHECK_EQ(allowed->status, 200);
}
