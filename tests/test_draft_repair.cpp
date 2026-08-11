#include "TestHarness.h"

#include <string>
#include <vector>

#include "rpa/ai/DraftRepair.h"
#include "rpa/ai/PromptBuilder.h"

using namespace rpa;

namespace {

std::vector<ai::ChatMessage> initialHistory() {
    ai::ChatMessage turn;
    turn.role = ai::ChatMessage::Role::User;
    turn.text = "幫我做一個登入流程";
    return {turn};
}

ai::AgentReply draftWithIssues(std::vector<std::string> issues) {
    ai::AgentReply reply;
    reply.hasScript = true;
    reply.stepIssues = std::move(issues);
    reply.rawStepsJson = "[{\"id\":\"s1\",\"type\":\"clik\",\"params_json\":\"{}\"}]";
    return reply;
}

}  // namespace

RPA_TEST(repair_skips_a_clean_draft) {
    ai::DraftRepairSession session(initialHistory());

    ai::AgentReply reply;
    reply.hasScript = true;
    CHECK(!session.shouldRepair(reply));
}

RPA_TEST(repair_skips_a_chat_only_turn) {
    ai::DraftRepairSession session(initialHistory());

    ai::AgentReply reply;
    reply.hasScript = false;
    reply.stepIssues = {"step 1: not an object"};
    CHECK(!session.shouldRepair(reply));
}

RPA_TEST(repair_retries_a_draft_with_issues) {
    ai::DraftRepairSession session(initialHistory());
    CHECK(session.shouldRepair(draftWithIssues({"step 1 (s1): unknown step type"})));
}

RPA_TEST(repair_stops_at_the_attempt_ceiling) {
    ai::DraftRepairSession session(initialHistory(), 2);

    session.nextRequest(draftWithIssues({"issue A"}));
    CHECK(session.shouldRepair(draftWithIssues({"issue B"})));

    session.nextRequest(draftWithIssues({"issue B"}));
    CHECK_EQ(session.attempts(), 2);
    CHECK(!session.shouldRepair(draftWithIssues({"issue C"})));
}

RPA_TEST(repair_gives_up_when_the_same_issues_come_back) {
    // Identical complaints twice running means the agent is not converging, and
    // a third round would only cost money.
    ai::DraftRepairSession session(initialHistory(), 3);

    session.nextRequest(draftWithIssues({"step 1 (s1): unknown step type"}));
    CHECK(!session.shouldRepair(draftWithIssues({"step 1 (s1): unknown step type"})));
    CHECK(session.shouldRepair(draftWithIssues({"step 2 (s2): missing url"})));
}

RPA_TEST(repair_request_carries_the_rejected_rows_and_the_complaints) {
    ai::DraftRepairSession session(initialHistory());

    const auto history = session.nextRequest(draftWithIssues({"step 1 (s1): unknown step type"}));

    CHECK_EQ(history.size(), size_t{3});
    CHECK(history[1].role == ai::ChatMessage::Role::Assistant);
    CHECK(history[1].text.find("clik") != std::string::npos);

    CHECK(history[2].role == ai::ChatMessage::Role::User);
    CHECK(history[2].text.find("unknown step type") != std::string::npos);
    CHECK(history[2].text.find("clik") != std::string::npos);
    // The client replaces the whole draft, so a partial answer would silently
    // delete the steps that were fine.
    CHECK(history[2].text.find("complete") != std::string::npos);
}

RPA_TEST(repair_request_grows_the_history_across_rounds) {
    ai::DraftRepairSession session(initialHistory(), 3);

    const auto first = session.nextRequest(draftWithIssues({"issue A"}));
    const auto second = session.nextRequest(draftWithIssues({"issue B"}));

    CHECK_EQ(first.size(), size_t{3});
    CHECK_EQ(second.size(), size_t{5});
    CHECK(second[4].text.find("issue B") != std::string::npos);
}

RPA_TEST(repair_accumulates_the_cost_of_discarded_rounds) {
    ai::DraftRepairSession session(initialHistory(), 3);

    auto first = draftWithIssues({"issue A"});
    first.costUsd = 0.01;
    first.inputTokens = 100;
    first.outputTokens = 20;
    session.nextRequest(first);

    auto second = draftWithIssues({"issue B"});
    second.costUsd = 0.02;
    second.inputTokens = 150;
    second.outputTokens = 30;
    session.nextRequest(second);

    ai::AgentReply finalReply;
    finalReply.costUsd = 0.03;
    finalReply.inputTokens = 200;
    finalReply.outputTokens = 40;

    const ai::AgentReply merged = session.finalize(finalReply);
    CHECK(merged.costUsd > 0.0599 && merged.costUsd < 0.0601);
    CHECK_EQ(merged.inputTokens, 450);
    CHECK_EQ(merged.outputTokens, 90);
}

RPA_TEST(repair_handles_a_draft_that_returned_no_steps_at_all) {
    ai::DraftRepairSession session(initialHistory());

    ai::AgentReply reply;
    reply.hasScript = true;
    reply.stepIssues = {"has_script was true but the steps list was empty"};

    CHECK(session.shouldRepair(reply));
    const auto history = session.nextRequest(reply);
    CHECK(history.back().text.find("steps list was empty") != std::string::npos);
}

RPA_TEST(run_failure_request_names_the_step_and_quotes_the_error) {
    core::Script script;
    script.name = "erp-login";

    core::Step click;
    click.id = "click-login";
    click.type = core::StepType::Click;
    click.target.kind = core::TargetKind::Ocr;
    click.target.text = "登入";
    script.steps.push_back(click);

    const std::string prompt = ai::PromptBuilder::runFailureRequest(
        script, "click-login", "no OCR match for 登入", "1: started\n2: locating…", true);

    CHECK(prompt.find("click-login") != std::string::npos);
    CHECK(prompt.find("no OCR match") != std::string::npos);
    CHECK(prompt.find("locating…") != std::string::npos);
    CHECK(prompt.find("attached image") != std::string::npos);
    // The whole flow travels with it, because the fix may live in an earlier step.
    CHECK(prompt.find("Current flow") != std::string::npos);
}

RPA_TEST(run_failure_request_omits_the_screenshot_note_when_none_was_attached) {
    core::Script script;
    core::Step wait;
    wait.id = "w1";
    wait.type = core::StepType::Wait;
    wait.waitMs = 100;
    script.steps.push_back(wait);

    const std::string prompt =
        ai::PromptBuilder::runFailureRequest(script, "w1", "boom", "", false);

    CHECK(prompt.find("attached image") == std::string::npos);
    CHECK(prompt.find("boom") != std::string::npos);
}
