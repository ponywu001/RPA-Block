#pragma once

#include <QWidget>

#include "rpa/core/Script.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace rpa::studio {

/// Screen 1's right column: edits the selected step.
///
/// Every step type shares the common header (id / comment / enabled / on-fail)
/// and gets its own page in a stacked widget for type-specific fields.
class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setStep(const core::Step& step);
    void clearStep();
    /// Ids that `on_fail: goto` may point at.
    void setAvailableStepIds(const QStringList& ids, const QString& currentStepId);

    /// The step as currently shown in the form.
    core::Step step() const;

signals:
    /// Fires on any field edit; the host applies it to the flow.
    void stepEdited(const core::Step& step);
    /// "Pick target" was pressed; the host opens the full-screen picker.
    void targetPickRequested();

private:
    void buildCommonSection(QFormLayout* layout);
    /// Offset / region / retry, shared by the click and locate step types and so
    /// hosted outside the stacked pages.
    QWidget* buildTuningBox();
    QWidget* buildClickPage();
    QWidget* buildTypeTextPage();
    QWidget* buildKeyPressPage();
    QWidget* buildWaitPage();
    QWidget* buildLocatePage();
    QWidget* buildWindowPage();
    QWidget* buildScreenshotPage();
    QWidget* buildBranchPage();
    QWidget* buildHttpPage();

    void loadInto();
    void emitEdit();
    int pageIndexFor(core::StepType type) const;

    core::Step step_;
    bool loading_ = false;
    bool hasStep_ = false;

    // Common
    QLineEdit* idEdit_;
    QLineEdit* commentEdit_;
    QCheckBox* enabledCheck_;
    QComboBox* onFailCombo_;
    QComboBox* gotoCombo_;
    QLineEdit* typeLabel_;

    QStackedWidget* pages_;
    QWidget* tuningBox_;

    // Click / target
    QComboBox* targetKindCombo_;
    QLineEdit* targetTextEdit_;
    QComboBox* targetMatchCombo_;
    QLineEdit* targetTemplateEdit_;
    QDoubleSpinBox* targetThresholdSpin_;
    QSpinBox* targetPointXSpin_;
    QSpinBox* targetPointYSpin_;
    QComboBox* buttonCombo_;
    QSpinBox* clickCountSpin_;
    QPushButton* pickTargetButton_;

    // Shared locator extras (offset / region / retry) reused by click + locate
    QSpinBox* offsetXSpin_;
    QSpinBox* offsetYSpin_;
    QCheckBox* regionCheck_;
    QSpinBox* regionXSpin_;
    QSpinBox* regionYSpin_;
    QSpinBox* regionWSpin_;
    QSpinBox* regionHSpin_;
    QSpinBox* retryTimesSpin_;
    QSpinBox* retryIntervalSpin_;

    // Locate page duplicates of the target fields
    QLineEdit* locateTextEdit_;
    QComboBox* locateMatchCombo_;
    QLineEdit* locateTemplateEdit_;
    QDoubleSpinBox* locateThresholdSpin_;
    QLineEdit* saveToVarEdit_;
    QPushButton* pickLocateButton_;

    // Type text
    QPlainTextEdit* textEdit_;
    QSpinBox* intervalSpin_;

    // Key press
    QLineEdit* keysEdit_;

    // Wait
    QSpinBox* waitSpin_;

    // Window activate
    QLineEdit* titleMatchEdit_;
    QComboBox* titleMatchCombo_;

    // Screenshot
    QLineEdit* screenshotPathEdit_;

    // Branch (if / loop) — nested bodies are shown read-only as JSON
    QPlainTextEdit* branchJsonEdit_;
    QSpinBox* loopCountSpin_;

    // HTTP
    QComboBox* httpMethodCombo_;
    QLineEdit* urlEdit_;
    QPlainTextEdit* headersEdit_;
    QPlainTextEdit* bodyEdit_;
    QLineEdit* httpSaveVarEdit_;
};

}  // namespace rpa::studio
