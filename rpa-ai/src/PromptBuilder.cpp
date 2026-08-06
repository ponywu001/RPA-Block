#include "rpa/ai/PromptBuilder.h"

#include "rpa/core/ScriptIO.h"

namespace rpa::ai {

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

}  // namespace rpa::ai
