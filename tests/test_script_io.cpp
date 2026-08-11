#include "TestHarness.h"

#include "rpa/core/ScriptIO.h"

using namespace rpa::core;

namespace {

const char* kSample = R"JSON({
  "name": "invoice-download",
  "version": 1,
  "description": "login and grab the monthly invoice",
  "variables": { "username": "demo" },
  "steps": [
    { "id": "s1", "type": "window_activate", "title_match": "ERP", "match": "contains" },
    { "id": "s2", "type": "type_text", "text": "{{username}}", "interval_ms": 20 },
    { "id": "s3", "type": "ocr_find", "text": "Login", "match": "exact",
      "retry": { "times": 5, "interval_ms": 500 }, "save_to_var": "login_btn" },
    { "id": "s4", "type": "click",
      "target": { "kind": "image", "template": "assets/login.png", "threshold": 0.9,
                  "offset_x": 4, "offset_y": -2 },
      "button": "left" },
    { "id": "s5", "type": "loop", "count": 3, "steps": [
        { "id": "s5a", "type": "wait", "ms": 100 }
    ]},
    { "id": "s6", "type": "if",
      "condition": { "kind": "var_equals", "variable": "username", "value": "demo" },
      "then_steps": [ { "id": "s6a", "type": "key_press", "keys": "ctrl+s" } ],
      "else_steps": [ { "id": "s6b", "type": "wait", "ms": 5 } ] }
  ]
})JSON";

}  // namespace

RPA_TEST(parses_a_well_formed_script) {
    const ParseResult result = parseScript(kSample);
    CHECK(result.ok);
    CHECK_EQ(result.error, std::string{});
    CHECK_EQ(result.script.name, std::string{"invoice-download"});
    CHECK_EQ(result.script.version, 1);
    CHECK_EQ(result.script.steps.size(), size_t{6});
    CHECK_EQ(result.script.variables.at("username"), std::string{"demo"});
    CHECK_EQ(result.issues.size(), size_t{0});
}

RPA_TEST(parses_ocr_find_inline_locator_fields) {
    const ParseResult result = parseScript(kSample);
    const Step& step = result.script.steps[2];
    CHECK_EQ(toString(step.type), std::string{"ocr_find"});
    CHECK_EQ(step.target.text, std::string{"Login"});
    CHECK(step.target.match == MatchMode::Exact);
    CHECK_EQ(step.target.retry.times, 5);
    CHECK_EQ(step.target.retry.intervalMs, 500);
    CHECK_EQ(step.saveToVar, std::string{"login_btn"});
}

RPA_TEST(parses_nested_target_object_for_click) {
    const ParseResult result = parseScript(kSample);
    const Step& step = result.script.steps[3];
    CHECK(step.target.kind == TargetKind::Image);
    CHECK_EQ(step.target.templatePath, std::string{"assets/login.png"});
    CHECK(step.target.threshold > 0.89 && step.target.threshold < 0.91);
    CHECK_EQ(step.target.offsetX, 4);
    CHECK_EQ(step.target.offsetY, -2);
}

RPA_TEST(parses_nested_loop_and_if_bodies) {
    const ParseResult result = parseScript(kSample);
    CHECK_EQ(result.script.steps[4].loopCount, 3);
    CHECK_EQ(result.script.steps[4].loopSteps.size(), size_t{1});
    CHECK(result.script.steps[5].condition.has_value());
    CHECK(result.script.steps[5].condition->kind == Condition::Kind::VarEquals);
    CHECK_EQ(result.script.steps[5].thenSteps.size(), size_t{1});
    CHECK_EQ(result.script.steps[5].elseSteps.size(), size_t{1});
}

RPA_TEST(round_trips_through_serialize_and_parse) {
    const ParseResult first = parseScript(kSample);
    CHECK(first.ok);

    const std::string serialized = serializeScript(first.script);
    const ParseResult second = parseScript(serialized);
    CHECK(second.ok);
    CHECK_EQ(second.issues.size(), size_t{0});

    // Comparing the re-serialized forms proves the round trip is a fixed point,
    // which is what the editor's save/load cycle depends on.
    CHECK_EQ(serializeScript(second.script), serialized);
}

RPA_TEST(round_trips_launch_app_with_its_optional_fields) {
    const ParseResult result = parseScript(R"JSON({
      "name": "open-erp", "version": 1, "steps": [
        { "id": "open", "type": "launch_app",
          "path": "C:\\Program Files\\ERP\\erp.exe",
          "args": "--profile prod", "working_dir": "C:\\Program Files\\ERP" }
      ]})JSON");
    CHECK(result.ok);
    CHECK_EQ(result.issues.size(), size_t{0});

    const Step& step = result.script.steps[0];
    CHECK(step.type == StepType::LaunchApp);
    CHECK_EQ(step.path, std::string{"C:\\Program Files\\ERP\\erp.exe"});
    CHECK_EQ(step.launchArgs, std::string{"--profile prod"});
    CHECK_EQ(step.workingDir, std::string{"C:\\Program Files\\ERP"});

    const std::string serialized = serializeScript(result.script);
    const ParseResult second = parseScript(serialized);
    CHECK(second.ok);
    CHECK_EQ(serializeScript(second.script), serialized);
}

RPA_TEST(omits_the_optional_launch_app_fields_when_unset) {
    // `args` and `working_dir` are optional, and writing them out as empty
    // strings would leave every hand-read flow noisier than it needs to be.
    Script script;
    script.name = "open";
    Step step;
    step.id = "open";
    step.type = StepType::LaunchApp;
    step.path = "notepad.exe";
    script.steps.push_back(step);

    const std::string serialized = serializeScript(script);
    CHECK(serialized.find("\"path\"") != std::string::npos);
    CHECK(serialized.find("\"args\"") == std::string::npos);
    CHECK(serialized.find("\"working_dir\"") == std::string::npos);
}

