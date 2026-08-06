#pragma once

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "rpa/core/Input.h"
#include "rpa/core/Locator.h"

namespace rpa::test {

/// Records every actuation instead of driving the real desktop, so executor
/// control flow can be asserted deterministically.
class FakeInput : public core::IInputBackend {
public:
    std::vector<std::string> calls;
    bool failNextClick = false;

    bool moveMouse(core::Point p, std::string&) override {
        calls.push_back("move(" + std::to_string(p.x) + "," + std::to_string(p.y) + ")");
        return true;
    }

    bool click(core::Point p, core::MouseButton button, int count, std::string& error) override {
        if (failNextClick) {
            failNextClick = false;
            error = "simulated click failure";
            return false;
        }
        calls.push_back("click(" + std::to_string(p.x) + "," + std::to_string(p.y) + "," +
                        core::toString(button) + "," + std::to_string(count) + ")");
        return true;
    }

    bool typeText(const std::string& utf8, int, std::string&) override {
        calls.push_back("type(" + utf8 + ")");
        return true;
    }

    bool pressKeys(const std::string& combo, std::string&) override {
        calls.push_back("keys(" + combo + ")");
        return true;
    }
};

class FakeWindow : public core::IWindowBackend {
public:
    std::vector<std::string> activated;
    bool succeed = true;

    bool activateWindow(const std::string& titleMatch, core::MatchMode, std::string& error) override {
        if (!succeed) {
            error = "window not found";
            return false;
        }
        activated.push_back(titleMatch);
        return true;
    }

    bool foregroundWindowRect(core::Rect& out, std::string&) override {
        out = core::Rect{0, 0, 1920, 1080};
        return true;
    }
};

/// Answers locate() from a scripted table keyed by the target's text or
/// template path, so tests can stage hits and misses.
class FakeLocator : public core::ITargetLocator {
public:
    std::map<std::string, core::Point> hits;
    std::vector<std::string> queries;
    std::vector<std::string> captures;
    int locateCallCount = 0;

    core::LocateResult locate(const core::Target& target) override {
        ++locateCallCount;
        const std::string key =
            target.kind == core::TargetKind::Image ? target.templatePath : target.text;
        queries.push_back(key);

        core::LocateResult result;
        auto it = hits.find(key);
        if (it == hits.end()) {
            result.error = "not found: " + key;
            return result;
        }
        result.found = true;
        result.box = core::Rect{it->second.x - 10, it->second.y - 5, 20, 10};
        result.point = core::Point{it->second.x + target.offsetX, it->second.y + target.offsetY};
        result.confidence = 0.97;
        result.matchedText = key;
        return result;
    }

    bool captureToFile(const std::string& path,
                       const std::optional<core::Rect>&,
                       std::string&) override {
        captures.push_back(path);
        return true;
    }
};

inline std::string join(const std::vector<std::string>& items, const char* sep = "|") {
    std::ostringstream out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out << sep;
        out << items[i];
    }
    return out.str();
}

}  // namespace rpa::test
