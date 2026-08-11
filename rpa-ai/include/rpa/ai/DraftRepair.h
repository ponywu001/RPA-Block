#pragma once

#include <string>
#include <vector>

#include "rpa/ai/AgentTypes.h"

namespace rpa::ai {

/// Drives the "the agent wrote a step the IR rejects, ask it to fix that" loop.
///
/// The target user has no way to repair a malformed step by hand -- they cannot
/// read the JSON it lives in -- so a draft that only half-parses has to be sent
/// back to the agent rather than handed over with a list of complaints.
///
/// Free of Qt on purpose: the retry decision and the prompt assembly are what
/// can loop forever or burn money, and both are testable here without a network.
class DraftRepairSession {
public:
    /// `sentHistory` is the conversation as it was handed to the agent, current
    /// turn included.
    explicit DraftRepairSession(std::vector<ChatMessage> sentHistory, int maxAttempts = 2);

    /// Whether `reply` is worth sending back for another pass.
    bool shouldRepair(const AgentReply& reply) const;

    /// Build the follow-up conversation and count the attempt. `reply`'s cost is
    /// accumulated here, so every round is charged even though only the final
    /// reply reaches the transcript.
    std::vector<ChatMessage> nextRequest(const AgentReply& reply);

    /// Fold the accumulated cost of the discarded rounds into the reply the user
    /// finally sees. Without this the transcript quotes the price of one call
    /// while three were made.
    AgentReply finalize(AgentReply reply) const;

    int attempts() const { return attempts_; }

private:
    std::vector<ChatMessage> history_;
    int maxAttempts_;
    int attempts_ = 0;

    /// Issues from the previous round. Identical issues twice running means the
    /// agent is not converging, and a third identical round would only cost
    /// money -- so that is where the loop gives up.
    std::vector<std::string> previousIssues_;

    double accumulatedCost_ = 0.0;
    int accumulatedInputTokens_ = 0;
    int accumulatedOutputTokens_ = 0;
};

}  // namespace rpa::ai
