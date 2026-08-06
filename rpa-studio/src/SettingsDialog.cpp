#include "SettingsDialog.h"

#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "rpa/ai/AgentClient.h"

namespace rpa::studio {

SettingsDialog::SettingsDialog(AppSettings* settings, ai::AgentClient* agent, QWidget* parent)
    : QDialog(parent), settings_(settings), agent_(agent) {
    setWindowTitle(QStringLiteral("設定"));
    resize(700, 560);

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildAppearanceTab(), QStringLiteral("外觀"));
    tabs->addTab(buildAiTab(), QStringLiteral("AI 服務"));
    tabs->addTab(buildVisionTab(), QStringLiteral("視覺引擎"));
    tabs->addTab(buildRecorderTab(), QStringLiteral("錄製器"));
    tabs->addTab(buildShortcutTab(), QStringLiteral("快速鍵"));
    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        store();
        emit applied();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (agent_) {
        connect(agent_, &ai::AgentClient::connectionTested, this,
                [this](bool ok, const QString& detail) {
                    testButton_->setEnabled(true);
                    testResultLabel_->setText(
                        QStringLiteral("<span style='color:%1'>%2</span>")
                            .arg(ok ? theme().success.name() : theme().danger.name(), detail));
                });
    }

    load();
}

QWidget* SettingsDialog::buildAiTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* endpointBox = new QGroupBox(QStringLiteral("閘道"), page);
    auto* endpointForm = new QFormLayout(endpointBox);

    gatewayEdit_ = new QLineEdit(page);
    endpointForm->addRow(QStringLiteral("基底網址"), gatewayEdit_);

    assistantIdEdit_ = new QLineEdit(page);
    endpointForm->addRow(QStringLiteral("助手 ID"), assistantIdEdit_);

    auto* endpointHint = new QLabel(
        QStringLiteral("助手 ID 必須和閘道實際提供的 graph id 一致。"
                       "請求會送到 <code>&lt;基底網址&gt;/runs/stream</code>。"),
        page);
    endpointHint->setWordWrap(true);
    endpointForm->addRow(QString(), endpointHint);

    layout->addWidget(endpointBox);

    auto* authBox = new QGroupBox(QStringLiteral("認證方式"), page);
    auto* authForm = new QFormLayout(authBox);

    apiKeyRadio_ = new QRadioButton(QStringLiteral("X-API-Key（TeamSync 使用者 API 金鑰）"), page);
    jwtRadio_ = new QRadioButton(QStringLiteral("Bearer JWT（用帳號密碼登入取得）"), page);
    authForm->addRow(QString(), apiKeyRadio_);
    authForm->addRow(QString(), jwtRadio_);

    secretEdit_ = new QLineEdit(page);
    secretEdit_->setEchoMode(QLineEdit::Password);
    authForm->addRow(QStringLiteral("金鑰"), secretEdit_);

    signInUrlEdit_ = new QLineEdit(page);
    authForm->addRow(QStringLiteral("登入網址"), signInUrlEdit_);

    auto* secretHint = new QLabel(
        QStringLiteral("金鑰存在 Windows 認證管理員裡，不會和其他設定放在一起。"),
        page);
    secretHint->setWordWrap(true);
    authForm->addRow(QString(), secretHint);

    connect(jwtRadio_, &QRadioButton::toggled, this, [this](bool on) {
        signInUrlEdit_->setEnabled(on);
    });

    layout->addWidget(authBox);

    auto* modelBox = new QGroupBox(QStringLiteral("模型"), page);
    auto* modelForm = new QFormLayout(modelBox);

    providerCombo_ = new QComboBox(page);
    providerCombo_->addItem(QStringLiteral("（用閘道預設）"), QString());
    providerCombo_->addItem(QStringLiteral("claude"), QStringLiteral("claude"));
    providerCombo_->addItem(QStringLiteral("gemini"), QStringLiteral("gemini"));
    modelForm->addRow(QStringLiteral("供應商"), providerCombo_);

    modelCombo_ = new QComboBox(page);
    // Editable, because the gateway decides which ids it accepts — these are
    // suggestions, and an empty value defers to the gateway's own default.
    modelCombo_->setEditable(true);
    modelCombo_->addItem(QStringLiteral("（用閘道預設）"), QString());
    for (const char* id : {"claude-opus-5", "claude-sonnet-5", "claude-haiku-4-5",
                           "claude-opus-4-8", "claude-fable-5"}) {
        modelCombo_->addItem(QString::fromLatin1(id), QString::fromLatin1(id));
    }
    modelForm->addRow(QStringLiteral("模型"), modelCombo_);

    timeoutSpin_ = new QSpinBox(page);
    timeoutSpin_->setRange(10000, 600000);
    timeoutSpin_->setSingleStep(10000);
    timeoutSpin_->setSuffix(QStringLiteral(" 毫秒"));
    modelForm->addRow(QStringLiteral("請求逾時"), timeoutSpin_);

    auto* testRow = new QWidget(page);
    auto* testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    testButton_ = new QPushButton(QStringLiteral("測試連線"), testRow);
    testLayout->addWidget(testButton_);
    testLayout->addStretch(1);
    modelForm->addRow(QString(), testRow);

    testResultLabel_ = new QLabel(page);
    testResultLabel_->setWordWrap(true);
    modelForm->addRow(QString(), testResultLabel_);

    connect(testButton_, &QPushButton::clicked, this, &SettingsDialog::testConnection);

    layout->addWidget(modelBox);
    layout->addStretch(1);
    return page;
}

