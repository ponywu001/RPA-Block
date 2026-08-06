#pragma once

#include <QDialog>
#include <QWidget>

#include "rpa/core/Script.h"
#include "rpa/recorder/RecordedEvent.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;

namespace rpa::studio {

/// The small always-on-top bar shown while recording, since the main window is
/// minimised out of the way.
class RecordingOverlayBar : public QWidget {
    Q_OBJECT

public:
    explicit RecordingOverlayBar(QWidget* parent = nullptr);

    void setEventCount(int count);
    void setElapsedSeconds(int seconds);
    void setPaused(bool paused);

signals:
    void pauseToggled(bool paused);
    void stopRequested();

private:
    QLabel* statusLabel_;
    QLabel* countLabel_;
    QPushButton* pauseButton_;
    QPushButton* stopButton_;
    bool paused_ = false;
};

/// Screen 2's result view: the captured events, with a chance to trim them
/// before handing the recording to the assistant.
class RecordingResultDialog : public QDialog {
    Q_OBJECT

public:
    RecordingResultDialog(const recorder::RecordingSession& session, QWidget* parent = nullptr);

    /// The session minus whatever the user deleted.
    recorder::RecordingSession trimmedSession() const;

signals:
    /// The user chose to convert the (trimmed) recording into a flow.
    void handOffToAssistantRequested();

private:
    void buildUi();
    void refreshTable();
    void deleteSelectedRows();

    recorder::RecordingSession session_;
    QTableWidget* table_;
    QLabel* previewLabel_;
    QLabel* summaryLabel_;
};

/// Shows what applying an assistant draft would change, before it is applied.
class DraftPreviewDialog : public QDialog {
    Q_OBJECT

public:
    DraftPreviewDialog(const core::StepList& current,
                       const core::StepList& proposed,
                       QWidget* parent = nullptr);

    /// True when the user pressed Apply rather than Cancel.
    bool applyRequested() const { return apply_; }

private:
    bool apply_ = false;
};

}  // namespace rpa::studio
