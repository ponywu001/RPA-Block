#pragma once

#include <QWidget>
#include <vector>

#include "rpa/ai/AgentTypes.h"
#include "rpa/core/Script.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace rpa::studio {

/// Screen 1's AI dock: transcript, composer, and the apply gate.
///
/// A draft returned by the assistant is never written straight into the flow.
/// It is held here until the user previews the diff and presses apply, so an
/// unexpected reply cannot silently replace work in progress.
class ChatDock : public QWidget {
    Q_OBJECT

public:
    explicit ChatDock(QWidget* parent = nullptr);

    void appendUserMessage(const QString& text, bool hasScreenshot);
    void appendAssistantReply(const ai::AgentReply& reply);
    void appendSystemNote(const QString& text);

    /// Write the "here is what this panel does" note. Shown on construction and
    /// again whenever the transcript is cleared.
    void showIntro();
    void setBusy(bool busy);

    /// Local transcript, folded into each outgoing request.
    const std::vector<ai::ChatMessage>& history() const { return history_; }
    void clearHistory();

    /// True when the user asked for the current screen to be attached.
    bool attachScreenshotRequested() const;
    void clearAttachScreenshot();

    bool hasPendingDraft() const { return hasPendingDraft_; }
    const core::StepList& pendingSteps() const { return pendingSteps_; }
    QString pendingScriptName() const { return pendingScriptName_; }
    void clearPendingDraft();

signals:
    /// The composer was submitted. The host builds the full prompt and sends it.
    void sendRequested(const QString& text, bool attachScreenshot);
    void previewDraftRequested();
    void applyDraftRequested();
    void cancelRequested();

private:
    void submit();

    QTextBrowser* transcript_;
    QPlainTextEdit* composer_;
    QPushButton* sendButton_;
    QPushButton* attachButton_;
    QPushButton* previewButton_;
    QPushButton* applyButton_;
    QPushButton* cancelButton_;
    QLabel* statusLabel_;

    std::vector<ai::ChatMessage> history_;

    bool hasPendingDraft_ = false;
    core::StepList pendingSteps_;
    QString pendingScriptName_;
    bool attachScreenshot_ = false;
};

}  // namespace rpa::studio
