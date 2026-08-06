#pragma once

#include <string>

#include "rpa/core/Script.h"

namespace rpa::core {

struct ParseResult {
    bool ok = false;
    Script script;
    std::string error;
    std::vector<ValidationIssue> issues;
};

/// Parse a `.rpa.json` document. Structural JSON errors set `ok == false`;
/// semantic problems are reported through `issues` while still returning a
/// script, so the editor can show a partially-broken flow instead of nothing.
ParseResult parseScript(const std::string& json);

ParseResult loadScriptFile(const std::string& path);

std::string serializeScript(const Script& script, bool pretty = true);

bool saveScriptFile(const Script& script, const std::string& path, std::string& error);

/// Parse a single step from JSON. Used by the AI client, which receives steps
/// whose parameters arrive as a serialized `params_json` string.
bool parseStepFromParamsJson(const std::string& id,
                             const std::string& typeName,
                             const std::string& paramsJson,
                             const std::string& comment,
                             Step& out,
                             std::string& error);

}  // namespace rpa::core