RPA_TEST(reports_launch_app_without_a_path) {
    const ParseResult result = parseScript(R"JSON({
      "name": "bad", "version": 1, "steps": [
        { "id": "a", "type": "launch_app", "args": "--now" }
      ]})JSON");
    CHECK(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.message.find("launch_app needs a path") != std::string::npos) found = true;
    }
    CHECK(found);
}

RPA_TEST(rejects_malformed_json) {
    const ParseResult result = parseScript("{ not json");
    CHECK(!result.ok);
    CHECK_EQ(result.error, std::string{"invalid JSON"});
}

RPA_TEST(reports_duplicate_step_ids) {
    const ParseResult result = parseScript(R"JSON({
      "name": "dup", "version": 1, "steps": [
        { "id": "a", "type": "wait", "ms": 1 },
        { "id": "a", "type": "wait", "ms": 1 }
      ]})JSON");
    CHECK(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.message.find("duplicate step id") != std::string::npos) found = true;
    }
    CHECK(found);
}

RPA_TEST(reports_unresolvable_goto_target) {
    const ParseResult result = parseScript(R"JSON({
      "name": "goto", "version": 1, "steps": [
        { "id": "a", "type": "wait", "ms": 1, "on_fail": "goto:nowhere" }
      ]})JSON");
    CHECK(result.ok);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.message.find("does not exist") != std::string::npos) found = true;
    }
    CHECK(found);
}

RPA_TEST(reports_missing_required_fields_per_step_type) {
    const ParseResult result = parseScript(R"JSON({
      "name": "bad", "version": 1, "steps": [
        { "id": "a", "type": "http_request", "url": "ftp://example.com" },
        { "id": "b", "type": "image_find", "template": "", "threshold": 2.0 },
        { "id": "c", "type": "loop", "count": 0, "steps": [] }
      ]})JSON");
    CHECK(result.ok);
    CHECK(result.issues.size() >= 4);
}

RPA_TEST(parses_step_from_ai_params_json) {
    Step step;
    std::string error;
    const bool ok = parseStepFromParamsJson(
        "ai_1", "ocr_find",
        R"({"text":"登入","match":"contains","offset_x":80,"retry":{"times":2,"interval_ms":250}})",
        "generated by the assistant", step, error);

    CHECK(ok);
    CHECK_EQ(error, std::string{});
    CHECK_EQ(step.id, std::string{"ai_1"});
    CHECK(step.type == StepType::OcrFind);
    CHECK(step.target.match == MatchMode::Contains);
    CHECK_EQ(step.target.offsetX, 80);
    CHECK_EQ(step.target.retry.times, 2);
    CHECK_EQ(step.comment, std::string{"generated by the assistant"});
}

RPA_TEST(rejects_ai_step_with_unknown_type) {
    Step step;
    std::string error;
    CHECK(!parseStepFromParamsJson("x", "teleport", "{}", "", step, error));
    CHECK(error.find("unknown step type") != std::string::npos);
}

RPA_TEST(rejects_ai_step_with_broken_params_json) {
    Step step;
    std::string error;
    CHECK(!parseStepFromParamsJson("x", "wait", "{oops", "", step, error));
    CHECK(error.find("not valid JSON") != std::string::npos);
}

RPA_TEST(relative_target_survives_a_round_trip) {
    Script script;
    script.name = "relative";
    Step click;
    click.id = "fill_name";
    click.type = StepType::Click;
    click.target.kind = TargetKind::Relative;
    click.target.text = "客戶全稱";
    click.target.match = MatchMode::Contains;
    click.target.direction = Direction::Right;
    click.target.role = ElementRole::Input;
    click.target.maxDistance = 250;
    script.steps.push_back(click);

    const ParseResult reloaded = parseScript(serializeScript(script));
    CHECK(reloaded.ok);

    const Target& t = reloaded.script.steps.front().target;
    CHECK_EQ(static_cast<int>(t.kind), static_cast<int>(TargetKind::Relative));
    CHECK_EQ(t.text, std::string("客戶全稱"));
    CHECK_EQ(static_cast<int>(t.direction), static_cast<int>(Direction::Right));
    CHECK_EQ(static_cast<int>(t.role), static_cast<int>(ElementRole::Input));
    CHECK_EQ(t.maxDistance, 250);
}

RPA_TEST(relative_target_without_an_anchor_is_a_validation_issue) {
    Script script;
    script.name = "no anchor";
    Step click;
    click.id = "click";
    click.type = StepType::Click;
    click.target.kind = TargetKind::Relative;
    click.target.text = "";
    script.steps.push_back(click);

    const auto issues = validate(script);
    CHECK(!issues.empty());
}

RPA_TEST(every_direction_and_role_round_trips_through_its_name) {
    // Guards the enum-to-string tables: a value that serialises to something
    // parseDirection does not accept would silently come back as "right" on
    // reload, quietly retargeting a step at the other side of the form.
    for (int i = 0; i < 4; ++i) {
        const auto direction = static_cast<Direction>(i);
        Direction parsed = Direction::Below;
        CHECK(parseDirection(toString(direction), parsed));
        CHECK_EQ(static_cast<int>(parsed), i);
    }
    for (int i = 0; i < 4; ++i) {
        const auto role = static_cast<ElementRole>(i);
        ElementRole parsed = ElementRole::Button;
        CHECK(parseElementRole(toString(role), parsed));
        CHECK_EQ(static_cast<int>(parsed), i);
    }
}
