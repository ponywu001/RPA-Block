#include "TestHarness.h"

#include "FakeBackends.h"
#include "rpa/core/Executor.h"
#include "rpa/core/ScriptIO.h"

using namespace rpa::core;
using namespace rpa::test;

namespace {

struct Fixture {
    FakeInput input;
    FakeWindow window;
    FakeLocator locator;
    Executor executor{&input, &window, &locator};

    Script parse(const char* json) {
        ParseResult result = parseScript(json);
        CHECK(result.ok);
        return result.script;
    }
};

}  // namespace

RPA_TEST(runs_a_linear_flow_in_order) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "linear", "version": 1,
      "variables": { "who": "demo" },
      "steps": [
        { "id": "a", "type": "window_activate", "title_match": "Notepad" },
        { "id": "b", "type": "type_text", "text": "hello {{who}}" },
        { "id": "c", "type": "key_press", "keys": "ctrl+s" },
        { "id": "d", "type": "click", "target": { "kind": "point", "x": 10, "y": 20 } }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Succeeded);
    CHECK_EQ(result.stepsExecuted, 4);
    // The executor issues one click call; positioning the cursor is the input
    // backend's own business, so the fake records only the click.
    CHECK_EQ(join(f.input.calls),
             std::string{"type(hello demo)|keys(ctrl+s)|click(10,20,left,1)"});
    CHECK_EQ(join(f.window.activated), std::string{"Notepad"});
}

RPA_TEST(click_on_ocr_target_uses_located_point_with_offset) {
    Fixture f;
    f.locator.hits["Login"] = Point{500, 300};

    const Script script = f.parse(R"JSON({
      "name": "ocr-click", "version": 1, "steps": [
        { "id": "a", "type": "click",
          "target": { "kind": "ocr", "text": "Login", "offset_x": 80, "offset_y": 5 } }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"click(580,305,left,1)"});
}

RPA_TEST(ocr_find_publishes_hit_into_variable_scope) {
    Fixture f;
    f.locator.hits["Submit"] = Point{120, 240};

    const Script script = f.parse(R"JSON({
      "name": "ocr-vars", "version": 1, "steps": [
        { "id": "a", "type": "ocr_find", "text": "Submit", "save_to_var": "btn" }
      ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(f.executor.variables().get("btn").value(), std::string{"Submit"});
    CHECK_EQ(f.executor.variables().get("btn_found").value(), std::string{"true"});
    CHECK_EQ(f.executor.variables().get("btn_x").value(), std::string{"120"});
    CHECK_EQ(f.executor.variables().get("btn_y").value(), std::string{"240"});
}

RPA_TEST(missing_target_retries_then_aborts_the_run) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "retry", "version": 1, "steps": [
        { "id": "a", "type": "ocr_find", "text": "Nope",
          "retry": { "times": 3, "interval_ms": 0 } },
        { "id": "b", "type": "key_press", "keys": "enter" }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Failed);
    CHECK_EQ(result.failedStepId, std::string{"a"});
    CHECK_EQ(f.locator.locateCallCount, 3);
    // The step after the failure must not run under the default abort policy.
    CHECK_EQ(f.input.calls.size(), size_t{0});
}

RPA_TEST(on_fail_continue_keeps_the_run_going) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "continue", "version": 1, "steps": [
        { "id": "a", "type": "ocr_find", "text": "Nope",
          "retry": { "times": 1, "interval_ms": 0 }, "on_fail": "continue" },
        { "id": "b", "type": "key_press", "keys": "enter" }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(enter)"});
}

RPA_TEST(on_fail_goto_jumps_to_the_named_step) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "goto", "version": 1, "steps": [
        { "id": "a", "type": "ocr_find", "text": "Nope",
          "retry": { "times": 1, "interval_ms": 0 }, "on_fail": "goto:recover" },
        { "id": "skipped", "type": "key_press", "keys": "f1" },
        { "id": "recover", "type": "key_press", "keys": "escape" }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(escape)"});
}

RPA_TEST(loop_with_count_repeats_its_body) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "loop", "version": 1, "steps": [
        { "id": "l", "type": "loop", "count": 3, "steps": [
            { "id": "l1", "type": "key_press", "keys": "tab" }
        ]}
      ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(tab)|keys(tab)|keys(tab)"});
    CHECK_EQ(f.executor.variables().get("loop_index").value(), std::string{"2"});
}

RPA_TEST(loop_guard_stops_a_never_false_while_condition) {
    Fixture f;
    f.locator.hits["Spinner"] = Point{1, 1};  // always found, so the loop never exits

    ExecutorConfig config;
    config.maxLoopIterations = 5;
    f.executor.setConfig(config);

    const Script script = f.parse(R"JSON({
      "name": "runaway", "version": 1, "steps": [
        { "id": "l", "type": "loop",
          "while_condition": { "kind": "ocr_found",
                               "target": { "text": "Spinner", "retry": { "times": 1 } } },
          "steps": [ { "id": "l1", "type": "wait", "ms": 0 } ] }
      ]})JSON");

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error.find("maxLoopIterations") != std::string::npos);
}

