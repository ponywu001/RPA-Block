#include "TestHarness.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rpa/ai/ReplyDecoder.h"
#include "rpa/ai/SseParser.h"

using namespace rpa;

namespace {

/// Feed a stream one byte at a time. Chunk boundaries on a real connection land
/// wherever TCP puts them, so anything that only works on whole frames is a bug
/// waiting for a slow network.
std::vector<ai::SseEvent> parseByteByByte(const std::string& stream) {
    ai::SseParser parser;
    std::vector<ai::SseEvent> events;
    for (char c : stream) {
        parser.feed(std::string_view(&c, 1), events);
    }
    parser.flush(events);
    return events;
}

std::vector<ai::SseEvent> parseWhole(const std::string& stream) {
    ai::SseParser parser;
    std::vector<ai::SseEvent> events;
    parser.feed(stream, events);
    parser.flush(events);
    return events;
}

}  // namespace

RPA_TEST(sse_parses_lf_frames) {
    const std::string stream =
        "event: metadata\ndata: {\"run_id\":\"r1\"}\n\n"
        "event: values\ndata: {\"a\":1}\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{2});
    CHECK_EQ(events[0].name, std::string("metadata"));
    CHECK_EQ(events[0].data, std::string("{\"run_id\":\"r1\"}"));
    CHECK_EQ(events[1].name, std::string("values"));
    CHECK_EQ(events[1].data, std::string("{\"a\":1}"));
}

RPA_TEST(sse_parses_crlf_frames) {
    const std::string stream =
        "event: values\r\ndata: {\"a\":1}\r\n\r\n"
        "event: values\r\ndata: {\"a\":2}\r\n\r\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{2});
    CHECK_EQ(events[1].data, std::string("{\"a\":2}"));
}

RPA_TEST(sse_survives_chunk_boundary_inside_crlf) {
    // The byte-by-byte feed splits every "\r\n" in half, which is exactly the
    // case a naive replace("\r\n", "\n") on a per-chunk buffer gets wrong.
    const std::string stream = "event: values\r\ndata: {\"a\":1}\r\n\r\n";

    const auto events = parseByteByByte(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].name, std::string("values"));
    CHECK_EQ(events[0].data, std::string("{\"a\":1}"));
}

RPA_TEST(sse_treats_lone_cr_as_a_line_break) {
    const std::string stream = "event: values\rdata: {\"a\":1}\r\r";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].data, std::string("{\"a\":1}"));
}

RPA_TEST(sse_joins_multiline_data) {
    const std::string stream = "event: values\ndata: {\"a\":\ndata: 1}\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].data, std::string("{\"a\":\n1}"));
}

RPA_TEST(sse_strips_exactly_one_space_after_the_colon) {
    // Two spaces means the value genuinely starts with one. Trimming the whole
    // field would corrupt any data that legitimately begins with whitespace.
    const std::string stream = "event: values\ndata:  x\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].data, std::string(" x"));
}

RPA_TEST(sse_ignores_keepalive_comments) {
    const std::string stream = ": ping\n\n: ping\nevent: values\ndata: {}\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].name, std::string("values"));
}

RPA_TEST(sse_drops_frames_carrying_no_data) {
    const std::string stream = "event: end\n\nevent: values\ndata: {}\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].name, std::string("values"));
}

RPA_TEST(sse_flush_emits_a_frame_with_no_trailing_blank_line) {
    // A server that closes right after the terminal frame is the normal case,
    // and that frame is the one carrying the result.
    const std::string stream = "event: values\ndata: {\"final\":true}\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].data, std::string("{\"final\":true}"));
}

RPA_TEST(sse_flush_emits_nothing_for_an_empty_stream) {
    ai::SseParser parser;
    std::vector<ai::SseEvent> events;
    parser.feed("", events);
    parser.feed("\n\n   \n", events);
    parser.flush(events);
    CHECK(events.empty());
}

