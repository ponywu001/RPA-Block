#include "TestHarness.h"

#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "rpa/ai/OutputSchema.h"
#include "rpa/ai/PromptBuilder.h"
#include "rpa/core/Script.h"
#include "rpa/core/ScriptIO.h"

using json = nlohmann::json;
using namespace rpa;

namespace {

/// The field types OutputSchemaV1 accepts, per the gateway's API contract.
/// Sending anything outside this set fails the run with a validation error, so
/// the schema this client builds has to stay inside it.
const std::set<std::string> kAllowedTypes = {
    "ranged_int", "ranged_float", "boolean", "bool", "str", "literal_str",
    "literal_int", "literal_float", "described_object", "list",
};

/// Walk a field (or a nested `items` / `entries[].value` spec) and assert it
/// only uses constructs the contract permits.
void checkField(const json& field, bool isNested, int depth) {
    CHECK(field.is_object());
    CHECK(depth < 10);

    // Nested specs must omit `name`; root fields must carry one.
    if (isNested) {
        CHECK(!field.contains("name"));
    } else {
        CHECK(field.contains("name"));
        CHECK(field["name"].is_string());
        CHECK(!field["name"].get<std::string>().empty());
    }

    CHECK(field.contains("type"));
    const std::string type = field["type"].get<std::string>();
    if (kAllowedTypes.find(type) == kAllowedTypes.end()) {
        ::rpa::test::fail(__FILE__, __LINE__, "field uses unsupported type: " + type);
    }

    if (type == "literal_str" || type == "literal_int" || type == "literal_float") {
        CHECK(field.contains("values"));
        CHECK(field["values"].is_array());
        CHECK(!field["values"].empty());
    }
    if (type == "ranged_int" || type == "ranged_float") {
        CHECK(field.contains("min"));
        CHECK(field.contains("max"));
    }
    if (type == "list") {
        CHECK(field.contains("items"));
        checkField(field["items"], true, depth + 1);
    }
    if (type == "described_object") {
        CHECK(field.contains("entries"));
        CHECK(field["entries"].is_array());
        CHECK(!field["entries"].empty());
        for (const auto& entry : field["entries"]) {
            CHECK(entry.contains("key"));
            CHECK(entry["key"].is_string());
            CHECK(entry.contains("value"));
            checkField(entry["value"], true, depth + 1);
        }
    }
}

json schema() {
    json parsed = json::parse(ai::rpaOutputSchemaJson(), nullptr, false);
    CHECK(!parsed.is_discarded());
    return parsed;
}

/// Find a root field by name.
const json* findField(const json& doc, const std::string& name) {
    for (const auto& field : doc["fields"]) {
        if (field.value("name", std::string{}) == name) return &field;
    }
    return nullptr;
}

}  // namespace

RPA_TEST(output_schema_is_valid_json_with_version_1) {
    const json doc = schema();
    CHECK(doc.is_object());
    CHECK_EQ(doc.value("version", 0), 1);
    CHECK(doc.contains("fields"));
    CHECK(doc["fields"].is_array());
    CHECK(!doc["fields"].empty());
}

RPA_TEST(output_schema_uses_only_contract_supported_constructs) {
    const json doc = schema();
    for (const auto& field : doc["fields"]) {
        checkField(field, false, 0);
    }
}

RPA_TEST(output_schema_declares_the_fields_the_client_reads) {
    const json doc = schema();

    // AgentClient::decodeStructuredOutput reads exactly these; a rename on one
    // side without the other would silently yield empty replies.
    const json* reply = findField(doc, "reply");
    CHECK(reply != nullptr);
    CHECK_EQ((*reply)["type"].get<std::string>(), std::string{"str"});

    const json* hasScript = findField(doc, "has_script");
    CHECK(hasScript != nullptr);
    CHECK_EQ((*hasScript)["type"].get<std::string>(), std::string{"bool"});

    const json* scriptName = findField(doc, "script_name");
    CHECK(scriptName != nullptr);
    CHECK(scriptName->value("optional", false));

    const json* steps = findField(doc, "steps");
    CHECK(steps != nullptr);
    CHECK_EQ((*steps)["type"].get<std::string>(), std::string{"list"});
    CHECK(steps->value("optional", false));
}

