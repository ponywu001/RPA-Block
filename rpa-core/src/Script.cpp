#include "rpa/core/Script.h"

#include <algorithm>
#include <set>

namespace rpa::core {

std::string toString(MatchMode mode) {
    switch (mode) {
        case MatchMode::Exact: return "exact";
        case MatchMode::Contains: return "contains";
        case MatchMode::Regex: return "regex";
    }
    return "exact";
}

bool parseMatchMode(const std::string& text, MatchMode& out) {
    if (text == "exact") { out = MatchMode::Exact; return true; }
    if (text == "contains") { out = MatchMode::Contains; return true; }
    if (text == "regex") { out = MatchMode::Regex; return true; }
    return false;
}

std::string toString(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "left";
        case MouseButton::Right: return "right";
        case MouseButton::Middle: return "middle";
    }
    return "left";
}

bool parseMouseButton(const std::string& text, MouseButton& out) {
    if (text == "left") { out = MouseButton::Left; return true; }
    if (text == "right") { out = MouseButton::Right; return true; }
    if (text == "middle") { out = MouseButton::Middle; return true; }
    return false;
}

std::string toString(StepType type) {
    switch (type) {
        case StepType::Click: return "click";
        case StepType::DoubleClick: return "double_click";
        case StepType::TypeText: return "type_text";
        case StepType::KeyPress: return "key_press";
        case StepType::Wait: return "wait";
        case StepType::OcrFind: return "ocr_find";
        case StepType::ImageFind: return "image_find";
        case StepType::WindowActivate: return "window_activate";
        case StepType::Screenshot: return "screenshot";
        case StepType::If: return "if";
        case StepType::Loop: return "loop";
        case StepType::HttpRequest: return "http_request";
    }
    return "wait";
}

bool parseStepType(const std::string& text, StepType& out) {
    static const std::map<std::string, StepType> kMap = {
        {"click", StepType::Click},
        {"double_click", StepType::DoubleClick},
        {"type_text", StepType::TypeText},
        {"key_press", StepType::KeyPress},
        {"wait", StepType::Wait},
        {"ocr_find", StepType::OcrFind},
        {"image_find", StepType::ImageFind},
        {"window_activate", StepType::WindowActivate},
        {"screenshot", StepType::Screenshot},
        {"if", StepType::If},
        {"loop", StepType::Loop},
        {"http_request", StepType::HttpRequest},
    };
    auto it = kMap.find(text);
    if (it == kMap.end()) return false;
    out = it->second;
    return true;
}

void forEachStep(const StepList& steps, const std::function<void(const Step&)>& fn) {
    for (const auto& step : steps) {
        fn(step);
        if (!step.thenSteps.empty()) forEachStep(step.thenSteps, fn);
        if (!step.elseSteps.empty()) forEachStep(step.elseSteps, fn);
        if (!step.loopSteps.empty()) forEachStep(step.loopSteps, fn);
    }
}

