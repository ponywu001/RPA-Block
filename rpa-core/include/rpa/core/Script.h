#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rpa/core/Types.h"

namespace rpa::core {

enum class StepType {
    Click,
    DoubleClick,
    TypeText,
    KeyPress,
    Wait,
    OcrFind,
    ImageFind,
    WindowActivate,
    Screenshot,
    If,
    Loop,
    HttpRequest,
    LaunchApp,
};

/// One past the last `StepType`. Anything that has to cover every step kind --
/// the AI schema, the palette, the round-trip tests -- iterates against this so
/// adding a type surfaces as a failure rather than a silent gap.
constexpr int kStepTypeCount = static_cast<int>(StepType::LaunchApp) + 1;

std::string toString(StepType type);
bool parseStepType(const std::string& text, StepType& out);

enum class TargetKind {
    Ocr,
    Image,
    Point,
    /// "The field next to this label." Resolved against whatever the target
    /// application exposes -- see `Direction` and `ElementRole`.
    Relative,
};

/// Where the wanted control sits in relation to its label. Forms put labels
/// above their fields as often as beside them, and the same form does both.
enum class Direction {
    Right,
    Left,
    Above,
    Below,
};

/// What kind of control to accept. Deliberately coarse: a user pointing at a
/// form thinks "the box I type into", not "UIA_EditControlTypeId", and the
/// vision fallback cannot tell the finer kinds apart anyway.
enum class ElementRole {
    Any,
    Input,
    Button,
    Checkbox,
};

std::string toString(Direction direction);
bool parseDirection(const std::string& text, Direction& out);
std::string toString(ElementRole role);
bool parseElementRole(const std::string& text, ElementRole& out);

struct RetryPolicy {
    int times = 3;
    int intervalMs = 1000;
};

/// How a step locates its subject on screen. `Point` is only produced by the
/// recorder; the AI pass is expected to rewrite it into an Ocr/Image anchor.
struct Target {
    TargetKind kind = TargetKind::Point;

    // kind == Ocr
    std::string text;
    /// Contains, not Exact. OCR routinely sweeps a neighbouring glyph into the
    /// line -- a magnifier icon reads as "Q", so Windows' search box comes back
    /// as "Q 搜尋" -- and an exact match then fails on text that is plainly on
    /// screen. Exact remains available for when a substring would be ambiguous.
    MatchMode match = MatchMode::Contains;

    // kind == Image
    std::string templatePath;
    double threshold = 0.85;

    // kind == Point
    Point point;

    // kind == Relative -- `text` and `match` above name the anchor label.
    Direction direction = Direction::Right;
    ElementRole role = ElementRole::Input;
    /// How far from the anchor to keep looking, in pixels. Bounds the search so
    /// a missing field cannot match something on the far side of the window.
    int maxDistance = 400;

    // Shared
    std::optional<Rect> region;
    int offsetX = 0;
    int offsetY = 0;
    RetryPolicy retry;
};

struct Step;
using StepList = std::vector<Step>;

/// Condition for an `If` step. Evaluated against the current variable scope and
/// the result of the most recent locate attempt.
struct Condition {
    enum class Kind {
        OcrFound,
        ImageFound,
        VarEquals,
        VarContains,
    };

    Kind kind = Kind::OcrFound;
    Target target;        // OcrFound / ImageFound
    std::string variable; // VarEquals / VarContains
    std::string value;    // VarEquals / VarContains
};

struct Step {
    std::string id;
    StepType type = StepType::Wait;
    bool enabled = true;
    std::string comment;

    FailurePolicy onFail = FailurePolicy::Abort;
    std::string onFailGoto;  // populated when onFail == Goto

    // Click / DoubleClick
    Target target;
    MouseButton button = MouseButton::Left;
    int clickCount = 1;

    // TypeText
    std::string text;
    int intervalMs = 0;

    // KeyPress
    std::string keys;

    // Wait
    int waitMs = 0;

    // OcrFind / ImageFind write their hit into this variable (default last_match).
    std::string saveToVar;

    // WindowActivate
    std::string titleMatch;
    MatchMode titleMatchMode = MatchMode::Contains;

    // Screenshot / LaunchApp
    std::string path;

    // LaunchApp
    std::string launchArgs;
    std::string workingDir;

    // If
    std::optional<Condition> condition;
    StepList thenSteps;
    StepList elseSteps;

    // Loop
    int loopCount = 1;
    std::optional<Condition> whileCondition;
    StepList loopSteps;

    // HttpRequest
    std::string httpMethod = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Script {
    std::string name;
    int version = 1;
    std::string description;
    std::map<std::string, std::string> variables;
    StepList steps;
};

struct ValidationIssue {
    std::string stepId;   // empty when the issue is script-level
    std::string message;
};

/// Structural validation of a parsed script: unique ids, required fields per
/// step type, resolvable `goto` targets, sane numeric ranges.
std::vector<ValidationIssue> validate(const Script& script);

/// Depth-first walk over every step including nested if/loop bodies.
void forEachStep(const StepList& steps, const std::function<void(const Step&)>& fn);

}  // namespace rpa::core
