#include "ApiPanelDialog.h"

#include "CommandPreviewDialog.h"
#include "Theme.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace rpa::studio {

namespace {

QString statusText(core::RunStatus status) {
    return QString::fromStdString(core::toString(status));
}

QColor statusColour(core::RunStatus status) {
    switch (status) {
        case core::RunStatus::Succeeded: return QColor(61, 122, 78);
        case core::RunStatus::Failed: return QColor(191, 64, 56);
        case core::RunStatus::Cancelled: return QColor(139, 147, 163);
        default: return QColor(53, 97, 143);
    }
}

}  // namespace

ApiPanelDialog::ApiPanelDialog(AppSettings* settings,
                               server::ApiServer* api,
                               server::ScriptRepository* repository,
                               server::RunStore* runStore,
                               QWidget* parent)
    : QDialog(parent),
      settings_(settings),
      api_(api),
      repository_(repository),
      runStore_(runStore) {
    setWindowTitle(QStringLiteral("REST API 伺服器"));
    resize(860, 700);
    buildUi();
    refresh();

    // The server runs on its own thread, and history grows from API calls the
    // dialog never sees, so poll rather than trying to wire signals across.
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(2000);
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        refreshStatus();
        refreshRuns();
    });
    refreshTimer_->start();
}

void ApiPanelDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    // --- Server control ---------------------------------------------------
    auto* serverBox = new QGroupBox(QStringLiteral("伺服器"), this);
    auto* serverLayout = new QGridLayout(serverBox);

    statusLabel_ = new QLabel(serverBox);
    serverLayout->addWidget(statusLabel_, 0, 0, 1, 2);

    toggleButton_ = new QPushButton(serverBox);
    serverLayout->addWidget(toggleButton_, 0, 2);
    connect(toggleButton_, &QPushButton::clicked, this, &ApiPanelDialog::toggleServer);

    serverLayout->addWidget(new QLabel(QStringLiteral("綁定位址"), serverBox), 1, 0);
    bindCombo_ = new QComboBox(serverBox);
    bindCombo_->addItem(QStringLiteral("127.0.0.1（只有這台電腦）"), QStringLiteral("127.0.0.1"));
    bindCombo_->addItem(QStringLiteral("0.0.0.0（網路上都連得到）"), QStringLiteral("0.0.0.0"));
    serverLayout->addWidget(bindCombo_, 1, 1);

    serverLayout->addWidget(new QLabel(QStringLiteral("連接埠"), serverBox), 2, 0);
    portSpin_ = new QSpinBox(serverBox);
    portSpin_->setRange(1, 65535);
    serverLayout->addWidget(portSpin_, 2, 1);

    autoStartCheck_ = new QCheckBox(QStringLiteral("程式啟動時自動開啟伺服器"), serverBox);
    serverLayout->addWidget(autoStartCheck_, 3, 0, 1, 3);

    auto* warning = new QLabel(
        QStringLiteral("<span style='color:%1'>綁定 0.0.0.0 等於讓網路上任何連得到"
                       "這台電腦的人都能操作它的滑鼠和鍵盤。只在信任的網路裡這樣做，"
                       "並且務必保管好 API 金鑰。</span>")
            .arg(theme().danger.name()),
        serverBox);
    warning->setWordWrap(true);
    serverLayout->addWidget(warning, 4, 0, 1, 3);

    layout->addWidget(serverBox);

    connect(bindCombo_, &QComboBox::currentIndexChanged, this, [this] {
        settings_->apiBindAddress = bindCombo_->currentData().toString();
        emit settingsChanged();
    });
    connect(portSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        settings_->apiPort = value;
        emit settingsChanged();
    });
    connect(autoStartCheck_, &QCheckBox::toggled, this, [this](bool on) {
        settings_->apiAutoStart = on;
        emit settingsChanged();
    });

    // --- API keys ---------------------------------------------------------
    auto* keyBox = new QGroupBox(QStringLiteral("API 金鑰"), this);
    auto* keyLayout = new QVBoxLayout(keyBox);

    keyList_ = new QListWidget(keyBox);
    keyList_->setMaximumHeight(110);
    keyLayout->addWidget(keyList_);

    auto* keyButtons = new QWidget(keyBox);
    auto* keyButtonLayout = new QHBoxLayout(keyButtons);
    keyButtonLayout->setContentsMargins(0, 0, 0, 0);

    auto* generate = new QPushButton(QStringLiteral("＋  產生新金鑰"), keyButtons);
    auto* copyKey = new QPushButton(QStringLiteral("複製"), keyButtons);
    auto* revoke = new QPushButton(QStringLiteral("撤銷"), keyButtons);
    keyButtonLayout->addWidget(generate);
    keyButtonLayout->addWidget(copyKey);
    keyButtonLayout->addWidget(revoke);
    keyButtonLayout->addStretch(1);
    keyLayout->addWidget(keyButtons);

    connect(generate, &QPushButton::clicked, this, &ApiPanelDialog::generateKey);
    connect(copyKey, &QPushButton::clicked, this, &ApiPanelDialog::copySelectedKey);
    connect(revoke, &QPushButton::clicked, this, &ApiPanelDialog::revokeSelectedKey);

    layout->addWidget(keyBox);

    // --- Published flows --------------------------------------------------
    auto* scriptBox = new QGroupBox(QStringLiteral("已發佈的流程"), this);
    auto* scriptLayout = new QVBoxLayout(scriptBox);

    scriptList_ = new QListWidget(scriptBox);
    scriptList_->setMaximumHeight(130);
    scriptLayout->addWidget(scriptList_);

    auto* scriptButtons = new QWidget(scriptBox);
    auto* scriptButtonLayout = new QHBoxLayout(scriptButtons);
    scriptButtonLayout->setContentsMargins(0, 0, 0, 0);

    auto* publishCurrent = new QPushButton(QStringLiteral("發佈目前開啟的流程"), scriptButtons);
    auto* copyPowerShell =
        new QPushButton(QStringLiteral("複製 PowerShell"), scriptButtons);
    auto* copyCurl = new QPushButton(QStringLiteral("複製 curl（cmd／bash）"), scriptButtons);
    auto* unpublish = new QPushButton(QStringLiteral("取消發佈"), scriptButtons);
    auto* rescan = new QPushButton(QStringLiteral("重新掃描資料夾"), scriptButtons);
    scriptButtonLayout->addWidget(publishCurrent);
    scriptButtonLayout->addWidget(copyPowerShell);
    scriptButtonLayout->addWidget(copyCurl);
    scriptButtonLayout->addWidget(unpublish);
    scriptButtonLayout->addWidget(rescan);
    scriptButtonLayout->addStretch(1);
    scriptLayout->addWidget(scriptButtons);

    connect(publishCurrent, &QPushButton::clicked, this,
            [this] { emit publishCurrentFlowRequested(); });
    connect(copyCurl, &QPushButton::clicked, this,
            &ApiPanelDialog::copyCurlForSelectedScript);
    connect(copyPowerShell, &QPushButton::clicked, this,
            &ApiPanelDialog::copyPowerShellForSelectedScript);
    connect(unpublish, &QPushButton::clicked, this, &ApiPanelDialog::unpublishSelectedScript);
    connect(rescan, &QPushButton::clicked, this, [this] {
        std::vector<std::string> errors;
        repository_->reload(&errors);
        refreshScripts();
        if (!errors.empty()) {
            QStringList lines;
            for (const auto& error : errors) lines << QString::fromStdString(error);
            QMessageBox::warning(this, QStringLiteral("重新掃描時發現問題"),
                                 lines.join(QLatin1Char('\n')));
        }
    });

    layout->addWidget(scriptBox);

    // --- Run history ------------------------------------------------------
    auto* runBox = new QGroupBox(QStringLiteral("執行紀錄"), this);
    auto* runLayout = new QVBoxLayout(runBox);

    runTable_ = new QTableWidget(runBox);
    runTable_->setColumnCount(6);
    runTable_->setHorizontalHeaderLabels({QStringLiteral("編號"), QStringLiteral("流程"), QStringLiteral("結果"), QStringLiteral("開始"),
                                          QStringLiteral("結束"), QStringLiteral("來源")});
    runTable_->horizontalHeader()->setStretchLastSection(true);
    runTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    runTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    runLayout->addWidget(runTable_);

    layout->addWidget(runBox, 1);

    auto* closeRow = new QWidget(this);
    auto* closeLayout = new QHBoxLayout(closeRow);
    closeLayout->setContentsMargins(0, 0, 0, 0);
    closeLayout->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("關閉"), closeRow);
    closeLayout->addWidget(close);
    layout->addWidget(closeRow);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void ApiPanelDialog::refresh() {
    // Block the change signals while loading. Without this, a bind address that
    // is not one of the two listed options makes findData() return -1, the
    // clamp below picks index 0, and the "user changed it" handler silently
    // rewrites the setting to 127.0.0.1 and saves it.
    {
        const QSignalBlocker blockBind(bindCombo_);
        const QSignalBlocker blockPort(portSpin_);
        const QSignalBlocker blockAutoStart(autoStartCheck_);

        const int bindIndex = bindCombo_->findData(settings_->apiBindAddress);
        if (bindIndex >= 0) {
            bindCombo_->setCurrentIndex(bindIndex);
        } else {
            // Keep the configured value visible rather than pretending it is
            // loopback; the user set it deliberately somewhere else.
            bindCombo_->addItem(settings_->apiBindAddress, settings_->apiBindAddress);
            bindCombo_->setCurrentIndex(bindCombo_->count() - 1);
        }
        portSpin_->setValue(settings_->apiPort);
        autoStartCheck_->setChecked(settings_->apiAutoStart);
    }

    refreshStatus();
    refreshKeys();
    refreshScripts();
    refreshRuns();
}

