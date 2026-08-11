#include "TestHarness.h"

#include <string>

#include "rpa/ai/ReplyDecoder.h"

using namespace rpa;

namespace {

std::string wrapStructured(const std::string& structuredBody, const std::string& extra = {}) {
    return "{\"structured_output\":{" + structuredBody + "}" + extra + "}";
}

}  // namespace

RPA_TEST(decoder_reads_a_clean_draft) {
    const std::string payload = wrapStructured(
        "\"reply\":\"做好了\",\"has_script\":true,\"script_name\":\"登入\","
        "\"steps\":[{\"id\":\"s1\",\"type\":\"wait\",\"params_json\":\"{\\\"ms\\\":500}\"}]",
        ",\"usage_cost\":{\"cost\":0.0123,\"input_tokens\":40,\"output_tokens\":7}");

    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(payload, reply, error));
    CHECK_EQ(reply.reply, std::string("做好了"));
    CHECK(reply.hasScript);
    CHECK_EQ(reply.scriptName, std::string("登入"));
    CHECK_EQ(reply.steps.size(), size_t{1});
    CHECK_EQ(reply.steps[0].id, std::string("s1"));
    CHECK(reply.stepIssues.empty());
    CHECK_EQ(reply.inputTokens, 40);
    CHECK_EQ(reply.outputTokens, 7);
    CHECK(reply.costUsd > 0.012 && reply.costUsd < 0.013);
}

RPA_TEST(decoder_rejects_a_payload_without_structured_output) {
    ai::AgentReply reply;
    std::string error;
    CHECK(!ai::decodeAgentState("{\"messages\":[]}", reply, error));
    CHECK(!error.empty());
}

RPA_TEST(decoder_rejects_unparseable_json) {
    ai::AgentReply reply;
    std::string error;
    CHECK(!ai::decodeAgentState("{not json", reply, error));
    CHECK(!error.empty());
}

RPA_TEST(decoder_reports_a_bad_step_without_losing_the_good_ones) {
    const std::string payload = wrapStructured(
        "\"has_script\":true,\"steps\":["
        "{\"id\":\"good\",\"type\":\"wait\",\"params_json\":\"{\\\"ms\\\":100}\"},"
        "{\"id\":\"bad\",\"type\":\"not_a_step\",\"params_json\":\"{}\"}]");

    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(payload, reply, error));
    CHECK_EQ(reply.steps.size(), size_t{1});
    CHECK_EQ(reply.steps[0].id, std::string("good"));
    CHECK_EQ(reply.stepIssues.size(), size_t{1});
    CHECK(reply.stepIssues[0].find("bad") != std::string::npos);
}

RPA_TEST(decoder_keeps_the_raw_steps_including_rejected_rows) {
    // The repair round shows the agent its own rows. A row that failed to parse
    // is absent from `steps`, so only the raw copy can carry it back.
    const std::string payload = wrapStructured(
        "\"has_script\":true,\"steps\":["
        "{\"id\":\"bad\",\"type\":\"not_a_step\",\"params_json\":\"{}\"}]");

    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(payload, reply, error));
    CHECK(reply.steps.empty());
    CHECK(reply.rawStepsJson.find("not_a_step") != std::string::npos);
    CHECK(reply.rawStepsJson.find("bad") != std::string::npos);
}

RPA_TEST(decoder_flags_a_script_claim_with_no_steps) {
    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(wrapStructured("\"has_script\":true,\"steps\":[]"), reply, error));
    CHECK(reply.hasScript);
    CHECK_EQ(reply.stepIssues.size(), size_t{1});
}

RPA_TEST(decoder_leaves_a_chat_only_turn_alone) {
    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(wrapStructured("\"reply\":\"要做什麼？\",\"has_script\":false"),
                               reply, error));
    CHECK(!reply.hasScript);
    CHECK(reply.stepIssues.empty());
    CHECK(reply.steps.empty());
}

RPA_TEST(payload_has_structured_output_distinguishes_snapshots) {
    CHECK(ai::payloadHasStructuredOutput(wrapStructured("\"reply\":\"x\"")));
    CHECK(!ai::payloadHasStructuredOutput("{\"messages\":[]}"));
    CHECK(!ai::payloadHasStructuredOutput("{\"structured_output\":null}"));
    CHECK(!ai::payloadHasStructuredOutput("not json"));
}

RPA_TEST(error_detail_extraction_quotes_the_server) {
    CHECK_EQ(ai::extractErrorDetail("{\"detail\":\"Invalid API key\"}"),
             std::string("Invalid API key"));
    CHECK_EQ(ai::extractErrorDetail("{\"message\":\"boom\"}"), std::string("boom"));
    CHECK_EQ(ai::extractErrorDetail("{\"error\":\"nope\"}"), std::string("nope"));

    // FastAPI's 422 detail is a list; the dump still names the offending field.
    const std::string validation =
        ai::extractErrorDetail("{\"detail\":[{\"loc\":[\"body\",\"assistant_id\"]}]}");
    CHECK(validation.find("assistant_id") != std::string::npos);

    CHECK(ai::extractErrorDetail("<html>502 Bad Gateway</html>").empty());
    CHECK(ai::extractErrorDetail("{\"unrelated\":1}").empty());
}
