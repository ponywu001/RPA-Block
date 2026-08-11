#pragma once

#include <string>
#include <vector>

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

    /// Ask the agent to fix the steps its own last draft got wrong.
    /// `stepIssues` are the validator's complaints; `rawStepsJson` is the draft
    /// as written, including the rows that failed to parse.
    static std::string repairRequest(const std::vector<std::string>& stepIssues,
                                     const std::string& rawStepsJson);

    /// Ask the agent to diagnose a run that stopped on a failing step. A
    /// screenshot of the screen at the moment of failure is attached separately
    /// by the caller when the user agrees to send it.
    static std::string runFailureRequest(const core::Script& script,
                                         const std::string& failedStepId,
                                         const std::string& error,
                                         const std::string& logTail,
                                         bool screenshotAttached);
};

}  // namespace rpa::ai
