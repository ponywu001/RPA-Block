#include "MainWindow.h"

#include <QActionGroup>
#include <QInputDialog>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>

#include "ApiPanelDialog.h"
#include "BlockPalette.h"
#include "BlockStyle.h"
#include "ChatDock.h"
#include "FlowCanvas.h"
#include "FlowVariablesDialog.h"
#include "PropertyPanel.h"
#include "RecordingDialogs.h"
#include "SettingsDialog.h"
#include "TargetPickerOverlay.h"
#include "rpa/ai/PromptBuilder.h"
#include "rpa/core/ScriptIO.h"
#include "rpa/vision/ScreenCapture.h"

namespace rpa::studio {

namespace {

/// How long to let the desktop recomposite after hiding the editor, before the
/// target picker freezes the screen. Long enough that the editor is really gone
/// from the capture, short enough not to feel like a stall.
constexpr int kUncoverDelayMs = 200;

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    agent_ = new ai::AgentClient(this);

#ifdef _WIN32
    input_ = core::makeWin32InputBackend();
    windowBackend_ = core::makeWin32WindowBackend();
#endif

    buildUi();
    loadSettings();
    wireSignals();

    execution_.setBackends(input_.get(), windowBackend_.get(), &locator_);

    api_ = std::make_unique<server::ApiServer>(&repository_, &runStore_);
    api_->setRunHandler([this](const server::RunRequest& request, std::string& reason) {
        // Called on the HTTP thread. The controller's own busy latch is what
        // makes this safe; nothing here touches widgets.
        QString why;
        const bool accepted =
            execution_.start(request.script, request.variables,
                             QString::fromStdString(request.runId), QStringLiteral("api"), why);
        if (!accepted) reason = why.toStdString();
        return accepted;
    });

    restoreLayout();

    newFlow();
    applySettings();
    startApiServerIfConfigured();
    if (settings_.loadOcrOnStartup) loadOcrModels();

}

MainWindow::~MainWindow() {
    recorder_.stop();
    if (api_) api_->stop();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("RPA-Block"));
    resize(1500, 940);

    // The canvas grows with the flow, so it lives inside a scroll area rather
    // than being clipped by the window.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    editor_ = new FlowCanvas(scroll);
    scroll->setWidget(editor_);
    // QScrollArea does not pass its widget's minimum width up -- by design, so
    // it can be small and scroll. That left the dock layout free to squeeze the
    // canvas until a single block no longer fitted. Say it here instead, and the
    // docks give way first while staying freely resizable by hand.
    scroll->setMinimumWidth(editor_->minimumWidth() + 24);
    setCentralWidget(scroll);

    buildToolbar();
    // Docks before menus: the 外觀 menu lists one entry per dock, so they have to
    // exist first. The toolbar stays first because the menus reuse its actions.
    buildDocks();
    buildMenus();

    apiStatusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(apiStatusLabel_);
    statusBar()->showMessage(QStringLiteral("就緒"));
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar(QStringLiteral("執行"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    runAction_ = toolbar->addAction(QStringLiteral("▶  執行"), this, &MainWindow::runFlow);
    pauseAction_ =
        toolbar->addAction(QStringLiteral("⏸  暫停"), this, &MainWindow::pauseOrResumeFlow);
    stopAction_ = toolbar->addAction(QStringLiteral("⏹  停止"), this, &MainWindow::stopFlow);
    stepAction_ = toolbar->addAction(QStringLiteral("單步"), this, &MainWindow::stepOverFlow);

    toolbar->addSeparator();
    recordAction_ =
        toolbar->addAction(QStringLiteral("⏺  錄製"), this, &MainWindow::toggleRecording);

    toolbar->addSeparator();
    saveAction_ = toolbar->addAction(QStringLiteral("儲存"), this, [this] { saveFlow(); });
    toolbar->addAction(QStringLiteral("刪除積木"), this, [this] { editor_->removeSelected(); });
    toolbar->addAction(QStringLiteral("變數"), this, &MainWindow::editFlowVariables);

    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("API 伺服器"), this, &MainWindow::showApiPanel);
    toolbar->addAction(QStringLiteral("設定"), this, &MainWindow::showSettings);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(QStringLiteral("檔案(&F)"));
    file->addAction(QStringLiteral("新增流程"), QKeySequence::New, this, &MainWindow::newFlow);
    file->addAction(QStringLiteral("開啟…"), QKeySequence::Open, this, &MainWindow::openFlow);
    file->addAction(QStringLiteral("儲存"), QKeySequence::Save, this, [this] { saveFlow(); });
    file->addAction(QStringLiteral("另存為…"), QKeySequence::SaveAs, this,
                    [this] { saveFlowAs(); });
    file->addSeparator();
    file->addAction(QStringLiteral("發佈到 API"), this, &MainWindow::publishCurrentFlow);
    file->addSeparator();
    file->addAction(QStringLiteral("結束"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("編輯(&E)"));
    edit->addAction(QStringLiteral("刪除選取的積木"), QKeySequence::Delete, this,
                    [this] { editor_->removeSelected(); });
    edit->addAction(QStringLiteral("停用／啟用積木"), QKeySequence(Qt::CTRL | Qt::Key_E), this,
                    [this] { editor_->toggleSelectedEnabled(); });
    edit->addSeparator();
    edit->addAction(QStringLiteral("流程變數…"), this, &MainWindow::editFlowVariables);

    auto* run = menuBar()->addMenu(QStringLiteral("執行(&R)"));
    run->addAction(runAction_);
    run->addAction(pauseAction_);
    run->addAction(stopAction_);
    run->addAction(stepAction_);

    auto* view = menuBar()->addMenu(QStringLiteral("外觀(&V)"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    struct ThemeChoice {
        const char* label;
        ThemeMode mode;
    };
    for (const ThemeChoice& choice : {ThemeChoice{"跟隨系統", ThemeMode::System},
                                      ThemeChoice{"亮色", ThemeMode::Light},
                                      ThemeChoice{"暗色", ThemeMode::Dark}}) {
        QAction* action = view->addAction(QString::fromUtf8(choice.label));
        action->setCheckable(true);
        action->setChecked(ThemeManager::instance().mode() == choice.mode);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode = choice.mode] {
            applyThemeMode(mode);
        });
    }
    view->addSeparator();
    view->addAction(QStringLiteral("切換亮色／暗色"), QKeySequence(QStringLiteral("Ctrl+Shift+L")),
                    this, [this] {
                        applyThemeMode(ThemeManager::instance().isDark() ? ThemeMode::Light
                                                                        : ThemeMode::Dark);
                    });

    view->addSeparator();
    // Every dock's title bar has a close button. Without these entries a closed
    // panel is unreachable and the window just looks broken.
    auto* panels = view->addMenu(QStringLiteral("面板"));
    for (QDockWidget* dock : docks_) {
        panels->addAction(dock->toggleViewAction());
    }
    view->addAction(QStringLiteral("回復預設版面"), this, &MainWindow::restoreDefaultLayout);

    auto* tools = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    tools->addAction(QStringLiteral("在畫面上選取目標"), this, &MainWindow::openTargetPicker);
    tools->addAction(QStringLiteral("載入 OCR 模型"), this, &MainWindow::loadOcrModels);
    tools->addSeparator();
    tools->addAction(QStringLiteral("REST API 面板"), this, &MainWindow::showApiPanel);
    tools->addAction(QStringLiteral("設定"), this, &MainWindow::showSettings);

    auto* help = menuBar()->addMenu(QStringLiteral("說明(&H)"));
    help->addAction(QStringLiteral("關於"), this, [this] {
        QMessageBox about(this);
        about.setWindowTitle(QStringLiteral("關於 RPA-Block"));
        about.setIconPixmap(QPixmap(QStringLiteral(":/app-logo.png"))
                                .scaled(96, 96, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
        about.setTextFormat(Qt::RichText);
        about.setText(
            QStringLiteral(
                "<h3>RPA-Block 0.1.0</h3>"
                "<p>積木式 RPA 流程編輯器。畫面元素用 <b>OCR 文字錨點</b>或"
                "<b>影像模板</b>定位，而不是固定座標，所以視窗被移動後流程依然能跑。</p>"
                "<p>可以用 AI 對話產生流程，也可以錄製你實際的操作再交給 AI 整理。"
                "做好的流程能透過內建 REST API 從外部觸發。</p>"
                "<p><small>OCR 由 PaddleOCR PP-OCRv5 提供（Apache-2.0）。</small></p>"));
        about.exec();
    });
}

void MainWindow::buildDocks() {
    // --- Left: block palette over the project tree ------------------------
    auto* leftDock = new QDockWidget(QStringLiteral("積木"), this);
    leftDock->setObjectName(QStringLiteral("paletteDock"));

    auto* leftPane = new QWidget(leftDock);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* paletteScroll = new QScrollArea(leftPane);
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setFrameShape(QFrame::NoFrame);
    paletteScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    palette_ = new BlockPalette(paletteScroll);
    paletteScroll->setWidget(palette_);
    leftLayout->addWidget(paletteScroll, 3);

    auto* projectLabel = new QLabel(QStringLiteral("  專案"), leftPane);
    QFont projectFont = projectLabel->font();
    projectFont.setBold(true);
    projectLabel->setFont(projectFont);
    projectLabel->setMinimumHeight(26);
    leftLayout->addWidget(projectLabel);

    projectTree_ = new QTreeWidget(leftPane);
    projectTree_->setHeaderHidden(true);
    // Long flow names must not widen the dock. Without these the tree reports a
    // size hint big enough for its longest entry, the dock grows to match, and
    // the canvas is squeezed until a single block no longer fits.
    projectTree_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    projectTree_->setTextElideMode(Qt::ElideMiddle);
    projectTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    leftLayout->addWidget(projectTree_, 2);

    leftDock->setWidget(leftPane);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    // --- Right: properties over the assistant -----------------------------
    auto* rightDock = new QDockWidget(QStringLiteral("積木設定"), this);
    rightDock->setObjectName(QStringLiteral("propertyDock"));
    properties_ = new PropertyPanel(rightDock);
    rightDock->setWidget(properties_);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    auto* chatDock = new QDockWidget(QStringLiteral("AI 助手"), this);
    chatDock->setObjectName(QStringLiteral("chatDock"));
    chat_ = new ChatDock(chatDock);
    chatDock->setWidget(chat_);
    addDockWidget(Qt::RightDockWidgetArea, chatDock);

    // The right column needs real width -- its forms are label + field pairs
    // (template paths, anchor text) that clip and grow a scrollbar when squeezed.
    // But not at the canvas's expense: a block is 430px plus margins, so a canvas
    // under ~470 scrolls sideways to show a single block, which is worse. These
    // minimums leave 250 + 470 + 380 = 1100 inside the 1500 default.
    properties_->setMinimumWidth(360);
    chat_->setMinimumWidth(360);
    resizeDocks({leftDock, rightDock}, {250, 380}, Qt::Horizontal);
    resizeDocks({rightDock, chatDock}, {520, 380}, Qt::Vertical);

    // --- Bottom: log ------------------------------------------------------
    auto* logDock = new QDockWidget(QStringLiteral("執行紀錄"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    log_ = new QPlainTextEdit(logDock);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    logDock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    resizeDocks({logDock}, {150}, Qt::Vertical);

    // Remembered so the 外觀 menu can offer them back. A dock closes with one
    // click on its title bar; without a way to reopen it the panel is simply gone
    // and the window looks broken.
    docks_ = {leftDock, rightDock, chatDock, logDock};

    // Captured before any user rearrangement, so "回復預設版面" has something
    // real to restore rather than approximating the original layout.
    defaultLayout_ = saveState();
}

void MainWindow::wireSignals() {
    // Palette -> canvas
    connect(palette_, &BlockPalette::blockChosen, this,
            [this](core::StepType type) { addStepOfType(type); });

    // Canvas <-> properties
    connect(editor_, &FlowCanvas::selectionChanged, this,
            [this] { refreshPropertyPanel(); });
    connect(editor_, &FlowCanvas::scriptModified, this, [this] {
        markDirty();
        // Reloading the panel here would fight the user's own typing; the panel
        // is already showing exactly what it just sent.
        if (!applyingPanelEdit_) refreshPropertyPanel();
    });
    connect(editor_, &FlowCanvas::blockActivated, this, &MainWindow::openTargetPicker);

    connect(properties_, &PropertyPanel::stepEdited, this, [this](const core::Step& step) {
        applyingPanelEdit_ = true;
        editor_->replaceSelected(step);
        applyingPanelEdit_ = false;
    });
    connect(properties_, &PropertyPanel::targetPickRequested, this,
            &MainWindow::openTargetPicker);

    // Project tree -> open. Connected once here rather than inside
    // refreshProjectTree: Qt::UniqueConnection has no effect on lambdas, so
    // reconnecting on every refresh would fire the handler once per refresh.
    connect(projectTree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                const QString path = item->data(0, Qt::UserRole).toString();
                if (path.isEmpty()) return;  // a category row, not a flow
                if (!confirmDiscardChanges()) return;

                const core::ParseResult parsed = core::loadScriptFile(path.toStdString());
                if (!parsed.ok) {
                    QMessageBox::critical(this, QStringLiteral("無法開啟流程"),
                                          QString::fromStdString(parsed.error));
                    return;
                }
                setScript(parsed.script, path);
            });

    // Execution
    connect(&execution_, &ExecutionController::stepStarted, this,
            [this](const QString& stepId) { editor_->setRunningStep(stepId); },
            Qt::QueuedConnection);
    connect(&execution_, &ExecutionController::stepFinished, this,
            [this](const QString& stepId, bool ok, const QString&) {
                editor_->setStepOutcome(stepId, ok);
            },
            Qt::QueuedConnection);
    connect(&execution_, &ExecutionController::logged, this, &MainWindow::appendLog,
            Qt::QueuedConnection);
    connect(&execution_, &ExecutionController::statusChanged, this,
            [this](core::RunStatus status) {
                static const QHash<int, QString> kNames = {
                    {int(core::RunStatus::Queued), QStringLiteral("待執行")},
                    {int(core::RunStatus::Running), QStringLiteral("執行中")},
                    {int(core::RunStatus::Paused), QStringLiteral("已暫停")},
                    {int(core::RunStatus::Succeeded), QStringLiteral("成功")},
                    {int(core::RunStatus::Failed), QStringLiteral("失敗")},
                    {int(core::RunStatus::Cancelled), QStringLiteral("已取消")},
                };
                statusBar()->showMessage(
                    QStringLiteral("執行狀態：%1").arg(kNames.value(int(status))));
                updateActionStates();
            },
            Qt::QueuedConnection);
    connect(&execution_, &ExecutionController::runFinished, this,
            [this](const QString& runId, core::RunResult result) {
                runStore_.complete(runId.toStdString(), result);
                updateActionStates();
                if (result.status == core::RunStatus::Failed) {
                    statusBar()->showMessage(
                        QStringLiteral("執行失敗，卡在步驟 %1：%2")
                            .arg(QString::fromStdString(result.failedStepId),
                                 QString::fromStdString(result.error)));
                }
            },
            Qt::QueuedConnection);

    // Assistant
    connect(chat_, &ChatDock::sendRequested, this, &MainWindow::sendToAssistant);
    connect(chat_, &ChatDock::previewDraftRequested, this, &MainWindow::previewAssistantDraft);
    connect(chat_, &ChatDock::applyDraftRequested, this, &MainWindow::applyAssistantDraft);
    connect(chat_, &ChatDock::cancelRequested, this, [this] {
        agent_->cancel();
        chat_->setBusy(false);
        chat_->appendSystemNote(QStringLiteral("已取消這次請求。"));
    });

    connect(agent_, &ai::AgentClient::progress, this,
            [this](const QString& note) { statusBar()->showMessage(note); });
    connect(agent_, &ai::AgentClient::finished, this, [this](const ai::AgentReply& reply) {
        chat_->setBusy(false);
        chat_->appendAssistantReply(reply);

        // A recording hand-off is a one-shot conversion, so its draft goes
        // straight to the preview rather than waiting for another click.
        if (awaitingRecordingDraft_) {
            awaitingRecordingDraft_ = false;
            if (chat_->hasPendingDraft()) previewAssistantDraft();
        }
    });
    connect(agent_, &ai::AgentClient::failed, this, [this](const ai::AgentError& error) {
        chat_->setBusy(false);
        awaitingRecordingDraft_ = false;
        chat_->appendSystemNote(QStringLiteral("AI 請求失敗：%1")
                                    .arg(QString::fromStdString(error.message)));
    });

    // Emergency abort. Registered as an application-wide shortcut so it works
    // even when a running flow has stolen the pointer.
    auto* abort = new QShortcut(QKeySequence(settings_.abortShortcut), this);
    abort->setContext(Qt::ApplicationShortcut);
    connect(abort, &QShortcut::activated, this, [this] {
        stopFlow();
        if (recorder_.isRecording()) toggleRecording();
        statusBar()->showMessage(QStringLiteral("已緊急中止"));
    });

    auto* record = new QShortcut(QKeySequence(settings_.recordShortcut), this);
    record->setContext(Qt::ApplicationShortcut);
    connect(record, &QShortcut::activated, this, &MainWindow::toggleRecording);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void MainWindow::loadSettings() {
    settings_.load();
    // Before buildUi(), so the first paint is already in the saved theme and the
    // "外觀" menu's checkmark starts on the right entry.
    ThemeManager::instance().setMode(themeModeFromString(settings_.themeMode));
}

void MainWindow::restoreLayout() {
    if (!settings_.windowGeometry.isEmpty()) {
        restoreGeometry(settings_.windowGeometry);

        // A saved geometry can be off-screen: the monitor it was on may be gone,
        // or the arrangement may have changed. A window whose title bar is not
        // reachable cannot be moved back, so fall back to the default placement
        // rather than trusting the stored rectangle.
        bool visibleSomewhere = false;
        for (const QScreen* screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().intersects(frameGeometry())) {
                visibleSomewhere = true;
                break;
            }
        }
        if (!visibleSomewhere) {
            resize(1500, 940);
            move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
        }
    }

    if (!settings_.windowState.isEmpty()) {
        restoreState(settings_.windowState);
    }
}

QString MainWindow::projectSubdirectory(const QString& name) const {
    QDir root(settings_.projectDirectory);
    const QString path = root.filePath(name);
    QDir().mkpath(path);
    return path;
}

void MainWindow::applyThemeMode(ThemeMode mode) {
    ThemeManager::instance().setMode(mode);
    settings_.themeMode = themeModeToString(mode);
    settings_.save();
}

void MainWindow::applySettings() {
    agent_->setSettings(settings_.toAgentSettings());
    ThemeManager::instance().setMode(themeModeFromString(settings_.themeMode));

    locator_.setWorkingDirectory(settings_.projectDirectory.toStdString());
    execution_.setWorkingDirectory(settings_.projectDirectory);

    repository_.setDirectory(settings_.publishDirectory.toStdString());
    runStore_.setPersistencePath(settings_.runHistoryPath.toStdString());

    QDir().mkpath(settings_.projectDirectory);
    projectSubdirectory(QStringLiteral("assets"));
    projectSubdirectory(QStringLiteral("recordings"));

    refreshProjectTree();
    updateActionStates();
}

void MainWindow::startApiServerIfConfigured() {
    if (!settings_.apiAutoStart) {
        apiStatusLabel_->setText(QStringLiteral("API：已停止"));
        return;
    }
    if (settings_.apiKeys.isEmpty()) {
        appendLog(2, QString(),
                  QStringLiteral("設定為開機自動啟動 API，但沒有任何 API key，因此不啟動。"));
        apiStatusLabel_->setText(QStringLiteral("API：已停止"));
        return;
    }

    std::string error;
    if (api_->start(settings_.toServerConfig(), error)) {
        apiStatusLabel_->setText(QStringLiteral("API：埠 %1").arg(api_->boundPort()));
    } else {
        appendLog(3, QString(),
                  QStringLiteral("無法啟動 API 伺服器：%1").arg(QString::fromStdString(error)));
        apiStatusLabel_->setText(QStringLiteral("API：啟動失敗"));
    }
}

void MainWindow::loadOcrModels() {
    const QString directory = settings_.resolvedOcrModelDirectory();
    if (directory.isEmpty()) {
        appendLog(2, QString(),
                  QStringLiteral("找不到 OCR 模型，「找文字」積木會找不到目標。"
                                 "請到「設定 → 視覺引擎」指定資料夾。"));
        return;
    }

    const bool bundled = settings_.ocrModelDirectory.isEmpty();

    std::string error;
    if (locator_.loadOcr(settings_.toOcrConfig(), error)) {
        appendLog(1, QString(),
                  bundled ? QStringLiteral("已載入內建的 PP-OCRv5 模型（繁簡中文 + 日文 + 英數）")
                          : QStringLiteral("已從 %1 載入 OCR 模型").arg(directory));
    } else {
        appendLog(3, QString(),
                  QStringLiteral("載入 OCR 模型失敗：%1").arg(QString::fromStdString(error)));
    }
}

// ---------------------------------------------------------------------------
// File handling
// ---------------------------------------------------------------------------

void MainWindow::setScript(const core::Script& script, const QString& path) {
    editor_->setScript(script);
    currentPath_ = path;
    markDirty(false);
    refreshPropertyPanel();
}

void MainWindow::newFlow() {
    if (!confirmDiscardChanges()) return;

    core::Script script;
    script.name = "new-flow";
    script.version = 1;
    setScript(script, QString());
    chat_->clearHistory();
    log_->clear();
}

void MainWindow::openFlow() {
    if (!confirmDiscardChanges()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("開啟流程"), settings_.projectDirectory,
        QStringLiteral("RPA 流程 (*.rpa.json);;所有檔案 (*)"));
    if (path.isEmpty()) return;

    openFlowFile(path);
}

void MainWindow::openFlowFile(const QString& path) {
    const core::ParseResult parsed = core::loadScriptFile(path.toStdString());
    if (!parsed.ok) {
        QMessageBox::critical(this, QStringLiteral("無法開啟流程"),
                              QString::fromStdString(parsed.error));
        return;
    }

    setScript(parsed.script, path);

    if (!parsed.issues.empty()) {
        QStringList lines;
        for (const auto& issue : parsed.issues) {
            lines << QStringLiteral("• %1%2")
                         .arg(issue.stepId.empty()
                                  ? QString()
                                  : QStringLiteral("[%1] ")
                                        .arg(QString::fromStdString(issue.stepId)))
                         .arg(QString::fromStdString(issue.message));
        }
        // The flow is still loaded; the issues are shown so they can be fixed
        // rather than discovered mid-run.
        QMessageBox::warning(this, QStringLiteral("流程已開啟，但有問題需要修正"),
                             lines.join(QLatin1Char('\n')));
    }
}

bool MainWindow::saveFlow() {
    if (currentPath_.isEmpty()) return saveFlowAs();

    std::string error;
    if (!core::saveScriptFile(editor_->script(), currentPath_.toStdString(), error)) {
        QMessageBox::critical(this, QStringLiteral("儲存失敗"), QString::fromStdString(error));
        return false;
    }
    markDirty(false);
    statusBar()->showMessage(QStringLiteral("已儲存 %1").arg(currentPath_));
    refreshProjectTree();
    return true;
}

bool MainWindow::saveFlowAs() {
    const QString suggested =
        QDir(settings_.projectDirectory)
            .filePath(QString::fromStdString(editor_->script().name) +
                      QStringLiteral(".rpa.json"));

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("儲存流程"), suggested,
                                                      QStringLiteral("RPA 流程 (*.rpa.json)"));
    if (path.isEmpty()) return false;

    currentPath_ = path;
    return saveFlow();
}

bool MainWindow::confirmDiscardChanges() {
    if (!dirty_) return true;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("有未儲存的變更"),
        QStringLiteral("這個流程有還沒儲存的變更，要先存起來嗎？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveFlow();
    return true;
}

void MainWindow::markDirty(bool dirty) {
    dirty_ = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = currentPath_.isEmpty()
                             ? QStringLiteral("未命名")
                             : QFileInfo(currentPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2 — RPA-Block")
                       .arg(name, dirty_ ? QStringLiteral("＊") : QString()));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (execution_.isBusy()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("流程正在執行"),
            QStringLiteral("要停止正在跑的流程並結束程式嗎？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        execution_.stop();
    }

    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }

    settings_.windowGeometry = saveGeometry();
    settings_.windowState = saveState();
    settings_.save();
    event->accept();
}

// ---------------------------------------------------------------------------
// Run control
// ---------------------------------------------------------------------------

void MainWindow::restoreDefaultLayout() {
    // Show everything first: restoreState() reinstates positions, but a dock the
    // saved layout had hidden stays hidden, which is the state this action exists
    // to escape.
    for (QDockWidget* dock : docks_) dock->show();
    if (!defaultLayout_.isEmpty()) restoreState(defaultLayout_);
    statusBar()->showMessage(QStringLiteral("已回復預設版面"));
}

void MainWindow::showFlowVariables() {
    editFlowVariables();
}

void MainWindow::editFlowVariables() {
    FlowVariablesDialog dialog(FlowVariablesDialog::Mode::Declare,
                               editor_->script().variables, this);
    if (dialog.exec() != QDialog::Accepted) return;

    core::Script script = editor_->script();
    script.variables = dialog.values();
    editor_->setScript(script);
    markDirty();
    refreshPropertyPanel();
    statusBar()->showMessage(
        QStringLiteral("流程變數已更新（%1 個）").arg(script.variables.size()));
}

void MainWindow::runFlow() {
    const std::map<std::string, std::string>& declared = editor_->script().variables;

    // Ask before a manual run, so a flow whose inputs change every time does not
    // silently run on last week's values. Skipped when the flow declares nothing,
    // and skippable for the session once the values are settled.
    std::map<std::string, std::string> overrides;
    if (!declared.empty() && !skipVariablePrompt_) {
        std::map<std::string, std::string> prefill = declared;
        // Last run's answers win over the declared defaults: re-running with a
        // small change is the common case.
        for (const auto& [name, value] : lastRunVariables_) {
            if (prefill.count(name)) prefill[name] = value;
        }

        FlowVariablesDialog dialog(FlowVariablesDialog::Mode::Supply, prefill, this);
        if (dialog.exec() != QDialog::Accepted) return;

        overrides = dialog.values();
        lastRunVariables_ = overrides;
        if (dialog.suppressFuturePrompts()) skipVariablePrompt_ = true;
    } else {
        overrides = lastRunVariables_;
    }

    editor_->clearRunDecorations();

    // Record what the run actually used, not the declared defaults, or the
    // history shows values the run never saw.
    std::map<std::string, std::string> effective = declared;
    for (const auto& [name, value] : overrides) effective[name] = value;

    const std::string runId = runStore_.createRun(editor_->script().name, "ui", effective);

    QString reason;
    if (!execution_.start(editor_->script(), overrides, QString::fromStdString(runId),
                          QStringLiteral("ui"), reason)) {
        QMessageBox::warning(this, QStringLiteral("無法執行"), reason);
        return;
    }
    updateActionStates();
}

void MainWindow::pauseOrResumeFlow() {
    if (execution_.status() == core::RunStatus::Paused) {
        execution_.resume();
        pauseAction_->setText(QStringLiteral("⏸  暫停"));
    } else {
        execution_.pause();
        pauseAction_->setText(QStringLiteral("▶  繼續"));
    }
}

void MainWindow::stopFlow() {
    execution_.stop();
    updateActionStates();
}

void MainWindow::stepOverFlow() {
    // Single-stepping walks the top-level list, so a nested selection has no
    // meaningful "next step" to advance to.
    const int index = editor_->selectedTopLevelIndex();
    if (index < 0) {
        statusBar()->showMessage(
            QStringLiteral("請先選一個最外層的積木，單步執行只走最外層。"));
        return;
    }

    QString reason;
    if (!execution_.stepOver(editor_->script(), static_cast<size_t>(index), reason)) {
        QMessageBox::warning(this, QStringLiteral("無法單步執行"), reason);
        return;
    }

    if (index + 1 < static_cast<int>(editor_->script().steps.size())) {
        editor_->selectPath({index + 1});
    }
}

void MainWindow::updateActionStates() {
    const bool busy = execution_.isBusy();
    const bool recording = recorder_.isRecording();

    runAction_->setEnabled(!busy && !recording);
    pauseAction_->setEnabled(busy);
    stopAction_->setEnabled(busy);
    stepAction_->setEnabled(!busy && !recording);
    recordAction_->setText(recording ? QStringLiteral("⏹  停止錄製")
                                     : QStringLiteral("⏺  錄製"));

    if (api_) {
        apiStatusLabel_->setText(api_->isRunning()
                                     ? QStringLiteral("API：埠 %1").arg(api_->boundPort())
                                     : QStringLiteral("API：已停止"));
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void MainWindow::addStepOfType(core::StepType type) {
    core::Step step;
    step.type = type;
    step.id = editor_->suggestStepId(type).toStdString();

    // Sensible starting values per type, so a freshly added block is closer to
    // valid than empty.
    switch (type) {
        case core::StepType::Click:
        case core::StepType::DoubleClick:
            step.target.kind = core::TargetKind::Ocr;
            step.clickCount = type == core::StepType::DoubleClick ? 2 : 1;
            break;
        case core::StepType::Wait:
            step.waitMs = 500;
            break;
        case core::StepType::OcrFind:
            step.target.kind = core::TargetKind::Ocr;
            step.saveToVar = "last_match";
            break;
        case core::StepType::ImageFind:
            step.target.kind = core::TargetKind::Image;
            step.target.threshold = settings_.templateThreshold;
            step.saveToVar = "last_match";
            break;
        case core::StepType::Loop:
            step.loopCount = 3;
            break;
        case core::StepType::If: {
            core::Condition condition;
            condition.kind = core::Condition::Kind::OcrFound;
            condition.target.kind = core::TargetKind::Ocr;
            step.condition = condition;
            break;
        }
        case core::StepType::HttpRequest:
            step.httpMethod = "GET";
            step.saveToVar = "response";
            break;
        default:
            break;
    }

    editor_->addStep(step);
}

void MainWindow::refreshPropertyPanel() {
    const core::Step* step = editor_->selectedStep();
    if (!step) {
        properties_->clearStep();
        return;
    }

    QStringList ids;
    core::forEachStep(editor_->script().steps, [&](const core::Step& other) {
        ids << QString::fromStdString(other.id);
    });
    properties_->setAvailableStepIds(ids, QString::fromStdString(step->id));
    properties_->setStep(*step);
}

void MainWindow::refreshProjectTree() {
    projectTree_->clear();

    QDir root(settings_.projectDirectory);
    if (!root.exists()) return;

    auto* flows = new QTreeWidgetItem(projectTree_, {QStringLiteral("流程")});
    for (const QString& name :
         root.entryList({QStringLiteral("*.rpa.json")}, QDir::Files, QDir::Name)) {
        auto* item = new QTreeWidgetItem(flows, {name});
        item->setData(0, Qt::UserRole, root.filePath(name));
    }
    flows->setExpanded(true);

    auto* assets = new QTreeWidgetItem(projectTree_, {QStringLiteral("圖片素材")});
    QDir assetDir(root.filePath(QStringLiteral("assets")));
    for (const QString& name :
         assetDir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name)) {
        new QTreeWidgetItem(assets, {name});
    }

    auto* published = new QTreeWidgetItem(projectTree_, {QStringLiteral("已發佈")});
    for (const auto& entry : repository_.list()) {
        new QTreeWidgetItem(published, {QString::fromStdString(entry.id)});
    }
}

void MainWindow::appendLog(int level, const QString& stepId, const QString& message) {
    static const char* kLevels[] = {"除錯", "資訊", "警告", "錯誤"};
    const int clamped = std::clamp(level, 0, 3);

    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString prefix = stepId.isEmpty() ? QString() : QStringLiteral("[%1] ").arg(stepId);

    log_->appendPlainText(QStringLiteral("%1  %2  %3%4")
                              .arg(stamp)
                              .arg(QString::fromUtf8(kLevels[clamped]))
                              .arg(prefix, message));
}

void MainWindow::showTargetPicker() {
    if (!editor_->selectedStep()) {
        addStepOfType(core::StepType::Click);
    }
    openTargetPicker();
}

void MainWindow::openTargetPicker() {
    if (!editor_->selectedStep()) {
        statusBar()->showMessage(QStringLiteral("請先選一個積木，再選取畫面上的目標。"));
        return;
    }

    if (!picker_) {
        picker_ = new TargetPickerOverlay(&locator_, projectSubdirectory(QStringLiteral("assets")),
                                          nullptr);
        connect(picker_, &TargetPickerOverlay::targetPicked, this,
                [this](const core::Target& target) {
                    showNormal();
                    raise();

                    // The selection can have moved while the overlay was up.
                    const core::Step* current = editor_->selectedStep();
                    if (!current) {
                        statusBar()->showMessage(
                            QStringLiteral("選取的積木已改變，剛選的目標沒有套用。"));
                        return;
                    }

                    core::Step step = *current;
                    // Keep the retry tuning the user already set; only the
                    // anchor itself comes from the picker.
                    const core::RetryPolicy retry = step.target.retry;
                    step.target = target;
                    step.target.retry = retry;
                    editor_->replaceSelected(step);
                    refreshPropertyPanel();
                });
        connect(picker_, &TargetPickerOverlay::cancelled, this, [this] {
            showNormal();
            raise();
        });
    }

    // The project folder can have changed in Settings since the picker was
    // built; re-point it so crops land where the locator will look for them.
    picker_->setTemplateDirectory(projectSubdirectory(QStringLiteral("assets")));

    // Get out of the way so the picker captures the target application, not us.
    // hide() rather than showMinimized(): minimising is animated, and
    // processEvents() does not wait for the animation, so the capture used to
    // include a half-shrunk picture of the editor sitting on top of the very
    // application the user was trying to point at.
    hide();

    // Even without an animation the desktop underneath has to be recomposited
    // before a capture will show it, which no event-loop round trip guarantees.
    QTimer::singleShot(kUncoverDelayMs, this, [this] {
        QString error;
        if (!picker_->beginPick(error)) {
            showNormal();
            QMessageBox::warning(this, QStringLiteral("無法擷取畫面"), error);
        }
    });
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void MainWindow::toggleRecording() {
    if (recorder_.isRecording()) {
        recorder_.stop();
        if (recordingTimer_) recordingTimer_->stop();
        if (recordingBar_) recordingBar_->hide();

        showNormal();
        raise();
        updateActionStates();

        const recorder::RecordingSession session = recorder_.session();
        if (session.events.empty()) {
            statusBar()->showMessage(QStringLiteral("錄製結束，但沒有錄到任何操作。"));
            return;
        }

        RecordingResultDialog dialog(session, this);
        bool handOff = false;
        connect(&dialog, &RecordingResultDialog::handOffToAssistantRequested, this,
                [&handOff] { handOff = true; });

        dialog.exec();
        if (handOff) handOffRecordingToAssistant(dialog.trimmedSession());
        return;
    }

    if (execution_.isBusy()) {
        QMessageBox::warning(this, QStringLiteral("無法錄製"),
                             QStringLiteral("請先停止正在執行的流程。"));
        return;
    }

    recorder::RecorderConfig config;
    config.assetDirectory = projectSubdirectory(QStringLiteral("recordings")).toStdString();
    config.clickCaptureRadius = settings_.clickCaptureRadius;
    config.captureElementInfo = settings_.captureElementInfo;

    std::string error;
    if (!recorder_.start(config, error)) {
        QMessageBox::critical(this, QStringLiteral("無法開始錄製"),
                              QString::fromStdString(error));
        return;
    }

    if (!recordingBar_) {
        recordingBar_ = new RecordingOverlayBar(nullptr);
        connect(recordingBar_, &RecordingOverlayBar::stopRequested, this,
                &MainWindow::toggleRecording);
        // Without this the bar would repaint itself as "paused" while the hooks
        // kept capturing — a button that lies about what it did.
        connect(recordingBar_, &RecordingOverlayBar::pauseToggled, this,
                [this](bool paused) { recorder_.setPaused(paused); });
    }

    recordingClock_.start();
    recordingBar_->setEventCount(0);
    recordingBar_->setElapsedSeconds(0);
    recordingBar_->setPaused(false);
    recordingBar_->show();
    recordingBar_->move(80, 80);

    if (!recordingTimer_) {
        recordingTimer_ = new QTimer(this);
        recordingTimer_->setInterval(500);
        connect(recordingTimer_, &QTimer::timeout, this, [this] {
            if (!recordingBar_) return;
            recordingBar_->setElapsedSeconds(
                static_cast<int>(recordingClock_.elapsed() / 1000));
            recordingBar_->setEventCount(static_cast<int>(recorder_.session().events.size()));
        });
    }
    recordingTimer_->start();

    showMinimized();
    updateActionStates();
    statusBar()->showMessage(QStringLiteral("正在錄製…"));
}

void MainWindow::handOffRecordingToAssistant(const recorder::RecordingSession& session) {
    const std::string summary = recorder::toSummaryText(session);
    const std::string prompt =
        ai::PromptBuilder::recordingToFlowRequest(summary, editor_->script());

    chat_->appendUserMessage(
        QStringLiteral("把剛才錄的 %1 個操作整理成流程。").arg(session.events.size()), false);

    std::vector<ai::ChatMessage> history = chat_->history();
    if (history.empty()) history.push_back(ai::ChatMessage{});
    history.back().text = prompt;

    // Send the most informative frame we have so the assistant can see the app.
    for (const auto& event : session.events) {
        const std::string& path = event.fullScreenshotPath.empty() ? event.screenshotPath
                                                                   : event.fullScreenshotPath;
        if (path.empty()) continue;

        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        history.back().imagePng.assign(bytes.begin(), bytes.end());
        break;
    }

    awaitingRecordingDraft_ = true;
    chat_->setBusy(true);
    agent_->send(history);
}

// ---------------------------------------------------------------------------
// Assistant
// ---------------------------------------------------------------------------

void MainWindow::sendToAssistant(const QString& text, bool attachScreenshot) {
    chat_->appendUserMessage(text, attachScreenshot);

    std::vector<ai::ChatMessage> history = chat_->history();
    history.back().text =
        ai::PromptBuilder::chatRequest(text.toStdString(), editor_->script());

    if (attachScreenshot) {
        std::string error;
        const cv::Mat frame = vision::ScreenCapture::grab(std::nullopt, error);
        std::vector<uchar> png;
        if (!frame.empty() && cv::imencode(".png", frame, png)) {
            history.back().imagePng.assign(png.begin(), png.end());
        } else {
            chat_->appendSystemNote(
                QStringLiteral("擷取畫面失敗，這則訊息不附截圖送出。"));
        }
        chat_->clearAttachScreenshot();
    }

    chat_->setBusy(true);
    agent_->send(history);
}

void MainWindow::previewAssistantDraft() {
    if (!chat_->hasPendingDraft()) return;

    DraftPreviewDialog dialog(editor_->script().steps, chat_->pendingSteps(), this);
    dialog.exec();
    if (dialog.applyRequested()) applyAssistantDraft();
}

void MainWindow::applyAssistantDraft() {
    if (!chat_->hasPendingDraft()) return;

    core::Script script = editor_->script();
    script.steps = chat_->pendingSteps();
    if (!chat_->pendingScriptName().isEmpty()) {
        script.name = chat_->pendingScriptName().toStdString();
    }
    editor_->setScript(script);

    chat_->clearPendingDraft();
    chat_->appendSystemNote(QStringLiteral("已把草稿套用到流程。"));
    markDirty();
    refreshPropertyPanel();
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

void MainWindow::publishCurrentFlow() {
    if (editor_->script().steps.empty()) {
        QMessageBox::warning(this, QStringLiteral("沒有東西可以發佈"),
                             QStringLiteral("這個流程還沒有任何積木。"));
        return;
    }

    const auto issues = core::validate(editor_->script());
    if (!issues.empty()) {
        QStringList lines;
        for (const auto& issue : issues) lines << QString::fromStdString(issue.message);

        const auto answer = QMessageBox::question(
            this, QStringLiteral("流程有問題尚未修正"),
            QStringLiteral("%1\n\n還是要發佈嗎？從 API 呼叫時會在執行階段撞到這些問題。")
                .arg(lines.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    // The id is what goes in the REST URL, so let it be chosen rather than
    // derived. A Chinese flow name has no usable ASCII to slugify, and the
    // automatic fallback -- name plus a digest -- is unique but not something
    // anyone wants to type into a webhook.
    const QString suggested = QString::fromStdString(
        server::ScriptRepository::makeId(editor_->script().name));

    bool accepted = false;
    const QString chosen = QInputDialog::getText(
        this, QStringLiteral("發佈流程"),
        QStringLiteral("API 識別碼（會出現在網址裡）：\n"
                       "POST /api/v1/scripts/<識別碼>/run\n\n"
                       "只能用英數字、- 和 _。留空則自動產生。"),
        QLineEdit::Normal, suggested, &accepted);
    if (!accepted) return;

    std::string error;
    const auto id = repository_.publish(editor_->script(), chosen.trimmed().toStdString(), error);
    if (!id) {
        QMessageBox::critical(this, QStringLiteral("發佈失敗"), QString::fromStdString(error));
        return;
    }

    refreshProjectTree();
    if (apiPanel_) apiPanel_->refresh();
    statusBar()->showMessage(
        QStringLiteral("已發佈為 %1，可用 POST /api/v1/scripts/%1/run 觸發")
            .arg(QString::fromStdString(*id)));
}

void MainWindow::showApiPanel() {
    if (!apiPanel_) {
        apiPanel_ = new ApiPanelDialog(&settings_, api_.get(), &repository_, &runStore_, this);
        connect(apiPanel_, &ApiPanelDialog::settingsChanged, this, [this] {
            settings_.save();
            updateActionStates();
        });
        connect(apiPanel_, &ApiPanelDialog::publishCurrentFlowRequested, this,
                &MainWindow::publishCurrentFlow);
    }
    apiPanel_->refresh();
    // So it can flag a published snapshot the canvas has since moved past.
    apiPanel_->setCurrentFlow(editor_->script());
    apiPanel_->show();
    apiPanel_->raise();
}

void MainWindow::showSettings() {
    SettingsDialog dialog(&settings_, agent_, this);
    connect(&dialog, &SettingsDialog::applied, this, [this] {
        settings_.save();
        applySettings();
    });
    dialog.exec();
}

}  // namespace rpa::studio
