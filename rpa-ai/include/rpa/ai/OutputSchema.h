#pragma once

#include <string>

namespace rpa::ai {

/// The `output_schema` that constrains the agent's reply to a runnable RPA
/// draft. Returned as a JSON string so callers can embed it verbatim.
///
/// Shape (OutputSchemaV1):
///   reply        str   - Traditional Chinese explanation for the user
///   has_script   bool  - whether this turn produced a flow
///   script_name  str   - optional flow name
///   steps        list  - described_object per step: id / type / params_json / comment
///
/// `params_json` carries each step's parameters as a serialized JSON string.
/// OutputSchemaV1 has no recursive field type, so nested `if` / `loop` bodies
/// could not be expressed as real nested lists; folding them into a string
/// keeps arbitrarily deep flows expressible in a single turn. The client
/// re-parses and validates it against the IR on the way back in.
std::string rpaOutputSchemaJson();

/// System prompt prepended to the gateway's own extraction instructions. It
/// carries the step vocabulary and the anchor-over-coordinates rule.
std::string rpaSystemPrompt();

}  // namespace rpa::ai
