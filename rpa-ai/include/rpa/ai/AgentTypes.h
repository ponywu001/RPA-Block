#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rpa/core/Script.h"

namespace rpa::ai {

enum class AuthMode {
    ApiKey,  ///< X-API-Key: <teamsync user api key>
    Jwt,     ///< Authorization: Bearer <access_token>
};

struct AgentSettings {
    /// Full gateway base, e.g. https://agents.scfg.io/structured-multimodal-agent
    std::string gatewayUrl = "https://agents.scfg.io/structured-multimodal-agent";
    /// Must match the graph id registered in langgraph.json.
    std::string assistantId = "structured-multimodal-agent";

    AuthMode authMode = AuthMode::ApiKey;
    std::string apiKey;
    /// Used when authMode == Jwt; obtained from the TeamSync sign-in endpoint.
    std::string accessToken;
    std::string signInUrl = "https://api.cluster.scfg.io/public/auth/signin";

    /// Optional per-run overrides. Empty means "use the server default".
    std::string provider;  // "claude" | "gemini"
    std::string model;

    int maxOutputTokens = 8192;
    int timeoutMs = 120000;
};

/// One turn in the local conversation. The gateway contract takes only user
/// messages, so assistant turns are folded into the prompt text on send.
struct ChatMessage {
    enum class Role { User, Assistant, System };

    Role role = Role::User;
    std::string text;
    /// PNG bytes attached to a user turn; encoded as a data URI on the wire.
    std::vector<unsigned char> imagePng;
};

/// Decoded `structured_output` from the agent.
struct AgentReply {
    std::string reply;
    bool hasScript = false;
    std::string scriptName;

    /// Steps the client managed to reconstruct into the IR.
    core::StepList steps;
    /// Per-step problems: the step's index, id, and what went wrong. Reported
    /// rather than swallowed so the user can fix a partial draft by hand.
    std::vector<std::string> stepIssues;

    double costUsd = 0.0;
    int inputTokens = 0;
    int outputTokens = 0;
};

struct AgentError {
    std::string message;
    int httpStatus = 0;
};

}  // namespace rpa::ai
