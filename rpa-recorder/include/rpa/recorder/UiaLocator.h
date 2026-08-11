#pragma once

#include <functional>
#include <string>

#include "rpa/core/Locator.h"

namespace rpa::recorder {

/// Locates `TargetKind::Relative` targets through UI Automation.
///
/// Sits in front of the vision locator in a CompositeLocator: when the target
/// application exposes its controls, this answers exactly and instantly, with
/// no screenshot, no OCR, and nothing that a font size or a scaling factor can
/// shift. It declines everything else -- OCR text, image templates, raw points
/// stay with the vision backend.
///
/// Declining is not the same as failing. Returning "this backend does not
/// handle that kind of target" lets the composite move on quietly, while a real
/// miss carries a diagnosis worth showing the user.
class UiaLocator : public core::ITargetLocator {
public:
    /// Reports how a match was resolved -- by name or by geometry -- so the run
    /// log can show which, and a step that quietly changed strategy between
    /// runs is visible before it breaks.
    using StrategyReporter = std::function<void(const std::string& note)>;

    void setStrategyReporter(StrategyReporter reporter) { report_ = std::move(reporter); }

    core::LocateResult locate(const core::Target& target) override;

    /// Not this backend's job: automation has no view of pixels.
    bool captureToFile(const std::string& path,
                       const std::optional<core::Rect>& region,
                       std::string& error) override;

private:
    StrategyReporter report_;
};

}  // namespace rpa::recorder
