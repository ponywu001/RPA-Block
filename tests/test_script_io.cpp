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
