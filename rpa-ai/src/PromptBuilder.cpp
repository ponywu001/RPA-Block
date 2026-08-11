#include "rpa/ai/PromptBuilder.h"

#include <optional>

#include "rpa/core/ScriptIO.h"

namespace rpa::ai {

namespace {

/// Render just the failing step as JSON, so the agent does not have to count
/// its way through the flow to find the one that broke.
std::string describeFailedStep(const core::Script& script, const std::string& stepId) {
    std::optional<core::Step> found;
    core::forEachStep(script.steps, [&](const core::Step& step) {
        if (!found && step.id == stepId) found = step;
    });
    if (!found) return {};

    core::Script isolated;
    isolated.name = script.name;
    isolated.steps.push_back(*found);
    return core::serializeScript(isolated, true);
}

}  // namespace

std::string PromptBuilder::currentFlowContext(const core::Script& script) {
    if (script.steps.empty()) {
        return "## Current flow\n\nThe editor is empty; you are starting from scratch.\n\n";
    }
    return "## Current flow\n\nThis is what the editor holds right now. Modify it rather "
           "than starting over, unless the user asks for a fresh flow.\n\n```json\n" +
           core::serializeScript(script, true) + "\n```\n\n";
}

std::string PromptBuilder::chatRequest(const std::string& userText, const core::Script& script) {
    return currentFlowContext(script) + userText;
}

std::string PromptBuilder::recordingToFlowRequest(const std::string& recordingSummary,
                                                  const core::Script& script) {
    std::string prompt = currentFlowContext(script);

    prompt +=
        "## Recorded session\n\n"
        "The user performed the task manually and it was recorded. Each entry lists the raw "
        "input event, and where available the UI Automation element and window under the "
        "cursor. Screenshot paths point at a crop around each click.\n\n```\n";
    prompt += recordingSummary;
    prompt += "```\n\n";

    prompt +=
        "## What to produce\n\n"
        "Turn this recording into a replayable flow. Replace every raw coordinate with an "
        "ocr_find or image_find anchor derived from the element name or nearby text in the "
        "recording — a coordinate-based flow breaks as soon as a window moves. Drop "
        "accidental clicks and stray keystrokes that do not contribute to the task. In "
        "`reply`, list anything you had to guess so the user can check it.";

    return prompt;
}

std::string PromptBuilder::repairRequest(const std::vector<std::string>& stepIssues,
                                         const std::string& rawStepsJson) {
    std::string prompt =
        "## Your last draft did not validate\n\n"
        "These are the steps you returned:\n\n```json\n";
    prompt += rawStepsJson.empty() ? "(the steps list was empty)" : rawStepsJson;
    prompt += "\n```\n\nThe client rejected these rows:\n\n";

    for (const std::string& issue : stepIssues) {
        prompt += "- ";
        prompt += issue;
        prompt += "\n";
    }

    prompt +=
        "\n## What to produce\n\n"
        "Fix these rows and return the **complete** flow again — every step, not just the "
        "corrected ones, because the client replaces the whole draft with what you send. "
        "Each step's `params_json` must be a JSON string that parses on its own and matches "
        "the parameters that step type accepts. Do not repeat the mistakes listed above; if "
        "a step cannot be expressed the way you intended, use a different step type rather "
        "than emitting the same invalid parameters. The person reading your `reply` cannot "
        "edit JSON, so describe the fix in plain terms.";

    return prompt;
}

std::string PromptBuilder::runFailureRequest(const core::Script& script,
                                             const std::string& failedStepId,
                                             const std::string& error,
                                             const std::string& logTail,
                                             bool screenshotAttached) {
    std::string prompt = currentFlowContext(script);

    prompt += "## The run failed\n\nStep `";
    prompt += failedStepId;
    prompt += "` stopped the flow with:\n\n```\n";
    prompt += error.empty() ? "(no error message was recorded)" : error;
    prompt += "\n```\n\n";

    const std::string failedStep = describeFailedStep(script, failedStepId);
    if (!failedStep.empty()) {
        prompt += "That step is:\n\n```json\n";
        prompt += failedStep;
        prompt += "\n```\n\n";
    }

    if (!logTail.empty()) {
        prompt += "The tail of the execution log:\n\n```\n";
        prompt += logTail;
        prompt += "\n```\n\n";
    }

    if (screenshotAttached) {
        prompt +=
            "The attached image is the screen at the moment the run stopped. It may not show "
            "what the flow expected — that difference is usually the diagnosis.\n\n";
    }

    prompt +=
        "## What to produce\n\n"
        "Work out why the step failed, then return the corrected flow in full. Common causes: "
        "the anchor text is not on screen at that point, OCR read it differently from how it "
        "is written (icons next to text get read as characters, and Chinese text carries no "
        "spaces), the window was not focused yet, or the flow moved on before the screen "
        "finished loading. Prefer anchors you can actually see in the attached screenshot over "
        "the ones that failed, and add a wait or a retry when the cause is timing. In `reply`, "
        "explain in plain language what went wrong and what you changed — the person reading it "
        "cannot inspect the flow's JSON.";

    return prompt;
}

}  // namespace rpa::ai
