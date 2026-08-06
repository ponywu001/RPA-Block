#include "ChatDock.h"

#include "Theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <functional>
#include <utility>

namespace rpa::studio {

namespace {

QString escape(const QString& text) {
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

/// Submits on Enter and inserts a newline on Shift+Enter, which is what people
/// expect from a chat box.
class Composer : public QPlainTextEdit {
public:
    Composer(QWidget* parent, std::function<void()> onSubmit)
        : QPlainTextEdit(parent), onSubmit_(std::move(onSubmit)) {}

protected:
    void keyPressEvent(QKeyEvent* event) override {
        const bool isEnter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (isEnter && !(event->modifiers() & Qt::ShiftModifier)) {
            if (onSubmit_) onSubmit_();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    std::function<void()> onSubmit_;
};

}  // namespace

ChatDock::ChatDock(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    transcript_ = new QTextBrowser(this);
    transcript_->setOpenExternalLinks(false);
    layout->addWidget(transcript_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* draftRow = new QWidget(this);
    auto* draftLayout = new QHBoxLayout(draftRow);
    draftLayout->setContentsMargins(0, 0, 0, 0);
    previewButton_ = new QPushButton(QStringLiteral("👁  預覽差異"), draftRow);
    applyButton_ = new QPushButton(QStringLiteral("✔  套用到流程"), draftRow);
    previewButton_->setEnabled(false);
    applyButton_->setEnabled(false);
    draftLayout->addWidget(previewButton_);
    draftLayout->addWidget(applyButton_);
    draftLayout->addStretch(1);
    layout->addWidget(draftRow);

    auto* composerRow = new QWidget(this);
    auto* composerLayout = new QHBoxLayout(composerRow);
    composerLayout->setContentsMargins(0, 0, 0, 0);

    attachButton_ = new QPushButton(QStringLiteral("📷"), composerRow);
    attachButton_->setCheckable(true);
    attachButton_->setToolTip(QStringLiteral("下一則訊息附上目前畫面"));
    attachButton_->setFixedWidth(36);
    composerLayout->addWidget(attachButton_);

    composer_ = new Composer(composerRow, [this] { submit(); });
    composer_->setPlaceholderText(
        QStringLiteral("描述你想要的流程。Enter 送出，Shift+Enter 換行。"));
    composer_->setMaximumHeight(90);
    composerLayout->addWidget(composer_, 1);

    sendButton_ = new QPushButton(QStringLiteral("➤"), composerRow);
    sendButton_->setFixedWidth(36);
    composerLayout->addWidget(sendButton_);

    cancelButton_ = new QPushButton(QStringLiteral("停止"), composerRow);
    cancelButton_->setVisible(false);
    composerLayout->addWidget(cancelButton_);

    layout->addWidget(composerRow);

    connect(sendButton_, &QPushButton::clicked, this, &ChatDock::submit);
    connect(attachButton_, &QPushButton::toggled, this,
            [this](bool on) { attachScreenshot_ = on; });
    connect(previewButton_, &QPushButton::clicked, this,
            [this] { emit previewDraftRequested(); });
    connect(applyButton_, &QPushButton::clicked, this, [this] { emit applyDraftRequested(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { emit cancelRequested(); });

    showIntro();
}

void ChatDock::showIntro() {
    appendSystemNote(
        QStringLiteral("說明你想要的流程，或是錄一段實際操作再交給我整理。\n"
                       "產生的草稿一定會先讓你預覽，確認後才會動到畫布。"));
}

void ChatDock::submit() {
    const QString text = composer_->toPlainText().trimmed();
    if (text.isEmpty()) return;

    emit sendRequested(text, attachScreenshot_);
    composer_->clear();
}

void ChatDock::appendUserMessage(const QString& text, bool hasScreenshot) {
    QString html = QStringLiteral("<p><b style='color:%1'>你</b><br>%2")
                       .arg(theme().accent.name(), escape(text));
    if (hasScreenshot) html += QStringLiteral("<br><i>（已附上目前畫面）</i>");
    html += QStringLiteral("</p>");
    transcript_->append(html);

    ai::ChatMessage message;
    message.role = ai::ChatMessage::Role::User;
    message.text = text.toStdString();
    history_.push_back(std::move(message));
}

void ChatDock::appendAssistantReply(const ai::AgentReply& reply) {
    QString html = QStringLiteral("<p><b style='color:%1'>AI 助手</b><br>%2")
                       .arg(theme().success.name(),
                            escape(QString::fromStdString(reply.reply)));

    if (reply.hasScript) {
        html += QStringLiteral("<br><i>草稿：%1 個積木%2</i>")
                    .arg(reply.steps.size())
                    .arg(reply.scriptName.empty()
                             ? QString()
                             : QStringLiteral("，流程名稱「%1」").arg(QString::fromStdString(reply.scriptName)));
    }
    if (!reply.stepIssues.empty()) {
        html += QStringLiteral("<br><span style='color:%1'>有 %2 個積木無法還原：</span><ul>")
                    .arg(theme().danger.name())
                    .arg(reply.stepIssues.size());
        for (const auto& issue : reply.stepIssues) {
            html += QStringLiteral("<li>%1</li>").arg(escape(QString::fromStdString(issue)));
        }
        html += QStringLiteral("</ul>");
    }
    if (reply.costUsd > 0.0) {
        html += QStringLiteral("<br><small>花費 US$%1 · 輸入 %2／輸出 %3 token</small>")
                    .arg(reply.costUsd, 0, 'f', 4)
                    .arg(reply.inputTokens)
                    .arg(reply.outputTokens);
    }
    html += QStringLiteral("</p>");
    transcript_->append(html);

    ai::ChatMessage message;
    message.role = ai::ChatMessage::Role::Assistant;
    message.text = reply.reply;
    history_.push_back(std::move(message));

    // Only a draft with at least one usable step is worth offering to apply.
    hasPendingDraft_ = reply.hasScript && !reply.steps.empty();
    pendingSteps_ = reply.steps;
    pendingScriptName_ = QString::fromStdString(reply.scriptName);

    previewButton_->setEnabled(hasPendingDraft_);
    applyButton_->setEnabled(hasPendingDraft_);

    if (reply.hasScript && reply.steps.empty()) {
        statusLabel_->setText(
            QStringLiteral("<span style='color:%1'>AI 說它產生了流程，但沒有一個積木"
                           "能還原，所以沒有東西可以套用。</span>")
                .arg(theme().danger.name()));
    } else {
        statusLabel_->clear();
    }
}

void ChatDock::appendSystemNote(const QString& text) {
    transcript_->append(QStringLiteral("<p style='color:%1'><i>%2</i></p>")
                            .arg(theme().textMuted.name(), escape(text)));
}

void ChatDock::setBusy(bool busy) {
    sendButton_->setEnabled(!busy);
    composer_->setReadOnly(busy);
    cancelButton_->setVisible(busy);
    statusLabel_->setText(busy ? QStringLiteral("正在等 AI 回覆…") : QString());
}

void ChatDock::clearHistory() {
    history_.clear();
    transcript_->clear();
    clearPendingDraft();
    // Back to the initial state, not to a blank void. The window opens on a new
    // flow, which clears the transcript, so without this the intro the
    // constructor wrote was wiped before it was ever on screen -- and an empty
    // transcript is exactly when that hint is worth reading.
    showIntro();
}

bool ChatDock::attachScreenshotRequested() const {
    return attachScreenshot_;
}

void ChatDock::clearAttachScreenshot() {
    attachScreenshot_ = false;
    attachButton_->setChecked(false);
}

void ChatDock::clearPendingDraft() {
    hasPendingDraft_ = false;
    pendingSteps_.clear();
    pendingScriptName_.clear();
    previewButton_->setEnabled(false);
    applyButton_->setEnabled(false);
}

}  // namespace rpa::studio