namespace {

void validateTarget(const Step& step,
                    const Target& target,
                    const char* what,
                    std::vector<ValidationIssue>& issues) {
    switch (target.kind) {
        case TargetKind::Ocr:
            if (target.text.empty()) {
                issues.push_back({step.id, std::string(what) + ": ocr target needs a non-empty text"});
            }
            break;
        case TargetKind::Image:
            if (target.templatePath.empty()) {
                issues.push_back({step.id, std::string(what) + ": image target needs a template path"});
            }
            if (target.threshold <= 0.0 || target.threshold > 1.0) {
                issues.push_back({step.id, std::string(what) + ": threshold must be within (0, 1]"});
            }
            break;
        case TargetKind::Point:
            break;
    }
    if (target.retry.times < 0) {
        issues.push_back({step.id, std::string(what) + ": retry.times cannot be negative"});
    }
    if (target.retry.intervalMs < 0) {
        issues.push_back({step.id, std::string(what) + ": retry.interval_ms cannot be negative"});
    }
    if (target.region && target.region->empty()) {
        issues.push_back({step.id, std::string(what) + ": region has zero width or height"});
    }
}

void collectIds(const StepList& steps, std::set<std::string>& ids, std::vector<ValidationIssue>& issues) {
    for (const auto& step : steps) {
        if (step.id.empty()) {
            issues.push_back({"", "step of type '" + toString(step.type) + "' has an empty id"});
        } else if (!ids.insert(step.id).second) {
            issues.push_back({step.id, "duplicate step id"});
        }
        collectIds(step.thenSteps, ids, issues);
        collectIds(step.elseSteps, ids, issues);
        collectIds(step.loopSteps, ids, issues);
    }
}

void validateSteps(const StepList& steps,
                   const std::set<std::string>& allIds,
                   std::vector<ValidationIssue>& issues) {
    for (const auto& step : steps) {
        if (step.onFail == FailurePolicy::Goto) {
            if (step.onFailGoto.empty()) {
                issues.push_back({step.id, "on_fail is 'goto' but no target step id was given"});
            } else if (allIds.find(step.onFailGoto) == allIds.end()) {
                issues.push_back({step.id, "on_fail goto target '" + step.onFailGoto + "' does not exist"});
            }
        }

        switch (step.type) {
            case StepType::Click:
            case StepType::DoubleClick:
                validateTarget(step, step.target, "target", issues);
                if (step.clickCount < 1 || step.clickCount > 3) {
                    issues.push_back({step.id, "click count must be between 1 and 3"});
                }
                break;
            case StepType::TypeText:
                if (step.text.empty()) {
                    issues.push_back({step.id, "type_text has no text"});
                }
                if (step.intervalMs < 0) {
                    issues.push_back({step.id, "interval_ms cannot be negative"});
                }
                break;
            case StepType::KeyPress:
                if (step.keys.empty()) {
                    issues.push_back({step.id, "key_press has no keys"});
                }
                break;
            case StepType::Wait:
                if (step.waitMs < 0) {
                    issues.push_back({step.id, "wait ms cannot be negative"});
                }
                break;
            case StepType::OcrFind: {
                Target t = step.target;
                t.kind = TargetKind::Ocr;
                validateTarget(step, t, "ocr_find", issues);
                break;
            }
            case StepType::ImageFind: {
                Target t = step.target;
                t.kind = TargetKind::Image;
                validateTarget(step, t, "image_find", issues);
                break;
            }
            case StepType::WindowActivate:
                if (step.titleMatch.empty()) {
                    issues.push_back({step.id, "window_activate needs a title_match"});
                }
                break;
            case StepType::Screenshot:
                if (step.path.empty()) {
                    issues.push_back({step.id, "screenshot needs a path"});
                }
                break;
            case StepType::If:
                if (!step.condition) {
                    issues.push_back({step.id, "if step has no condition"});
                }
                if (step.thenSteps.empty() && step.elseSteps.empty()) {
                    issues.push_back({step.id, "if step has neither then_steps nor else_steps"});
                }
                break;
            case StepType::Loop:
                if (!step.whileCondition && step.loopCount < 1) {
                    issues.push_back({step.id, "loop needs either count >= 1 or a while_condition"});
                }
                if (step.loopSteps.empty()) {
                    issues.push_back({step.id, "loop has no steps"});
                }
                break;
            case StepType::HttpRequest:
                if (step.url.empty()) {
                    issues.push_back({step.id, "http_request needs a url"});
                } else if (step.url.rfind("http://", 0) != 0 && step.url.rfind("https://", 0) != 0) {
                    issues.push_back({step.id, "http_request url must start with http:// or https://"});
                }
                break;
        }

        validateSteps(step.thenSteps, allIds, issues);
        validateSteps(step.elseSteps, allIds, issues);
        validateSteps(step.loopSteps, allIds, issues);
    }
}

}  // namespace

std::vector<ValidationIssue> validate(const Script& script) {
    std::vector<ValidationIssue> issues;

    if (script.name.empty()) {
        issues.push_back({"", "script has no name"});
    }
    if (script.version != 1) {
        issues.push_back({"", "unsupported script version (expected 1)"});
    }
    if (script.steps.empty()) {
        issues.push_back({"", "script has no steps"});
    }

    std::set<std::string> ids;
    collectIds(script.steps, ids, issues);
    validateSteps(script.steps, ids, issues);

    return issues;
}

}  // namespace rpa::core
