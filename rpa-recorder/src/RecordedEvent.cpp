#include "rpa/recorder/RecordedEvent.h"

#include <sstream>

#include <nlohmann/json.hpp>

namespace rpa::recorder {

using json = nlohmann::json;

namespace {

json dumpElement(const ElementInfo& element) {
    json j;
    if (!element.name.empty()) j["name"] = element.name;
    if (!element.controlType.empty()) j["control_type"] = element.controlType;
    if (!element.automationId.empty()) j["automation_id"] = element.automationId;
    if (!element.className.empty()) j["class_name"] = element.className;
    if (!element.windowTitle.empty()) j["window_title"] = element.windowTitle;
    if (!element.processName.empty()) j["process"] = element.processName;
    if (!element.bounds.empty()) {
        j["bounds"] = json{{"x", element.bounds.x},
                           {"y", element.bounds.y},
                           {"width", element.bounds.width},
                           {"height", element.bounds.height}};
    }
    return j;
}

}  // namespace

std::string toJson(const RecordingSession& session, bool pretty) {
    json doc;
    doc["asset_directory"] = session.assetDirectory;
    doc["desktop"] = json{{"x", session.desktopBounds.x},
                          {"y", session.desktopBounds.y},
                          {"width", session.desktopBounds.width},
                          {"height", session.desktopBounds.height}};

    json events = json::array();
    for (const auto& event : session.events) {
        json e;
        e["type"] = toString(event.type);
        e["t_ms"] = event.timestampMs;

        switch (event.type) {
            case RecordedEventType::MouseClick:
            case RecordedEventType::MouseDoubleClick:
                e["x"] = event.position.x;
                e["y"] = event.position.y;
                e["button"] = core::toString(event.button);
                break;
            case RecordedEventType::TextInput:
                e["text"] = event.text;
                break;
            case RecordedEventType::KeyCombo:
                e["keys"] = event.keys;
                break;
            case RecordedEventType::WindowChange:
                break;
        }

        const json element = dumpElement(event.element);
        if (!element.empty()) e["element"] = element;
        if (!event.screenshotPath.empty()) e["screenshot"] = event.screenshotPath;
        if (!event.fullScreenshotPath.empty()) e["full_screenshot"] = event.fullScreenshotPath;

        events.push_back(std::move(e));
    }
    doc["events"] = std::move(events);

    return pretty ? doc.dump(2) : doc.dump();
}

std::string toSummaryText(const RecordingSession& session) {
    std::ostringstream out;
    out << "Recorded " << session.events.size() << " events.\n";

    int index = 1;
    for (const auto& event : session.events) {
        out << index++ << ". [" << event.timestampMs << "ms] ";

        switch (event.type) {
            case RecordedEventType::MouseClick:
            case RecordedEventType::MouseDoubleClick:
                out << (event.type == RecordedEventType::MouseDoubleClick ? "double-click"
                                                                          : "click")
                    << " " << core::toString(event.button) << " at (" << event.position.x << ", "
                    << event.position.y << ")";
                break;
            case RecordedEventType::TextInput:
                out << "type \"" << event.text << "\"";
                break;
            case RecordedEventType::KeyCombo:
                out << "press " << event.keys;
                break;
            case RecordedEventType::WindowChange:
                out << "window changed";
                break;
        }

        const ElementInfo& element = event.element;
        if (!element.controlType.empty() || !element.name.empty()) {
            out << "  -> element: " << element.controlType;
            if (!element.name.empty()) out << " \"" << element.name << "\"";
        }
        if (!element.windowTitle.empty()) out << "  window: \"" << element.windowTitle << "\"";
        if (!element.processName.empty()) out << " (" << element.processName << ")";
        if (!event.screenshotPath.empty()) out << "  screenshot: " << event.screenshotPath;

        out << "\n";
    }

    return out.str();
}

}  // namespace rpa::recorder
