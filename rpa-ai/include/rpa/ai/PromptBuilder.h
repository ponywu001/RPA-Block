#pragma once

#include <string>

#include "rpa/core/Script.h"

namespace rpa::ai {

/// Assembles the text half of a request. Kept separate from AgentClient so the
/// wording can be tuned (and eyeballed in tests) without touching transport.
class PromptBuilder {
public:
    /// Context block describing the flow currently open in the editor.
    static std::string currentFlowContext(const core::Script& script);

    /// Wrap a plain chat message with the current-flow context.
    static std::string chatRequest(const std::string& userText, const core::Script& script);

    /// Ask the agent to convert a recording into a flow. `recordingSummary`
    /// comes from rpa-recorder's `toSummaryText`.
    static std::string recordingToFlowRequest(const std::string& recordingSummary,
                                              const core::Script& script);
};

}  // namespace rpa::ai