RPA_TEST(if_takes_the_then_branch_when_the_condition_holds) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "if-then", "version": 1,
      "variables": { "mode": "fast" },
      "steps": [
        { "id": "i", "type": "if",
          "condition": { "kind": "var_equals", "variable": "mode", "value": "fast" },
          "then_steps": [ { "id": "t", "type": "key_press", "keys": "f5" } ],
          "else_steps": [ { "id": "e", "type": "key_press", "keys": "f9" } ] }
      ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f5)"});
}

RPA_TEST(if_takes_the_else_branch_when_the_condition_fails) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "if-else", "version": 1,
      "variables": { "mode": "slow" },
      "steps": [
        { "id": "i", "type": "if",
          "condition": { "kind": "var_equals", "variable": "mode", "value": "fast" },
          "then_steps": [ { "id": "t", "type": "key_press", "keys": "f5" } ],
          "else_steps": [ { "id": "e", "type": "key_press", "keys": "f9" } ] }
      ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f9)"});
}

RPA_TEST(if_on_image_found_consults_the_locator) {
    Fixture f;
    f.locator.hits["assets/ok.png"] = Point{7, 7};

    const Script script = f.parse(R"JSON({
      "name": "if-image", "version": 1, "steps": [
        { "id": "i", "type": "if",
          "condition": { "kind": "image_found",
                         "target": { "template": "assets/ok.png", "retry": { "times": 1 } } },
          "then_steps": [ { "id": "t", "type": "key_press", "keys": "enter" } ] }
      ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(enter)"});
}

RPA_TEST(overrides_win_over_declared_variables) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "override", "version": 1,
      "variables": { "who": "declared" },
      "steps": [ { "id": "a", "type": "type_text", "text": "{{who}}" } ]})JSON");

    CHECK(f.executor.run(script, {{"who", "from-api"}}).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"type(from-api)"});
}

RPA_TEST(disabled_steps_are_skipped_but_do_not_fail_the_run) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "disabled", "version": 1, "steps": [
        { "id": "a", "type": "key_press", "keys": "f1", "enabled": false },
        { "id": "b", "type": "key_press", "keys": "f2" }
      ]})JSON");

    const RunResult result = f.executor.run(script);
    CHECK(result.status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f2)"});
    CHECK_EQ(result.stepsExecuted, 1);
}

RPA_TEST(input_backend_failure_aborts_with_the_backend_message) {
    Fixture f;
    f.input.failNextClick = true;

    const Script script = f.parse(R"JSON({
      "name": "click-fail", "version": 1, "steps": [
        { "id": "a", "type": "click", "target": { "kind": "point", "x": 1, "y": 2 } }
      ]})JSON");

    const RunResult result = f.executor.run(script);
    CHECK(result.status == RunStatus::Failed);
    CHECK_EQ(result.failedStepId, std::string{"a"});
    CHECK_EQ(result.error, std::string{"simulated click failure"});
}

RPA_TEST(screenshot_step_delegates_to_the_locator) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "shot", "version": 1,
      "variables": { "stamp": "0001" },
      "steps": [ { "id": "a", "type": "screenshot", "path": "out/{{stamp}}.png" } ]})JSON");

    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.locator.captures), std::string{"out/0001.png"});
}

RPA_TEST(stop_request_mid_run_cancels_the_remaining_steps) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "stop", "version": 1, "steps": [
        { "id": "a", "type": "key_press", "keys": "f1" },
        { "id": "b", "type": "key_press", "keys": "f2" },
        { "id": "c", "type": "key_press", "keys": "f3" }
      ]})JSON");

    // Mirrors the F12 emergency abort: the stop latch is set from another
    // thread while a step is in flight.
    ExecutorCallbacks callbacks;
    callbacks.onStepFinished = [&](const std::string& stepId, const StepOutcome&) {
        if (stepId == "a") f.executor.requestStop();
    };
    f.executor.setCallbacks(callbacks);

    const RunResult result = f.executor.run(script);

    CHECK(result.status == RunStatus::Cancelled);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f1)"});
}

