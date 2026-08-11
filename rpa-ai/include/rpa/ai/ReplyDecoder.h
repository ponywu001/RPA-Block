#pragma once

#include <string>

#include "rpa/ai/AgentTypes.h"

namespace rpa::ai {

/// Turn a gateway `values` state payload into an AgentReply.
///
/// Takes the payload as raw JSON text rather than a parsed document so the Qt
/// transport layer never has to name a JSON type, and so this whole contract --
/// the piece that breaks when the gateway changes shape -- is testable without
/// a network stack.
bool decodeAgentState(const std::string& valuesPayload, AgentReply& reply, std::string& error);

/// Whether a payload carries a `structured_output` object. This is the probe's
/// success condition: reaching the gateway is not the same as it answering in
/// the agreed shape.
bool payloadHasStructuredOutput(const std::string& valuesPayload);

/// Pull a human-readable reason out of an HTTP error body. Gateways answer 401
/// and 422 with JSON (`detail`, `message`, or `error`), and that text is the
/// difference between "the key is wrong" and "the payload is wrong" -- Qt's own
/// errorString says neither. Returns empty when the body carries nothing useful.
std::string extractErrorDetail(const std::string& body);

}  // namespace rpa::ai