void ApiPanelDialog::refreshStatus() {
    const bool running = api_->isRunning();

    if (running) {
        statusLabel_->setText(QStringLiteral("<b><span style='color:%1'>● 執行中</span></b>"
                                             " — http://%2:%3/api/v1")
                                  .arg(theme().success.name(), settings_->apiBindAddress)
                                  .arg(api_->boundPort()));
    } else {
        statusLabel_->setText(QStringLiteral("<b><span style='color:%1'>● 已停止</span></b>")
                                  .arg(theme().textMuted.name()));
    }
    toggleButton_->setText(running ? QStringLiteral("停止") : QStringLiteral("啟動"));

    bindCombo_->setEnabled(!running);
    portSpin_->setEnabled(!running);
}

void ApiPanelDialog::toggleServer() {
    if (api_->isRunning()) {
        api_->stop();
        refreshStatus();
        return;
    }

    if (settings_->apiKeys.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("還沒有 API 金鑰"),
                             QStringLiteral("請先產生至少一組 API 金鑰。沒有金鑰的話，"
                                            "所有需要認證的端點都會直接拒絕請求。"));
        return;
    }

    if (settings_->apiBindAddress == QStringLiteral("0.0.0.0")) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("要開放給網路嗎？"),
            QStringLiteral("綁定 0.0.0.0 之後，網路上任何連得到這台電腦又持有 API 金鑰的人，"
                       "都能操作它的滑鼠和鍵盤。\n\n要繼續嗎？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    std::string error;
    if (!api_->start(settings_->toServerConfig(), error)) {
        QMessageBox::critical(this, QStringLiteral("無法啟動伺服器"),
                              QString::fromStdString(error));
        return;
    }
    refreshStatus();
}

