#include "rpa/ai/DraftRepair.h"

#include <utility>

#include "rpa/ai/PromptBuilder.h"

namespace rpa::ai {

DraftRepairSession::DraftRepairSession(std::vector<ChatMessage> sentHistory, int maxAttempts)
    : history_(std::move(sentHistory)), maxAttempts_(maxAttempts) {}

bool DraftRepairSession::shouldRepair(const AgentReply& reply) const {
    if (!reply.hasScript) return false;
    if (reply.stepIssues.empty()) return false;
    if (attempts_ >= maxAttempts_) return false;
    if (!previousIssues_.empty() && previousIssues_ == reply.stepIssues) return false;
    return true;
}

std::vector<ChatMessage> DraftRepairSession::nextRequest(const AgentReply& reply) {
    ++attempts_;
    previousIssues_ = reply.stepIssues;
    accumulatedCost_ += reply.costUsd;
    accumulatedInputTokens_ += reply.inputTokens;
    accumulatedOutputTokens_ += reply.outputTokens;

    // The rejected draft goes back as an assistant turn so the agent sees its
    // own rows next to the complaints about them; the issue list alone leaves it
    // guessing at what it wrote.
    ChatMessage draft;
    draft.role = ChatMessage::Role::Assistant;
    draft.text = reply.rawStepsJson.empty() ? "(no steps were returned)" : reply.rawStepsJson;
    history_.push_back(std::move(draft));

    ChatMessage request;
    request.role = ChatMessage::Role::User;
    request.text = PromptBuilder::repairRequest(reply.stepIssues, reply.rawStepsJson);
    history_.push_back(std::move(request));

    return history_;
}

AgentReply DraftRepairSession::finalize(AgentReply reply) const {
    reply.costUsd += accumulatedCost_;
    reply.inputTokens += accumulatedInputTokens_;
    reply.outputTokens += accumulatedOutputTokens_;
    return reply;
}

}  // namespace rpa::ai
