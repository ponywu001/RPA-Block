#pragma once

#include <QDialog>

#include "AppSettings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace rpa::ai {
class AgentClient;
}

namespace rpa::studio {

/// Screen 5: AI gateway, vision engine, recorder, and shortcut settings.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(AppSettings* settings, ai::AgentClient* agent, QWidget* parent = nullptr);

signals:
    /// Emitted on accept, after the edits have been written back to `settings`.
    void applied();

private:
    QWidget* buildAiTab();
    QWidget* buildAppearanceTab();
    QWidget* buildVisionTab();
    QWidget* buildRecorderTab();
    QWidget* buildShortcutTab();

    void load();
    /// Commit the form into the shared settings object.
    void store();
    /// Read the form into an arbitrary settings object. `store()` is this
    /// applied to `settings_`; the test button uses a throwaway copy so that
    /// pressing Test and then Cancel does not leave the edits committed.
    void storeInto(AppSettings* target) const;
    void testConnection();
    void browseForOcrModels();
    void browseForProjectDirectory();

    AppSettings* settings_;
    ai::AgentClient* agent_;

    // AI
    QLineEdit* gatewayEdit_;
    QLineEdit* assistantIdEdit_;
    QRadioButton* apiKeyRadio_;
    QRadioButton* jwtRadio_;
    QLineEdit* secretEdit_;
    QLineEdit* signInUrlEdit_;
    QComboBox* themeCombo_;
    QComboBox* providerCombo_;
    QComboBox* modelCombo_;
    QSpinBox* timeoutSpin_;
    QPushButton* testButton_;
    QLabel* testResultLabel_;

    // Vision
    QLineEdit* ocrDirectoryEdit_;
    QComboBox* ocrLanguageCombo_;
    QDoubleSpinBox* thresholdSpin_;
    QCheckBox* loadOcrCheck_;

    // Recorder
    QSpinBox* clickRadiusSpin_;
    QCheckBox* elementInfoCheck_;
    QLineEdit* projectDirectoryEdit_;

    // Shortcuts
    QLineEdit* recordShortcutEdit_;
    QLineEdit* abortShortcutEdit_;
};

}  // namespace rpa::studio