void ApiPanelDialog::generateKey() {
    const QString key = QString::fromStdString(server::ApiServer::generateApiKey());
    settings_->apiKeys.append(key);
    emit settingsChanged();
    refreshKeys();

    QGuiApplication::clipboard()->setText(key);
    QMessageBox::information(
        this, QStringLiteral("已產生金鑰"),
        QStringLiteral("新金鑰已複製到剪貼簿：\n\n%1\n\n"
                       "如果伺服器正在執行，請重新啟動它才會生效。")
            .arg(key));
}

void ApiPanelDialog::copySelectedKey() {
    auto* item = keyList_->currentItem();
    if (!item) return;
    QGuiApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
}

void ApiPanelDialog::revokeSelectedKey() {
    auto* item = keyList_->currentItem();
    if (!item) return;

    const QString key = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, QStringLiteral("要撤銷這組金鑰嗎？"),
                              QStringLiteral("任何還在用這組金鑰的呼叫端，之後都會收到 401。")) !=
        QMessageBox::Yes) {
        return;
    }

    settings_->apiKeys.removeAll(key);
    emit settingsChanged();
    refreshKeys();
}

void ApiPanelDialog::refreshKeys() {
    keyList_->clear();
    for (const QString& key : settings_->apiKeys) {
        // Show only the tail; the full value stays retrievable via Copy.
        const QString masked = key.length() > 12
                                   ? key.left(7) + QStringLiteral("****") + key.right(4)
                                   : key;
        auto* item = new QListWidgetItem(masked, keyList_);
        item->setData(Qt::UserRole, key);
    }
}

