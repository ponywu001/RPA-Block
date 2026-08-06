#pragma once

#include <cstdint>
#include <string>

namespace rpa::core {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool empty() const { return width <= 0 || height <= 0; }
    Point center() const { return Point{x + width / 2, y + height / 2}; }
};

enum class MatchMode {
    Exact,
    Contains,
    Regex,
};

enum class MouseButton {
    Left,
    Right,
    Middle,
};

enum class FailurePolicy {
    Abort,
    Continue,
    Goto,
};

std::string toString(MatchMode mode);
bool parseMatchMode(const std::string& text, MatchMode& out);

std::string toString(MouseButton button);
bool parseMouseButton(const std::string& text, MouseButton& out);

}  // namespace rpa::core
