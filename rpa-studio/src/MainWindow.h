#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <memory>
#include <optional>

#include "AppSettings.h"
#include "ExecutionController.h"
#include "Theme.h"
#include "rpa/ai/AgentClient.h"
#include "rpa/ai/DraftRepair.h"
#include "rpa/core/Input.h"
#include "rpa/core/Script.h"
#include "rpa/recorder/Recorder.h"
#include "rpa/recorder/UiaLocator.h"
#include "rpa/server/ApiServer.h"
#include "rpa/server/RunStore.h"
#include "rpa/server/ScriptRepository.h"
#include "rpa/vision/VisionLocator.h"

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QDockWidget;
class QTreeWidget;

namespace rpa::studio {

class ApiPanelDialog;
class BlockPalette;
class ChatDock;
class FlowVariablesDialog;
class FlowCanvas;
class PropertyPanel;
class RecordingOverlayBar;
class TargetPickerOverlay;
class TunnelController;

/// Screen 1, and the host that owns every subsystem.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load a flow from `path`, reporting any problem to the user. Used for the
    /// command-line argument so a .rpa.json can be opened directly.
    void openFlowFile(const QString& path);

    /// Open the screen target picker straight away, adding a block to attach the
    /// result to if the flow is empty. Backs `--pick-target`, which exercises
    /// capture, OCR and template saving without authoring a flow first.
    void showTargetPicker();

    /// Open the flow's input editor. Backs `--edit-variables`.
    void showFlowVariables();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // File
    void newFlow();
    void openFlow();
    bool saveFlow();
    bool saveFlowAs();

    // Run control
    void runFlow();
    void pauseOrResumeFlow();
    void stopFlow();
    void stepOverFlow();

    // Recording
    void toggleRecording();

    // Panels
    void editFlowVariables();
    void showApiPanel();
    void showSettings();

private:
    /// Switch theme, persist the choice, and repaint the widgets that draw
    /// themselves. Kept in one place so the menu, the settings page and startup
    /// cannot drift apart on what "the current theme" means.
    void applyThemeMode(ThemeMode mode);
    /// Put every panel back where it started. The only way out of a layout
    /// with panels closed or dragged somewhere unusable.
    void restoreDefaultLayout();
    /// Reinstate the saved window geometry and dock arrangement, ignoring a
    /// saved position that would land off-screen.
    void restoreLayout();

    void buildUi();
    void buildToolbar();
    void buildMenus();
    void buildDocks();
    void wireSignals();

    void loadSettings();
    void applySettings();
    void startApiServerIfConfigured();
    void startTunnelIfConfigured();
    void loadOcrModels();

    void setScript(const core::Script& script, const QString& path);
    bool confirmDiscardChanges();
    void markDirty(bool dirty = true);
    void updateWindowTitle();
    void updateActionStates();
    void refreshPropertyPanel();
    void refreshProjectTree();

    void appendLog(int level, const QString& stepId, const QString& message);
    void addStepOfType(core::StepType type);
    void openTargetPicker();

    void sendToAssistant(const QString& text, bool attachScreenshot);
    void previewAssistantDraft();
    void applyAssistantDraft();
    void handOffRecordingToAssistant(const recorder::RecordingSession& session);
    void askAssistantAboutFailure();
    /// Start a request and arm the repair loop that guards its reply.
    void dispatchToAssistant(std::vector<ai::ChatMessage> history);

    void publishCurrentFlow();

    QString projectSubdirectory(const QString& name) const;

    // --- Subsystems -------------------------------------------------------
    AppSettings settings_;

    std::unique_ptr<core::IInputBackend> input_;
    std::unique_ptr<core::IWindowBackend> windowBackend_;
    vision::VisionLocator locator_;
    core::NullTargetLocator fallbackLocator_;
    /// Asked before the vision locator, and only ever answers relative targets.
    /// A form that exposes its controls is resolved exactly; one that does not
    /// falls through to pixels without the flow having to say which it is.
    recorder::UiaLocator uiaLocator_;
    core::CompositeLocator resolvedLocator_;

    ExecutionController execution_;
    recorder::Recorder recorder_;
    ai::AgentClient* agent_;

    server::ScriptRepository repository_;
    server::RunStore runStore_;
    std::unique_ptr<server::ApiServer> api_;
    /// Owned here rather than by the API dialog, so closing that window does not
    /// silently take a public tunnel down with it -- or, worse, leave one open
    /// with no way back to the button that closes it.
    TunnelController* tunnel_ = nullptr;

    // --- UI ---------------------------------------------------------------
    FlowCanvas* editor_;
    PropertyPanel* properties_;
    ChatDock* chat_;
    BlockPalette* palette_;
    QTreeWidget* projectTree_;
    QPlainTextEdit* log_;
    QLabel* apiStatusLabel_;

    QAction* runAction_;
    QAction* pauseAction_;
    QAction* stopAction_;
    QAction* stepAction_;
    QAction* recordAction_;
    QAction* saveAction_;

    TargetPickerOverlay* picker_ = nullptr;
    RecordingOverlayBar* recordingBar_ = nullptr;
    QTimer* recordingTimer_ = nullptr;
    QElapsedTimer recordingClock_;
    ApiPanelDialog* apiPanel_ = nullptr;

    /// Every dock, so the 外觀 menu can offer each one back.
    QList<QDockWidget*> docks_;
    /// The layout as built, captured before the user can rearrange it.
    QByteArray defaultLayout_;

    QString currentPath_;
    bool dirty_ = false;
    /// Values the last manual run used, so re-running with one field changed
    /// does not mean retyping the rest. Session-only: never written to the flow.
    std::map<std::string, std::string> lastRunVariables_;
    /// Set once the user says the values are settled.
    bool skipVariablePrompt_ = false;

    /// Set while an edit is travelling from the property panel into the flow.
    /// Without it the round trip (panel -> flow -> scriptModified ->
    /// refreshPropertyPanel -> setStep -> setPlainText) resets the caret on
    /// every keystroke, which makes the multi-line fields impossible to type in.
    bool applyingPanelEdit_ = false;
    /// Set while a recording is being handed to the assistant, so the reply is
    /// routed to the flow rather than treated as a plain chat answer.
    bool awaitingRecordingDraft_ = false;

    /// Guards the in-flight request. A draft whose steps the IR rejects is sent
    /// back for another pass instead of reaching the user, who has no way to
    /// repair a malformed step by hand.
    std::optional<ai::DraftRepairSession> repair_;

    /// What the last failed run was running, kept for the "ask the assistant"
    /// button. The script is a snapshot rather than a live read of the editor:
    /// an API-triggered run can fail while the user is editing something else.
    struct FailureContext {
        core::Script script;
        std::string failedStepId;
        std::string error;
        QString screenshotPath;
    };
    std::optional<FailureContext> lastFailure_;
    QPushButton* askAiButton_ = nullptr;
};

}  // namespace rpa::studio