void ApiPanelDialog::refreshScripts() {
    scriptList_->clear();
    for (const auto& published : repository_->list()) {
        const QString id = QString::fromStdString(published.id);

        // Spell out the parameters. Publishing takes a snapshot, so the flow open
        // on the canvas and the one the API runs can differ -- and the difference
        // that bites is exactly this one: a variable added after publishing does
        // not exist for callers, and the copied command shows an empty object with
        // nothing to explain why.
        QStringList names;
        for (const auto& [name, value] : published.script.variables) {
            names << QString::fromStdString(name);
        }
        const QString parameters = names.isEmpty()
                                       ? QStringLiteral("無參數")
                                       : QStringLiteral("參數：%1").arg(names.join(QStringLiteral(", ")));

        QString label = QStringLiteral("✔  %1   —   %2 個積木   ·   %3   ·   %4")
                            .arg(id)
                            .arg(published.script.steps.size())
                            .arg(parameters, QString::fromStdString(published.publishedAt));

        if (isStale(published)) {
            label += QStringLiteral("        ⚠ 畫布上的版本已修改，尚未重新發佈");
        }

        auto* item = new QListWidgetItem(label, scriptList_);
        item->setData(Qt::UserRole, id);
        if (isStale(published)) item->setForeground(theme().warning);
    }
}

bool ApiPanelDialog::isStale(const server::PublishedScript& published) const {
    if (currentFlow_.steps.empty() && currentFlow_.name.empty()) return false;

    // Matched by the id the open flow would publish under, since that is what a
    // re-publish would overwrite.
    const std::string wouldBe = server::ScriptRepository::makeId(currentFlow_.name);
    if (wouldBe != published.id) return false;

    return core::serializeScript(currentFlow_, true) !=
           core::serializeScript(published.script, true);
}

void ApiPanelDialog::setCurrentFlow(const core::Script& script) {
    currentFlow_ = script;
    refreshScripts();
}

QString ApiPanelDialog::endpointFor(const QString& scriptId) const {
    const int port = api_->isRunning() ? api_->boundPort() : settings_->apiPort;
    const QString host = settings_->apiBindAddress == QStringLiteral("0.0.0.0")
                             ? QStringLiteral("<this-machine>")
                             : settings_->apiBindAddress;
    return QStringLiteral("http://%1:%2/api/v1/scripts/%3/run").arg(host).arg(port).arg(scriptId);
}

QString ApiPanelDialog::apiKeyOrPlaceholder() const {
    return settings_->apiKeys.isEmpty() ? QStringLiteral("<your-api-key>")
                                        : settings_->apiKeys.first();
}

QString ApiPanelDialog::variablesJson(const QString& scriptId) const {
    // The flow's declared inputs with their defaults, so the copied command is a
    // filled-in template rather than an empty object the caller has to go and
    // research. This used to be a hardcoded `{}`.
    QStringList pairs;
    if (const auto published = repository_->find(scriptId.toStdString())) {
        for (const auto& [name, value] : published->script.variables) {
            QString escaped = QString::fromStdString(value);
            escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            pairs << QStringLiteral("\"%1\":\"%2\"").arg(QString::fromStdString(name), escaped);
        }
    }
    return QStringLiteral("{\"variables\":{%1}}").arg(pairs.join(QLatin1Char(',')));
}

QString ApiPanelDialog::curlFor(const QString& scriptId) const {
    // curl.exe, not curl: in PowerShell `curl` is an alias for Invoke-WebRequest,
    // so the bare name does not run curl at all. The JSON is double-quoted with
    // backslash-escaped quotes rather than wrapped in single quotes, which cmd
    // passes through literally and the server then rejects as invalid JSON.
    //
    // This form runs in cmd and in bash. It does *not* survive PowerShell's
    // parser, which is why there is a separate PowerShell button rather than one
    // command claiming to work everywhere.
    QString body = variablesJson(scriptId);
    body.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    return QStringLiteral(
               "curl.exe -X POST %1 "
               "-H \"X-API-Key: %2\" "
               "-H \"Content-Type: application/json\" "
               "-d \"%3\"")
        .arg(endpointFor(scriptId), apiKeyOrPlaceholder(), body);
}

QString ApiPanelDialog::powerShellFor(const QString& scriptId) const {
    // Single-quoted here, so PowerShell leaves the JSON alone, and sent as UTF-8
    // bytes: Invoke-RestMethod otherwise encodes the body in Latin-1 and any
    // Chinese value arrives at the server as mojibake.
    QString body = variablesJson(scriptId);
    body.replace(QStringLiteral("'"), QStringLiteral("''"));

    return QStringLiteral(
               "$body = '%1'\n"
               "Invoke-RestMethod -Method Post -Uri \"%2\" `\n"
               "  -Headers @{ \"X-API-Key\" = \"%3\" } `\n"
               "  -ContentType \"application/json; charset=utf-8\" `\n"
               "  -Body ([Text.Encoding]::UTF8.GetBytes($body))")
        .arg(body, endpointFor(scriptId), apiKeyOrPlaceholder());
}