QWidget* SettingsDialog::buildAppearanceTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    themeCombo_ = new QComboBox(page);
    themeCombo_->addItem(QStringLiteral("跟隨系統"), themeModeToString(ThemeMode::System));
    themeCombo_->addItem(QStringLiteral("亮色"), themeModeToString(ThemeMode::Light));
    themeCombo_->addItem(QStringLiteral("暗色"), themeModeToString(ThemeMode::Dark));
    form->addRow(QStringLiteral("色彩主題"), themeCombo_);

    // Applied as the selection changes rather than on OK: picking a theme you
    // cannot see the result of is a guess, and the choice is trivially reversible.
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        ThemeManager::instance().setMode(
            themeModeFromString(themeCombo_->currentData().toString()));
    });

    auto* hint = new QLabel(
        QStringLiteral("「跟隨系統」會跟著 Windows 的深淺色設定即時切換。\n"
                       "也可以用 Ctrl+Shift+L 直接切換，或從「外觀」選單選。"),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    return page;
}

QWidget* SettingsDialog::buildVisionTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* directoryRow = new QWidget(page);
    auto* directoryLayout = new QHBoxLayout(directoryRow);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    ocrDirectoryEdit_ = new QLineEdit(directoryRow);
    directoryLayout->addWidget(ocrDirectoryEdit_);
    auto* browse = new QPushButton(QStringLiteral("瀏覽…"), directoryRow);
    directoryLayout->addWidget(browse);
    form->addRow(QStringLiteral("OCR 模型資料夾"), directoryRow);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseForOcrModels);

    auto* modelHint = new QLabel(
        QStringLiteral("資料夾裡要有 <code>det.onnx</code>、<code>rec.onnx</code> 和 "
                       "<code>keys.txt</code>，也就是匯出成 ONNX 的 PP-OCR 偵測與辨識"
                       "模型，加上它們的字典檔。"),
        page);
    modelHint->setWordWrap(true);
    form->addRow(QString(), modelHint);

    ocrLanguageCombo_ = new QComboBox(page);
    ocrLanguageCombo_->addItem(QStringLiteral("繁體中文 + 英文"),
                               QStringLiteral("chinese_traditional+english"));
    ocrLanguageCombo_->addItem(QStringLiteral("簡體中文 + 英文"),
                               QStringLiteral("chinese_simplified+english"));
    ocrLanguageCombo_->addItem(QStringLiteral("只有英文"), QStringLiteral("english"));
    form->addRow(QStringLiteral("語言"), ocrLanguageCombo_);

    thresholdSpin_ = new QDoubleSpinBox(page);
    thresholdSpin_->setRange(0.10, 1.00);
    thresholdSpin_->setSingleStep(0.01);
    thresholdSpin_->setDecimals(2);
    form->addRow(QStringLiteral("圖片比對預設相似度"), thresholdSpin_);

    loadOcrCheck_ = new QCheckBox(QStringLiteral("啟動時載入 OCR 模型"), page);
    form->addRow(QString(), loadOcrCheck_);

    auto* loadHint = new QLabel(
        QStringLiteral("載入要花幾秒鐘。如果你主要用圖片比對，可以不要開。"),
        page);
    loadHint->setWordWrap(true);
    form->addRow(QString(), loadHint);

    return page;
}