RPA_TEST(step_entries_match_what_the_client_decodes) {
    const json doc = schema();
    const json* steps = findField(doc, "steps");
    CHECK(steps != nullptr);

    const json& item = (*steps)["items"];
    CHECK_EQ(item["type"].get<std::string>(), std::string{"described_object"});

    std::set<std::string> keys;
    for (const auto& entry : item["entries"]) {
        keys.insert(entry["key"].get<std::string>());
    }

    CHECK(keys.count("id") == 1);
    CHECK(keys.count("type") == 1);
    CHECK(keys.count("params_json") == 1);
    CHECK(keys.count("comment") == 1);
}

RPA_TEST(step_type_enum_covers_exactly_the_ir_step_types) {
    const json doc = schema();
    const json* steps = findField(doc, "steps");
    CHECK(steps != nullptr);

    std::set<std::string> declared;
    for (const auto& entry : (*steps)["items"]["entries"]) {
        if (entry["key"].get<std::string>() != "type") continue;
        for (const auto& value : entry["value"]["values"]) {
            declared.insert(value.get<std::string>());
        }
    }

    // Every advertised name must round-trip through the IR parser: an enum
    // value the executor cannot build is a step the assistant can emit and the
    // client can only reject.
    for (const auto& name : declared) {
        core::StepType type;
        if (!core::parseStepType(name, type)) {
            ::rpa::test::fail(__FILE__, __LINE__,
                              "schema advertises a step type the IR cannot parse: " + name);
        }
    }

    // And the other direction: a step type the schema omits is one the
    // assistant can never produce.
    for (int i = 0; i <= static_cast<int>(core::StepType::HttpRequest); ++i) {
        const std::string name = core::toString(static_cast<core::StepType>(i));
        if (declared.find(name) == declared.end()) {
            ::rpa::test::fail(__FILE__, __LINE__,
                              "schema omits the step type: " + name);
        }
    }
}

RPA_TEST(system_prompt_documents_every_step_type) {
    const std::string prompt = ai::rpaSystemPrompt();
    CHECK(!prompt.empty());

    // The model only knows a step's parameter shape if the prompt spells it out.
    for (int i = 0; i <= static_cast<int>(core::StepType::HttpRequest); ++i) {
        const std::string name = core::toString(static_cast<core::StepType>(i));
        if (prompt.find(name) == std::string::npos) {
            ::rpa::test::fail(__FILE__, __LINE__,
                              "system prompt never mentions the step type: " + name);
        }
    }
}

RPA_TEST(system_prompt_states_the_anchor_over_coordinates_rule) {
    const std::string prompt = ai::rpaSystemPrompt();
    // This is the whole point of the recording hand-off; losing it would make
    // generated flows break on any window move.
    CHECK(prompt.find("ocr_find") != std::string::npos);
    CHECK(prompt.find("image_find") != std::string::npos);
    CHECK(prompt.find("coordinate") != std::string::npos);
}

RPA_TEST(prompt_builder_embeds_the_current_flow_as_parseable_json) {
    core::Script script;
    script.name = "existing";
    script.version = 1;
    core::Step step;
    step.id = "a";
    step.type = core::StepType::Wait;
    step.waitMs = 250;
    script.steps.push_back(step);

    const std::string context = ai::PromptBuilder::currentFlowContext(script);
    CHECK(context.find("Current flow") != std::string::npos);

    // Extract the fenced block and prove it is really the on-disk format, not
    // an approximation that would confuse the model.
    const size_t open = context.find("```json");
    const size_t close = context.rfind("```");
    CHECK(open != std::string::npos);
    CHECK(close > open);

    const std::string embedded = context.substr(open + 7, close - open - 7);
    const core::ParseResult parsed = core::parseScript(embedded);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.script.name, std::string{"existing"});
    CHECK_EQ(parsed.script.steps.size(), size_t{1});
    CHECK_EQ(parsed.script.steps[0].waitMs, 250);
}