QString ApiPanelDialog::parameterNote(const QString& scriptId) const {
    const auto published = repository_->find(scriptId.toStdString());
    if (!published) return {};

    QString note;

    if (published->script.variables.empty()) {
        // Answers "my flow has a url variable, so why is variables empty?" --
        // almost always because the variable was added after publishing.
        note += QStringLiteral(
            "<p>這支<b>已發佈的</b>流程沒有宣告參數，所以 <code>variables</code> 是空的。"
            "如果你剛在畫布上加了變數，按「發佈目前開啟的流程」重新發佈 —— "
            "發佈存的是當下的快照。</p>");
    } else {
        QStringList blank;
        QStringList filled;
        for (const auto& [name, value] : published->script.variables) {
            const QString display = QString::fromStdString(name);
            if (value.empty()) {
                blank << display;
            } else {
                filled << QStringLiteral("%1 = %2").arg(display,
                                                        QString::fromStdString(value));
            }
        }
        if (!filled.isEmpty()) {
            note += QStringLiteral("<p>已帶入流程的預設值：<code>%1</code></p>")
                        .arg(filled.join(QStringLiteral("</code>, <code>")));
        }
        if (!blank.isEmpty()) {
            // The command is copy-and-run only if the values are usable; an empty
            // default is not, and saying so beats letting the flow type nothing.
            note += QStringLiteral(
                        "<p><b>這些參數沒有預設值，複製後請自己填：</b> <code>%1</code>"
                        "<br>（流程變數的預設值可以在「編輯 → 流程變數…」設定，"
                        "設好再重新發佈就會出現在這裡。）</p>")
                        .arg(blank.join(QStringLiteral("</code>, <code>")));
        }
    }

    if (isStale(*published)) {
        note += QStringLiteral(
            "<p><b>⚠ 畫布上的版本和已發佈的不一樣。</b>這條指令跑的是已發佈的那份。</p>");
    }
    return note;
}

void ApiPanelDialog::copyCurlForSelectedScript() {
    auto* item = scriptList_->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    const QString command = curlFor(id);
    QGuiApplication::clipboard()->setText(command);

    const QString note =
        QStringLiteral("<p>貼到 <b>cmd</b> 或 <b>bash</b> 執行。PowerShell 請改用"
                       "「複製 PowerShell」—— 它的剖析器會把這裡的引號逃脫吃掉。</p>") +
        parameterNote(id);

    CommandPreviewDialog(QStringLiteral("已複製 curl 指令"), command, note, this).exec();
}

void ApiPanelDialog::copyPowerShellForSelectedScript() {
    auto* item = scriptList_->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    const QString command = powerShellFor(id);
    QGuiApplication::clipboard()->setText(command);

    CommandPreviewDialog(QStringLiteral("已複製 PowerShell 指令"), command,
                         parameterNote(id), this)
        .exec();
}

void ApiPanelDialog::unpublishSelectedScript() {
    auto* item = scriptList_->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, QStringLiteral("要取消發佈 %1 嗎？").arg(id),
                              QStringLiteral("之後就無法透過 API 執行它，"
                                         "而且它的檔案會從發佈資料夾刪除。")) !=
        QMessageBox::Yes) {
        return;
    }

    std::string error;
    if (!repository_->unpublish(id.toStdString(), error)) {
        QMessageBox::warning(this, QStringLiteral("取消發佈失敗"), QString::fromStdString(error));
    }
    refreshScripts();
}

void ApiPanelDialog::refreshRuns() {
    const std::vector<server::RunRecord> records = runStore_->list(200, 0);

    runTable_->setRowCount(static_cast<int>(records.size()));
    for (int row = 0; row < static_cast<int>(records.size()); ++row) {
        const server::RunRecord& record = records[static_cast<size_t>(row)];

        auto set = [this, row](int column, const QString& text) {
            runTable_->setItem(row, column, new QTableWidgetItem(text));
        };

        set(0, QString::fromStdString(record.runId));
        set(1, QString::fromStdString(record.scriptId));

        auto* statusItem = new QTableWidgetItem(statusText(record.status));
        statusItem->setForeground(statusColour(record.status));
        if (!record.error.empty()) {
            statusItem->setToolTip(QString::fromStdString(record.error));
            statusItem->setText(statusText(record.status) +
                                QStringLiteral("（%1）").arg(QString::fromStdString(record.failedStepId)));
        }
        runTable_->setItem(row, 2, statusItem);

        set(3, record.startedAt ? QString::fromStdString(server::toIso8601(*record.startedAt))
                                : QString());
        set(4, record.finishedAt ? QString::fromStdString(server::toIso8601(*record.finishedAt))
                                 : QString());
        set(5, QString::fromStdString(record.source));
    }
    runTable_->resizeColumnsToContents();
}

}  // namespace rpa::studio