RPA_TEST(sse_handles_data_field_without_a_space) {
    const std::string stream = "event:values\ndata:{\"a\":1}\n\n";

    const auto events = parseWhole(stream);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].name, std::string("values"));
    CHECK_EQ(events[0].data, std::string("{\"a\":1}"));
}

RPA_TEST(sse_byte_by_byte_matches_whole_stream_parse) {
    const std::string stream =
        "event: metadata\r\ndata: {\"run_id\":\"r1\"}\r\n\r\n"
        ": keep-alive\r\n\r\n"
        "event: values\r\ndata: {\"structured_output\":{\"reply\":\"hi\"}}\r\n\r\n"
        "event: values\r\ndata: {\"structured_output\":{\"reply\":\"done\"}}\r\n";

    const auto chunked = parseByteByByte(stream);
    const auto whole = parseWhole(stream);

    CHECK_EQ(chunked.size(), whole.size());
    CHECK_EQ(chunked.size(), size_t{3});
    for (size_t i = 0; i < chunked.size(); ++i) {
        CHECK_EQ(chunked[i].name, whole[i].name);
        CHECK_EQ(chunked[i].data, whole[i].data);
    }
}

RPA_TEST(sse_reset_discards_a_partial_frame) {
    ai::SseParser parser;
    std::vector<ai::SseEvent> events;
    parser.feed("event: values\ndata: {\"a\":", events);
    CHECK(events.empty());

    parser.reset();
    parser.feed("event: values\ndata: {\"b\":2}\n\n", events);
    CHECK_EQ(events.size(), size_t{1});
    CHECK_EQ(events[0].data, std::string("{\"b\":2}"));
}

// --- Recorded gateway stream -----------------------------------------------
//
// tests/data/gateway-stream.sse is a real response captured with
// `rpa-ai-probe chat ... --dump-sse`. Everything above this line tests what the
// spec allows; this tests what the service actually does, which is the only
// thing that stops the framing from drifting back to a guess.

RPA_TEST(recorded_gateway_stream_decodes_to_the_drafted_flow) {
    std::ifstream file(std::string(RPA_TEST_DATA_DIR) + "/gateway-stream.sse",
                       std::ios::binary);
    CHECK(file.is_open());
    const std::string stream((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    CHECK(!stream.empty());

    // Fed in 64-byte slices: the recording arrived over many reads, and the
    // parser has to behave the same however the bytes are cut up.
    ai::SseParser parser;
    std::vector<ai::SseEvent> events;
    for (size_t offset = 0; offset < stream.size(); offset += 64) {
        parser.feed(std::string_view(stream).substr(offset, 64), events);
    }
    parser.flush(events);

    CHECK_EQ(events.size(), size_t{3});
    CHECK_EQ(events[0].name, std::string("metadata"));
    CHECK_EQ(events[1].name, std::string("values"));
    CHECK_EQ(events[2].name, std::string("values"));

    // The terminal snapshot is the one carrying the result; the earlier one
    // does not, which is what makes "last values wins" the wrong rule.
    CHECK(!ai::payloadHasStructuredOutput(events[1].data));
    CHECK(ai::payloadHasStructuredOutput(events[2].data));

    ai::AgentReply reply;
    std::string error;
    CHECK(ai::decodeAgentState(events[2].data, reply, error));
    CHECK(reply.hasScript);
    CHECK(reply.stepIssues.empty());
    CHECK_EQ(reply.steps.size(), size_t{4});
    CHECK(reply.costUsd > 0.0);
    CHECK(reply.inputTokens > 0);

    // The recording answers "開啟記事本，輸入「哈囉」，然後存檔". Chinese has to
    // survive the whole path — request encoding, the agent's nested
    // params_json, and this client's re-parse of it — and nothing along that
    // path fails loudly when it does not: a flow that types mojibake runs
    // perfectly well.
    const core::Step* typing = nullptr;
    for (const core::Step& step : reply.steps) {
        if (step.type == core::StepType::TypeText) typing = &step;
    }
    CHECK(typing != nullptr);
    CHECK_EQ(typing->text, std::string("\xE5\x93\x88\xE5\x9B\x89"));  // 哈囉
}