QWidget* SettingsDialog::buildRecorderTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    clickRadiusSpin_ = new QSpinBox(page);
    clickRadiusSpin_->setRange(20, 600);
    clickRadiusSpin_->setSuffix(QStringLiteral(" 像素"));
    form->addRow(QStringLiteral("每次點擊的截圖半徑"), clickRadiusSpin_);

    elementInfoCheck_ = new QCheckBox(QStringLiteral("每次點擊查詢 UI 元素資訊"), page);
    form->addRow(QString(), elementInfoCheck_);

    auto* elementHint = new QLabel(
        QStringLiteral("AI 是靠元素名稱才能把一次點擊改寫成文字錨點。關掉會讓錄製輕一點，"
                       "但產生出來的流程會脆弱很多。"),
        page);
    elementHint->setWordWrap(true);
    form->addRow(QString(), elementHint);

    auto* projectRow = new QWidget(page);
    auto* projectLayout = new QHBoxLayout(projectRow);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectDirectoryEdit_ = new QLineEdit(projectRow);
    projectLayout->addWidget(projectDirectoryEdit_);
    auto* browse = new QPushButton(QStringLiteral("瀏覽…"), projectRow);
    projectLayout->addWidget(browse);
    form->addRow(QStringLiteral("專案資料夾"), projectRow);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseForProjectDirectory);

    auto* projectHint = new QLabel(
        QStringLiteral("流程檔、<code>assets/</code> 圖片、錄製檔和發佈資料夾都放在這裡。"
                       "流程裡的圖片路徑是相對於這個資料夾，所以流程可以在不同電腦之間搬移。"),
        page);
    projectHint->setWordWrap(true);
    form->addRow(QString(), projectHint);

    return page;
}

QWidget* SettingsDialog::buildShortcutTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    recordShortcutEdit_ = new QLineEdit(page);
    form->addRow(QStringLiteral("開始／停止錄製"), recordShortcutEdit_);

    abortShortcutEdit_ = new QLineEdit(page);
    form->addRow(QStringLiteral("緊急中止"), abortShortcutEdit_);

    auto* hint = new QLabel(
        QStringLiteral("緊急中止是註冊成全系統快速鍵。流程正在操作滑鼠時你按不到任何按鈕，"
                       "這個鍵就是唯一的逃生門 —— 請設成你絕對不會誤按的組合。"),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);

    return page;
}

