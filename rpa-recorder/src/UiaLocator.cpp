#include "rpa/recorder/UiaLocator.h"

#include "rpa/recorder/UiaInspector.h"

namespace rpa::recorder {

namespace {

const char* strategyName(UiaMatchStrategy strategy) {
    switch (strategy) {
        case UiaMatchStrategy::ByName: return "控制項自己的名稱";
        case UiaMatchStrategy::ByGeometry: return "標籤的相對位置";
        case UiaMatchStrategy::None: break;
    }
    return "";
}

}  // namespace

core::LocateResult UiaLocator::locate(const core::Target& target) {
    core::LocateResult result;

    if (target.kind != core::TargetKind::Relative) {
        // Silently not ours. The composite treats an empty error as "nothing to
        // report" and moves to the next backend without adding noise to a
        // failure message that is really about OCR or templates.
        return result;
    }

    // Each locate() may run on a different executor thread, and a UI Automation
    // object must not cross apartments.
    initializeUiaForThread();

    const UiaMatch match = findRelativeElement(target.text, target.match, target.direction,
                                               target.role, target.maxDistance);

    if (!match.found) {
        result.error = match.diagnosis.empty() ? "UI Automation could not resolve that target"
                                               : match.diagnosis;
        return result;
    }

    result.found = true;
    result.box = match.bounds;
    result.point = core::Point{match.bounds.x + match.bounds.width / 2 + target.offsetX,
                               match.bounds.y + match.bounds.height / 2 + target.offsetY};
    // No probability involved: automation either handed us the control or it
    // did not, so reporting anything less than certainty would be a fiction.
    result.confidence = 1.0;
    result.matchedText = match.name.empty() ? match.controlType : match.name;

    if (report_) {
        report_("以 " + std::string(strategyName(match.strategy)) + " 找到「" + target.text +
                "」" + core::toString(target.direction) + "方的 " + match.controlType);
    }
    return result;
}

bool UiaLocator::captureToFile(const std::string&,
                               const std::optional<core::Rect>&,
                               std::string& error) {
    error = "the UI Automation backend does not capture the screen";
    return false;
}

}  // namespace rpa::recorder
