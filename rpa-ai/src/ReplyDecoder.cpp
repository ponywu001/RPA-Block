#include "rpa/ai/ReplyDecoder.h"

#include <nlohmann/json.hpp>

#include "rpa/core/ScriptIO.h"

namespace rpa::ai {

using json = nlohmann::json;

namespace {

/// Rebuild IR steps from the agent's described_object rows. Each row's
/// `params_json` is re-parsed and validated; a bad row is reported rather than
/// discarding the whole draft.
void decodeSteps(const json& steps, AgentReply& reply) {
    if (!steps.is_array()) return;

    // Kept verbatim so a repair round can show the agent the rows it wrote,
    // including the ones that failed to parse and are therefore absent from
    // `steps` below.
    reply.rawStepsJson = steps.dump(2);

    int index = 0;
    for (const auto& entry : steps) {
        ++index;
        if (!entry.is_object()) {
            reply.stepIssues.push_back("step " + std::to_string(index) + ": not an object");
            continue;
        }

        const std::string id = entry.value("id", "step_" + std::to_string(index));
        const std::string type = entry.value("type", "");
        const std::string params = entry.value("params_json", "{}");
        const std::string comment = entry.value("comment", "");

        core::Step step;
        std::string error;
        if (!core::parseStepFromParamsJson(id, type, params, comment, step, error)) {
            reply.stepIssues.push_back("step " + std::to_string(index) + " (" + id + "): " + error);
            continue;
        }
        reply.steps.push_back(std::move(step));
    }
}

}  // namespace

bool decodeAgentState(const std::string& valuesPayload, AgentReply& reply, std::string& error) {
    const json state = json::parse(valuesPayload, nullptr, false);
    if (state.is_discarded() || !state.is_object()) {
        error = "could not parse the gateway's final state payload";
        return false;
    }

    auto structured = state.find("structured_output");
    if (structured == state.end() || !structured->is_object()) {
        error = "response contained no structured_output";
        return false;
    }

    reply.reply = structured->value("reply", "");
    reply.hasScript = structured->value("has_script", false);
    reply.scriptName = structured->value("script_name", "");

    auto steps = structured->find("steps");
    if (steps != structured->end()) decodeSteps(*steps, reply);

    // A turn that claims a script but yielded no usable step is a failure the
    // user needs to see, not an empty "applied" action.
    if (reply.hasScript && reply.steps.empty() && reply.stepIssues.empty()) {
        reply.stepIssues.push_back("has_script was true but the steps list was empty");
    }

    auto usage = state.find("usage_cost");
    if (usage != state.end() && usage->is_object()) {
        reply.costUsd = usage->value("cost", 0.0);
        reply.inputTokens = usage->value("input_tokens", 0);
        reply.outputTokens = usage->value("output_tokens", 0);
    }

    return true;
}

bool payloadHasStructuredOutput(const std::string& valuesPayload) {
    const json state = json::parse(valuesPayload, nullptr, false);
    if (state.is_discarded() || !state.is_object()) return false;
    auto structured = state.find("structured_output");
    return structured != state.end() && structured->is_object();
}

std::string extractErrorDetail(const std::string& body) {
    const json doc = json::parse(body, nullptr, false);
    if (doc.is_discarded()) return {};

    if (doc.is_string()) return doc.get<std::string>();
    if (!doc.is_object()) return {};

    for (const char* key : {"detail", "message", "error"}) {
        auto found = doc.find(key);
        if (found == doc.end()) continue;
        if (found->is_string()) return found->get<std::string>();
        // FastAPI's 422 detail is a list of per-field validation objects; the
        // dump is ugly but it names the offending field, which is the point.
        if (!found->is_null()) return found->dump();
    }
    return {};
}

}  // namespace rpa::ai