RPA_TEST(a_stale_stop_request_does_not_kill_the_next_run) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "fresh", "version": 1,
      "steps": [ { "id": "a", "type": "key_press", "keys": "f1" } ]})JSON");

    f.executor.requestStop();
    // run() clears the latch on entry, so a stop left over from a previous run
    // must not cancel this one.
    CHECK(f.executor.run(script).status == RunStatus::Succeeded);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f1)"});
}

RPA_TEST(status_callback_reports_the_lifecycle) {
    Fixture f;
    std::vector<std::string> statuses;
    ExecutorCallbacks callbacks;
    callbacks.onStatusChanged = [&](RunStatus s) { statuses.push_back(toString(s)); };
    f.executor.setCallbacks(callbacks);

    const Script script = f.parse(R"JSON({
      "name": "cb", "version": 1,
      "steps": [ { "id": "a", "type": "wait", "ms": 0 } ]})JSON");

    f.executor.run(script);
    CHECK_EQ(join(statuses), std::string{"running|succeeded"});
}

RPA_TEST(single_step_execution_advances_one_step_only) {
    Fixture f;
    const Script script = f.parse(R"JSON({
      "name": "single", "version": 1, "steps": [
        { "id": "a", "type": "key_press", "keys": "f1" },
        { "id": "b", "type": "key_press", "keys": "f2" }
      ]})JSON");

    const StepOutcome first = f.executor.runSingleStep(script, 0);
    CHECK(first.ok);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f1)"});

    const StepOutcome second = f.executor.runSingleStep(script, 1);
    CHECK(second.ok);
    CHECK_EQ(join(f.input.calls), std::string{"keys(f1)|keys(f2)"});

    const StepOutcome outOfRange = f.executor.runSingleStep(script, 99);
    CHECK(!outOfRange.ok);
}

namespace {

/// Locator that reports a fixed outcome, for exercising CompositeLocator's
/// ordering and its error aggregation.
class ScriptedLocator : public rpa::core::ITargetLocator {
public:
    ScriptedLocator(bool succeeds, std::string reason)
        : succeeds_(succeeds), reason_(std::move(reason)) {}

    rpa::core::LocateResult locate(const rpa::core::Target&) override {
        ++calls;
        rpa::core::LocateResult result;
        if (succeeds_) {
            result.found = true;
            result.point = rpa::core::Point{7, 9};
            result.matchedText = reason_;
        } else {
            result.error = reason_;
        }
        return result;
    }

    bool captureToFile(const std::string&,
                       const std::optional<rpa::core::Rect>&,
                       std::string& error) override {
        error = reason_;
        return succeeds_;
    }

    int calls = 0;

private:
    bool succeeds_;
    std::string reason_;
};

}  // namespace

RPA_TEST(composite_locator_returns_the_first_hit_and_stops) {
    ScriptedLocator first(true, "uia");
    ScriptedLocator second(true, "vision");

    rpa::core::CompositeLocator composite;
    composite.addBackend(&first);
    composite.addBackend(&second);

    const auto result = composite.locate(rpa::core::Target{});
    CHECK(result.found);
    CHECK_EQ(result.matchedText, std::string("uia"));
    // The expensive backend is never reached when the cheap one answers.
    CHECK_EQ(second.calls, 0);
}

RPA_TEST(composite_locator_falls_through_to_the_next_backend) {
    ScriptedLocator first(false, "no such control");
    ScriptedLocator second(true, "vision");

    rpa::core::CompositeLocator composite;
    composite.addBackend(&first);
    composite.addBackend(&second);

    const auto result = composite.locate(rpa::core::Target{});
    CHECK(result.found);
    CHECK_EQ(result.matchedText, std::string("vision"));
}

RPA_TEST(composite_locator_reports_every_reason_it_failed) {
    // Both reasons matter: "the app exposes no controls" and "the label was
    // never on screen" are different problems with different fixes, and only
    // keeping the last one would hide whichever came first.
    ScriptedLocator first(false, "uia saw no Edit controls");
    ScriptedLocator second(false, "ocr never matched the label");

    rpa::core::CompositeLocator composite;
    composite.addBackend(&first);
    composite.addBackend(&second);

    const auto result = composite.locate(rpa::core::Target{});
    CHECK(!result.found);
    CHECK(result.error.find("uia saw no Edit controls") != std::string::npos);
    CHECK(result.error.find("ocr never matched the label") != std::string::npos);
}

RPA_TEST(composite_locator_with_no_backends_says_so) {
    rpa::core::CompositeLocator composite;
    composite.addBackend(nullptr);

    const auto result = composite.locate(rpa::core::Target{});
    CHECK(!result.found);
    CHECK(!result.error.empty());
}