void SettingsDialog::load() {
    themeCombo_->setCurrentIndex(
        std::max(0, themeCombo_->findData(settings_->themeMode)));

    gatewayEdit_->setText(settings_->gatewayUrl);
    assistantIdEdit_->setText(settings_->assistantId);
    signInUrlEdit_->setText(settings_->signInUrl);
    apiKeyRadio_->setChecked(settings_->authMode == ai::AuthMode::ApiKey);
    jwtRadio_->setChecked(settings_->authMode == ai::AuthMode::Jwt);
    signInUrlEdit_->setEnabled(settings_->authMode == ai::AuthMode::Jwt);
    secretEdit_->setText(settings_->aiSecret());

    providerCombo_->setCurrentIndex(std::max(0, providerCombo_->findData(settings_->provider)));
    if (settings_->model.isEmpty()) {
        modelCombo_->setCurrentIndex(0);
    } else {
        modelCombo_->setCurrentText(settings_->model);
    }
    timeoutSpin_->setValue(settings_->aiTimeoutMs);

    ocrDirectoryEdit_->setText(settings_->ocrModelDirectory);
    ocrLanguageCombo_->setCurrentIndex(
        std::max(0, ocrLanguageCombo_->findData(settings_->ocrLanguage)));
    thresholdSpin_->setValue(settings_->templateThreshold);
    loadOcrCheck_->setChecked(settings_->loadOcrOnStartup);

    clickRadiusSpin_->setValue(settings_->clickCaptureRadius);
    elementInfoCheck_->setChecked(settings_->captureElementInfo);
    projectDirectoryEdit_->setText(settings_->projectDirectory);

    recordShortcutEdit_->setText(settings_->recordShortcut);
    abortShortcutEdit_->setText(settings_->abortShortcut);
}

void SettingsDialog::storeInto(AppSettings* target) const {
    target->themeMode = themeCombo_->currentData().toString();
    target->gatewayUrl = gatewayEdit_->text().trimmed();
    target->assistantId = assistantIdEdit_->text().trimmed();
    target->signInUrl = signInUrlEdit_->text().trimmed();
    target->authMode = jwtRadio_->isChecked() ? ai::AuthMode::Jwt : ai::AuthMode::ApiKey;

    target->provider = providerCombo_->currentData().toString();

    // The model box is editable, so an id the user typed has no data role.
    const QString modelData = modelCombo_->currentData().toString();
    const QString modelText = modelCombo_->currentText().trimmed();
    const bool pickedFromList =
        modelCombo_->currentIndex() >= 0 &&
        modelText == modelCombo_->itemText(modelCombo_->currentIndex());
    target->model = pickedFromList ? modelData : modelText;

    target->aiTimeoutMs = timeoutSpin_->value();

    target->ocrModelDirectory = ocrDirectoryEdit_->text().trimmed();
    target->ocrLanguage = ocrLanguageCombo_->currentData().toString();
    target->templateThreshold = thresholdSpin_->value();
    target->loadOcrOnStartup = loadOcrCheck_->isChecked();

    target->clickCaptureRadius = clickRadiusSpin_->value();
    target->captureElementInfo = elementInfoCheck_->isChecked();
    target->projectDirectory = projectDirectoryEdit_->text().trimmed();

    target->recordShortcut = recordShortcutEdit_->text().trimmed();
    target->abortShortcut = abortShortcutEdit_->text().trimmed();
}

void SettingsDialog::store() {
    storeInto(settings_);
    // The secret lives in the OS credential store rather than on the struct, so
    // it is written only on a real commit — never from the test path.
    settings_->setAiSecret(secretEdit_->text());
}

void SettingsDialog::testConnection() {
    if (!agent_) return;

    // Probe with a throwaway copy of the form. Committing here would mean
    // pressing Test and then Cancel still applied every edit — including
    // writing the secret to the Windows Credential Manager.
    AppSettings probe = *settings_;
    storeInto(&probe);

    ai::AgentSettings agentSettings = probe.toAgentSettings();
    const std::string secret = secretEdit_->text().toStdString();
    if (probe.authMode == ai::AuthMode::Jwt) {
        agentSettings.accessToken = secret;
    } else {
        agentSettings.apiKey = secret;
    }
    agent_->setSettings(std::move(agentSettings));

    testButton_->setEnabled(false);
    testResultLabel_->setText(QStringLiteral("正在連線…"));
    agent_->testConnection();
}

void SettingsDialog::browseForOcrModels() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("選擇 OCR 模型資料夾"), ocrDirectoryEdit_->text());
    if (!directory.isEmpty()) ocrDirectoryEdit_->setText(directory);
}

void SettingsDialog::browseForProjectDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("選擇專案資料夾"), projectDirectoryEdit_->text());
    if (!directory.isEmpty()) projectDirectoryEdit_->setText(directory);
}

}  // namespace rpa::studio