RPA_TEST(prompt_builder_says_so_when_the_flow_is_empty) {
    const core::Script empty;
    const std::string context = ai::PromptBuilder::currentFlowContext(empty);
    CHECK(context.find("empty") != std::string::npos);
    // No JSON block, so the model is not handed a confusing `"steps": []`.
    CHECK(context.find("```json") == std::string::npos);
}

RPA_TEST(recording_prompt_carries_the_summary_and_the_rewrite_instruction) {
    const core::Script script;
    const std::string summary = "1. [0ms] click left at (512, 300)  -> element: Button \"Login\"";
    const std::string prompt = ai::PromptBuilder::recordingToFlowRequest(summary, script);

    CHECK(prompt.find(summary) != std::string::npos);
    CHECK(prompt.find("ocr_find") != std::string::npos);
    CHECK(prompt.find("coordinate") != std::string::npos);
}

RPA_TEST(schema_step_round_trips_through_the_ir_for_every_step_type) {
    // The params_json shapes the prompt advertises must actually decode. This
    // is the contract the whole "AI writes a runnable flow" claim rests on.
    struct Sample {
        const char* type;
        const char* params;
    };
    const Sample samples[] = {
        {"click", R"({"target":{"kind":"ocr","text":"Login","match":"exact"},"button":"left"})"},
        {"double_click", R"({"target":{"kind":"image","template":"a.png","threshold":0.9}})"},
        {"type_text", R"({"text":"hello {{user}}","interval_ms":10})"},
        {"key_press", R"({"keys":"ctrl+shift+s"})"},
        {"wait", R"({"ms":500})"},
        {"ocr_find",
         R"({"text":"Submit","match":"contains","offset_x":10,"retry":{"times":3,"interval_ms":500},"save_to_var":"btn"})"},
        {"image_find", R"({"template":"assets/b.png","threshold":0.85,"save_to_var":"btn"})"},
        {"window_activate", R"({"title_match":"ERP","match":"contains"})"},
        {"screenshot", R"({"path":"out/a.png"})"},
        {"if",
         R"({"condition":{"kind":"var_equals","variable":"x","value":"1"},"then_steps":[{"id":"t","type":"wait","ms":1}]})"},
        {"loop", R"({"count":3,"steps":[{"id":"l","type":"wait","ms":1}]})"},
        {"http_request",
         R"({"method":"POST","url":"https://example.com/h","headers":{"A":"b"},"body":"{}","save_to_var":"r"})"},
    };

    for (const auto& sample : samples) {
        core::Step step;
        std::string error;
        if (!core::parseStepFromParamsJson("s", sample.type, sample.params, "", step, error)) {
            ::rpa::test::fail(__FILE__, __LINE__,
                              std::string("params_json shape rejected for ") + sample.type +
                                  ": " + error);
        }
        CHECK_EQ(core::toString(step.type), std::string{sample.type});
    }
}

RPA_TEST(nested_bodies_survive_the_params_json_indirection) {
    // OutputSchemaV1 has no recursive type, so if/loop bodies ride inside the
    // params_json string. Prove a two-level nest actually comes back.
    core::Step step;
    std::string error;
    const bool ok = core::parseStepFromParamsJson(
        "outer", "loop",
        R"({"count":2,"steps":[
             {"id":"inner_if","type":"if",
              "condition":{"kind":"var_equals","variable":"m","value":"1"},
              "then_steps":[{"id":"deep","type":"key_press","keys":"enter"}]}
           ]})",
        "", step, error);

    CHECK(ok);
    CHECK_EQ(error, std::string{});
    CHECK_EQ(step.loopCount, 2);
    CHECK_EQ(step.loopSteps.size(), size_t{1});
    CHECK(step.loopSteps[0].type == core::StepType::If);
    CHECK_EQ(step.loopSteps[0].thenSteps.size(), size_t{1});
    CHECK_EQ(step.loopSteps[0].thenSteps[0].keys, std::string{"enter"});
}
