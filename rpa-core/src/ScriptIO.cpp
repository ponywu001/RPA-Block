#include "rpa/core/ScriptIO.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace rpa::core {

using json = nlohmann::json;

namespace {

std::string getString(const json& j, const char* key, const std::string& fallback = {}) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

int getInt(const json& j, const char* key, int fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_float()) return static_cast<int>(it->get<double>());
    if (it->is_string()) {
        try {
            return std::stoi(it->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

double getDouble(const json& j, const char* key, double fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool getBool(const json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    return fallback;
}

Rect parseRect(const json& j) {
    Rect r;
    r.x = getInt(j, "x", 0);
    r.y = getInt(j, "y", 0);
    r.width = getInt(j, "width", 0);
    r.height = getInt(j, "height", 0);
    return r;
}

json dumpRect(const Rect& r) {
    return json{{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}};
}

RetryPolicy parseRetry(const json& j) {
    RetryPolicy p;
    p.times = getInt(j, "times", p.times);
    p.intervalMs = getInt(j, "interval_ms", p.intervalMs);
    return p;
}

Target parseTarget(const json& j) {
    Target t;
    const std::string kind = getString(j, "kind", "point");
    if (kind == "ocr") t.kind = TargetKind::Ocr;
    else if (kind == "image") t.kind = TargetKind::Image;
    else if (kind == "relative") t.kind = TargetKind::Relative;
    else t.kind = TargetKind::Point;

    t.text = getString(j, "text");
    parseMatchMode(getString(j, "match", "exact"), t.match);
    parseDirection(getString(j, "direction", "right"), t.direction);
    parseElementRole(getString(j, "element", "input"), t.role);
    t.maxDistance = getInt(j, "max_distance", t.maxDistance);
    t.templatePath = getString(j, "template");
    t.threshold = getDouble(j, "threshold", t.threshold);
    t.point.x = getInt(j, "x", 0);
    t.point.y = getInt(j, "y", 0);
    t.offsetX = getInt(j, "offset_x", 0);
    t.offsetY = getInt(j, "offset_y", 0);

    auto region = j.find("region");
    if (region != j.end() && region->is_object()) {
        t.region = parseRect(*region);
    }
    auto retry = j.find("retry");
    if (retry != j.end() && retry->is_object()) {
        t.retry = parseRetry(*retry);
    }
    return t;
}

json dumpTarget(const Target& t) {
    json j;
    switch (t.kind) {
        case TargetKind::Ocr:
            j["kind"] = "ocr";
            j["text"] = t.text;
            j["match"] = toString(t.match);
            break;
        case TargetKind::Image:
            j["kind"] = "image";
            j["template"] = t.templatePath;
            j["threshold"] = t.threshold;
            break;
        case TargetKind::Point:
            j["kind"] = "point";
            j["x"] = t.point.x;
            j["y"] = t.point.y;
            break;
        case TargetKind::Relative:
            j["kind"] = "relative";
            j["text"] = t.text;
            j["match"] = toString(t.match);
            j["direction"] = toString(t.direction);
            j["element"] = toString(t.role);
            j["max_distance"] = t.maxDistance;
            break;
    }
    if (t.offsetX != 0) j["offset_x"] = t.offsetX;
    if (t.offsetY != 0) j["offset_y"] = t.offsetY;
    if (t.region) j["region"] = dumpRect(*t.region);
    if (t.kind != TargetKind::Point) {
        j["retry"] = json{{"times", t.retry.times}, {"interval_ms", t.retry.intervalMs}};
    }
    return j;
}

Condition parseCondition(const json& j) {
    Condition c;
    const std::string kind = getString(j, "kind", "ocr_found");
    if (kind == "image_found") c.kind = Condition::Kind::ImageFound;
    else if (kind == "var_equals") c.kind = Condition::Kind::VarEquals;
    else if (kind == "var_contains") c.kind = Condition::Kind::VarContains;
    else c.kind = Condition::Kind::OcrFound;

    auto target = j.find("target");
    if (target != j.end() && target->is_object()) {
        c.target = parseTarget(*target);
    }
    // A condition's target kind is implied by the condition itself, so callers
    // may omit `kind` inside the nested target object.
    if (c.kind == Condition::Kind::OcrFound) c.target.kind = TargetKind::Ocr;
    if (c.kind == Condition::Kind::ImageFound) c.target.kind = TargetKind::Image;

    c.variable = getString(j, "variable");
    c.value = getString(j, "value");
    return c;
}

json dumpCondition(const Condition& c) {
    json j;
    switch (c.kind) {
        case Condition::Kind::OcrFound:
            j["kind"] = "ocr_found";
            j["target"] = dumpTarget(c.target);
            break;
        case Condition::Kind::ImageFound:
            j["kind"] = "image_found";
            j["target"] = dumpTarget(c.target);
            break;
        case Condition::Kind::VarEquals:
            j["kind"] = "var_equals";
            j["variable"] = c.variable;
            j["value"] = c.value;
            break;
        case Condition::Kind::VarContains:
            j["kind"] = "var_contains";
            j["variable"] = c.variable;
            j["value"] = c.value;
            break;
    }
    return j;
}

void parseStepList(const json& arr, StepList& out);

void applyStepFields(const json& j, Step& step) {
    step.enabled = getBool(j, "enabled", true);
    step.comment = getString(j, "comment");

    const std::string onFail = getString(j, "on_fail", "abort");
    if (onFail == "continue") {
        step.onFail = FailurePolicy::Continue;
    } else if (onFail.rfind("goto:", 0) == 0) {
        step.onFail = FailurePolicy::Goto;
        step.onFailGoto = onFail.substr(5);
    } else {
        step.onFail = FailurePolicy::Abort;
    }

    auto target = j.find("target");
    if (target != j.end() && target->is_object()) {
        step.target = parseTarget(*target);
    } else {
        // ocr_find / image_find carry their locator fields inline rather than
        // nested under `target`, which keeps hand-written scripts readable.
        step.target = parseTarget(j);
    }

    parseMouseButton(getString(j, "button", "left"), step.button);
    step.clickCount = getInt(j, "count", step.type == StepType::DoubleClick ? 2 : 1);

    step.text = getString(j, "text");
    step.intervalMs = getInt(j, "interval_ms", 0);
    step.keys = getString(j, "keys");
    step.waitMs = getInt(j, "ms", 0);
    step.saveToVar = getString(j, "save_to_var", "last_match");
    step.titleMatch = getString(j, "title_match");
    parseMatchMode(getString(j, "match", "contains"), step.titleMatchMode);
    step.path = getString(j, "path");
    step.launchArgs = getString(j, "args");
    step.workingDir = getString(j, "working_dir");

    auto condition = j.find("condition");
    if (condition != j.end() && condition->is_object()) {
        step.condition = parseCondition(*condition);
    }
    auto thenSteps = j.find("then_steps");
    if (thenSteps != j.end() && thenSteps->is_array()) parseStepList(*thenSteps, step.thenSteps);
    auto elseSteps = j.find("else_steps");
    if (elseSteps != j.end() && elseSteps->is_array()) parseStepList(*elseSteps, step.elseSteps);

    step.loopCount = getInt(j, "count", 1);
    auto whileCondition = j.find("while_condition");
    if (whileCondition != j.end() && whileCondition->is_object()) {
        step.whileCondition = parseCondition(*whileCondition);
    }
    auto loopSteps = j.find("steps");
    if (loopSteps != j.end() && loopSteps->is_array()) parseStepList(*loopSteps, step.loopSteps);

    step.httpMethod = getString(j, "method", "GET");
    step.url = getString(j, "url");
    step.body = getString(j, "body");
    auto headers = j.find("headers");
    if (headers != j.end() && headers->is_object()) {
        for (auto it = headers->begin(); it != headers->end(); ++it) {
            step.headers[it.key()] = it.value().is_string() ? it.value().get<std::string>()
                                                            : it.value().dump();
        }
    }
    auto saveTo = j.find("save_to_var");
    if (saveTo != j.end() && saveTo->is_string()) {
        step.saveToVar = saveTo->get<std::string>();
    }
}

Step parseStep(const json& j) {
    Step step;
    step.id = getString(j, "id");
    parseStepType(getString(j, "type", "wait"), step.type);
    applyStepFields(j, step);
    return step;
}

void parseStepList(const json& arr, StepList& out) {
    for (const auto& item : arr) {
        if (!item.is_object()) continue;
        out.push_back(parseStep(item));
    }
}

json dumpStep(const Step& step);

json dumpStepList(const StepList& steps) {
    json arr = json::array();
    for (const auto& s : steps) arr.push_back(dumpStep(s));
    return arr;
}

json dumpStep(const Step& step) {
    json j;
    j["id"] = step.id;
    j["type"] = toString(step.type);
    if (!step.enabled) j["enabled"] = false;
    if (!step.comment.empty()) j["comment"] = step.comment;

    switch (step.onFail) {
        case FailurePolicy::Abort: break;  // default, omitted
        case FailurePolicy::Continue: j["on_fail"] = "continue"; break;
        case FailurePolicy::Goto: j["on_fail"] = "goto:" + step.onFailGoto; break;
    }

    switch (step.type) {
        case StepType::Click:
        case StepType::DoubleClick:
            j["target"] = dumpTarget(step.target);
            j["button"] = toString(step.button);
            if (step.type == StepType::Click && step.clickCount != 1) j["count"] = step.clickCount;
            break;
        case StepType::TypeText:
            j["text"] = step.text;
            if (step.intervalMs > 0) j["interval_ms"] = step.intervalMs;
            break;
        case StepType::KeyPress:
            j["keys"] = step.keys;
            break;
        case StepType::Wait:
            j["ms"] = step.waitMs;
            break;
        case StepType::OcrFind: {
            j["text"] = step.target.text;
            j["match"] = toString(step.target.match);
            if (step.target.offsetX != 0) j["offset_x"] = step.target.offsetX;
            if (step.target.offsetY != 0) j["offset_y"] = step.target.offsetY;
            if (step.target.region) j["region"] = dumpRect(*step.target.region);
            j["retry"] = json{{"times", step.target.retry.times},
                              {"interval_ms", step.target.retry.intervalMs}};
            j["save_to_var"] = step.saveToVar;
            break;
        }
        case StepType::ImageFind: {
            j["template"] = step.target.templatePath;
            j["threshold"] = step.target.threshold;
            if (step.target.offsetX != 0) j["offset_x"] = step.target.offsetX;
            if (step.target.offsetY != 0) j["offset_y"] = step.target.offsetY;
            if (step.target.region) j["region"] = dumpRect(*step.target.region);
            j["retry"] = json{{"times", step.target.retry.times},
                              {"interval_ms", step.target.retry.intervalMs}};
            j["save_to_var"] = step.saveToVar;
            break;
        }
        case StepType::WindowActivate:
            j["title_match"] = step.titleMatch;
            j["match"] = toString(step.titleMatchMode);
            break;
        case StepType::Screenshot:
            j["path"] = step.path;
            if (step.target.region) j["region"] = dumpRect(*step.target.region);
            break;
        case StepType::If:
            if (step.condition) j["condition"] = dumpCondition(*step.condition);
            j["then_steps"] = dumpStepList(step.thenSteps);
            if (!step.elseSteps.empty()) j["else_steps"] = dumpStepList(step.elseSteps);
            break;
        case StepType::Loop:
            if (step.whileCondition) {
                j["while_condition"] = dumpCondition(*step.whileCondition);
            } else {
                j["count"] = step.loopCount;
            }
            j["steps"] = dumpStepList(step.loopSteps);
            break;
        case StepType::HttpRequest:
            j["method"] = step.httpMethod;
            j["url"] = step.url;
            if (!step.headers.empty()) j["headers"] = step.headers;
            if (!step.body.empty()) j["body"] = step.body;
            if (!step.saveToVar.empty()) j["save_to_var"] = step.saveToVar;
            break;
        case StepType::LaunchApp:
            j["path"] = step.path;
            if (!step.launchArgs.empty()) j["args"] = step.launchArgs;
            if (!step.workingDir.empty()) j["working_dir"] = step.workingDir;
            break;
    }

    return j;
}

}  // namespace

ParseResult parseScript(const std::string& text) {
    ParseResult result;

    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        result.error = "invalid JSON";
        return result;
    }
    if (!doc.is_object()) {
        result.error = "top level value must be an object";
        return result;
    }

    Script script;
    script.name = getString(doc, "name");
    script.version = getInt(doc, "version", 1);
    script.description = getString(doc, "description");

    auto vars = doc.find("variables");
    if (vars != doc.end() && vars->is_object()) {
        for (auto it = vars->begin(); it != vars->end(); ++it) {
            script.variables[it.key()] =
                it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
        }
    }

    auto steps = doc.find("steps");
    if (steps != doc.end() && steps->is_array()) {
        parseStepList(*steps, script.steps);
    }

    result.ok = true;
    result.script = std::move(script);
    result.issues = validate(result.script);
    return result;
}

ParseResult loadScriptFile(const std::string& path) {
    ParseResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "cannot open file: " + path;
        return result;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseScript(buffer.str());
}

std::string serializeScript(const Script& script, bool pretty) {
    json doc;
    doc["name"] = script.name;
    doc["version"] = script.version;
    if (!script.description.empty()) doc["description"] = script.description;
    if (!script.variables.empty()) doc["variables"] = script.variables;
    doc["steps"] = dumpStepList(script.steps);
    return pretty ? doc.dump(2) : doc.dump();
}

bool saveScriptFile(const Script& script, const std::string& path, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write file: " + path;
        return false;
    }
    out << serializeScript(script, true);
    if (!out) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool parseStepFromParamsJson(const std::string& id,
                             const std::string& typeName,
                             const std::string& paramsJson,
                             const std::string& comment,
                             Step& out,
                             std::string& error) {
    StepType type;
    if (!parseStepType(typeName, type)) {
        error = "unknown step type: " + typeName;
        return false;
    }

    json params = json::object();
    if (!paramsJson.empty()) {
        params = json::parse(paramsJson, nullptr, false);
        if (params.is_discarded()) {
            error = "params_json is not valid JSON";
            return false;
        }
        if (!params.is_object()) {
            error = "params_json must decode to an object";
            return false;
        }
    }

    Step step;
    step.id = id;
    step.type = type;
    applyStepFields(params, step);
    if (!comment.empty()) step.comment = comment;

    out = std::move(step);
    return true;
}

}  // namespace rpa::core
