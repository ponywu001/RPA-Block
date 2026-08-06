#include "RecordingDialogs.h"

#include "Theme.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "rpa/core/ScriptIO.h"

namespace rpa::studio {

// ---------------------------------------------------------------------------
// RecordingOverlayBar
// ---------------------------------------------------------------------------

RecordingOverlayBar::RecordingOverlayBar(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);

    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);

    statusLabel_ = new QLabel(QStringLiteral("<b style='color:%1'>● 錄製中 00:00</b>")
                                  .arg(theme().danger.name()),
                              topRow);
    topLayout->addWidget(statusLabel_);
    topLayout->addStretch(1);
    countLabel_ = new QLabel(QStringLiteral("0 個操作"), topRow);
    topLayout->addWidget(countLabel_);
    layout->addWidget(topRow);

    auto* buttonRow = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    pauseButton_ = new QPushButton(QStringLiteral("⏸  暫停"), buttonRow);
    stopButton_ = new QPushButton(QStringLiteral("⏹  停止（Esc）"), buttonRow);
    buttonLayout->addWidget(pauseButton_);
    buttonLayout->addWidget(stopButton_);
    layout->addWidget(buttonRow);

    setFixedWidth(300);

    connect(pauseButton_, &QPushButton::clicked, this, [this] {
        paused_ = !paused_;
        setPaused(paused_);
        emit pauseToggled(paused_);
    });
    connect(stopButton_, &QPushButton::clicked, this, [this] { emit stopRequested(); });
}

void RecordingOverlayBar::setEventCount(int count) {
    countLabel_->setText(QStringLiteral("%1 個操作").arg(count));
}

void RecordingOverlayBar::setElapsedSeconds(int seconds) {
    const QString clock = QStringLiteral("%1:%2")
                              .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                              .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    statusLabel_->setText(paused_
                              ? QStringLiteral("<b style='color:%1'>⏸ 已暫停 %2</b>")
                                    .arg(theme().textMuted.name(), clock)
                              : QStringLiteral("<b style='color:%1'>● 錄製中 %2</b>")
                                    .arg(theme().danger.name(), clock));
}

void RecordingOverlayBar::setPaused(bool paused) {
    paused_ = paused;
    pauseButton_->setText(paused ? QStringLiteral("▶  繼續") : QStringLiteral("⏸  暫停"));
}

// ---------------------------------------------------------------------------
// RecordingResultDialog
// ---------------------------------------------------------------------------

RecordingResultDialog::RecordingResultDialog(const recorder::RecordingSession& session,
                                             QWidget* parent)
    : QDialog(parent), session_(session) {
    setWindowTitle(QStringLiteral("錄製結果"));
    resize(940, 620);
    buildUi();
    refreshTable();
}

void RecordingResultDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    layout->addWidget(summaryLabel_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    table_ = new QTableWidget(splitter);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("操作"), QStringLiteral("元素"), QStringLiteral("視窗"), QStringLiteral("時間")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(table_);

    auto* previewPane = new QWidget(splitter);
    auto* previewLayout = new QVBoxLayout(previewPane);
    previewLayout->addWidget(new QLabel(QStringLiteral("<b>截圖</b>"), previewPane));
    previewLabel_ = new QLabel(previewPane);
    previewLabel_->setMinimumSize(260, 260);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setFrameShape(QFrame::Box);
    previewLabel_->setText(QStringLiteral("點一列看它的截圖。"));
    previewLayout->addWidget(previewLabel_);
    previewLayout->addStretch(1);
    splitter->addWidget(previewPane);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
                if (row < 0 || row >= static_cast<int>(session_.events.size())) return;

                const QString path =
                    QString::fromStdString(session_.events[static_cast<size_t>(row)].screenshotPath);
                if (path.isEmpty() || !QFileInfo::exists(path)) {
                    previewLabel_->setPixmap(QPixmap());
                    previewLabel_->setText(QStringLiteral("這個操作沒有截圖。"));
                    return;
                }
                QPixmap image(path);
                previewLabel_->setPixmap(
                    image.scaled(previewLabel_->size(), Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation));
            });

    auto* buttonRow = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    auto* deleteButton = new QPushButton(QStringLiteral("🗑  刪除選取"), buttonRow);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch(1);

    auto* cancel = new QPushButton(QStringLiteral("丟棄這次錄製"), buttonRow);
    auto* handOff = new QPushButton(QStringLiteral("🤖  交給 AI 整理成流程"), buttonRow);
    handOff->setDefault(true);
    buttonLayout->addWidget(cancel);
    buttonLayout->addWidget(handOff);
    layout->addWidget(buttonRow);

    connect(deleteButton, &QPushButton::clicked, this,
            &RecordingResultDialog::deleteSelectedRows);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(handOff, &QPushButton::clicked, this, [this] {
        emit handOffToAssistantRequested();
        accept();
    });
}

void RecordingResultDialog::refreshTable() {
    summaryLabel_->setText(QStringLiteral("<b>錄到 %1 個操作。</b>交給 AI 之前，"
                                          "先把誤觸的刪掉 —— 雜訊越少，產生的流程越乾淨。")
                               .arg(session_.events.size()));

    table_->setRowCount(static_cast<int>(session_.events.size()));

    for (int row = 0; row < static_cast<int>(session_.events.size()); ++row) {
        const recorder::RecordedEvent& event = session_.events[static_cast<size_t>(row)];

        QString description;
        switch (event.type) {
            case recorder::RecordedEventType::MouseClick:
            case recorder::RecordedEventType::MouseDoubleClick:
                description = QStringLiteral("%1 %2 於 (%3, %4)")
                                  .arg(event.type == recorder::RecordedEventType::MouseDoubleClick
                                           ? QStringLiteral("雙擊")
                                           : QStringLiteral("點擊"))
                                  .arg(QString::fromStdString(core::toString(event.button)))
                                  .arg(event.position.x)
                                  .arg(event.position.y);
                break;
            case recorder::RecordedEventType::TextInput:
                description = QStringLiteral("輸入「%1」").arg(QString::fromStdString(event.text));
                break;
            case recorder::RecordedEventType::KeyCombo:
                description = QStringLiteral("按 %1").arg(QString::fromStdString(event.keys));
                break;
            case recorder::RecordedEventType::WindowChange:
                description = QStringLiteral("切換視窗");
                break;
        }

        QString element;
        if (!event.element.controlType.empty() || !event.element.name.empty()) {
            element = QStringLiteral("%1 \"%2\"")
                          .arg(QString::fromStdString(event.element.controlType),
                               QString::fromStdString(event.element.name));
        }

        table_->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table_->setItem(row, 1, new QTableWidgetItem(description));
        table_->setItem(row, 2, new QTableWidgetItem(element));
        table_->setItem(row, 3,
                        new QTableWidgetItem(QString::fromStdString(event.element.windowTitle)));
        table_->setItem(row, 4,
                        new QTableWidgetItem(QStringLiteral("%1 毫秒").arg(event.timestampMs)));
    }
    table_->resizeColumnsToContents();
}

void RecordingResultDialog::deleteSelectedRows() {
    QSet<int> rows;
    for (const auto& index : table_->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    if (rows.isEmpty()) return;

    // Erase from the back so earlier indices stay valid.
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted) {
        if (row >= 0 && row < static_cast<int>(session_.events.size())) {
            session_.events.erase(session_.events.begin() + row);
        }
    }
    refreshTable();
}

recorder::RecordingSession RecordingResultDialog::trimmedSession() const {
    return session_;
}

// ---------------------------------------------------------------------------
// DraftPreviewDialog
// ---------------------------------------------------------------------------

DraftPreviewDialog::DraftPreviewDialog(const core::StepList& current,
                                       const core::StepList& proposed,
                                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("預覽 AI 草稿"));
    resize(940, 640);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        QStringLiteral("<b>套用這份草稿會整個換掉現在的流程。</b>"
                       "請先比對左右兩邊，確認之後再套用。"),
        this));

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto describe = [](const core::StepList& steps) {
        core::Script wrapper;
        wrapper.name = "flow";
        wrapper.steps = steps;
        return QString::fromStdString(core::serializeScript(wrapper, true));
    };

    auto* currentPane = new QWidget(splitter);
    auto* currentLayout = new QVBoxLayout(currentPane);
    currentLayout->addWidget(new QLabel(QStringLiteral("<b>目前的流程 — %1 個積木</b>").arg(current.size()),
                                        currentPane));
    auto* currentText = new QPlainTextEdit(currentPane);
    currentText->setReadOnly(true);
    currentText->setPlainText(current.empty() ? QStringLiteral("（目前沒有任何積木）") : describe(current));
    currentLayout->addWidget(currentText);
    splitter->addWidget(currentPane);

    auto* proposedPane = new QWidget(splitter);
    auto* proposedLayout = new QVBoxLayout(proposedPane);
    proposedLayout->addWidget(
        new QLabel(QStringLiteral("<b>AI 的草稿 — %1 個積木</b>").arg(proposed.size()), proposedPane));
    auto* proposedText = new QPlainTextEdit(proposedPane);
    proposedText->setReadOnly(true);
    proposedText->setPlainText(describe(proposed));
    proposedLayout->addWidget(proposedText);
    splitter->addWidget(proposedPane);

    layout->addWidget(splitter, 1);

    // Surface validation problems here rather than at run time, so a bad draft
    // is visible before it replaces working steps.
    core::Script candidate;
    candidate.name = "draft";
    candidate.steps = proposed;
    const auto issues = core::validate(candidate);
    if (!issues.empty()) {
        QStringList lines;
        for (const auto& issue : issues) {
            lines << QStringLiteral("• %1%2")
                         .arg(issue.stepId.empty()
                                  ? QString()
                                  : QStringLiteral("[%1] ").arg(
                                        QString::fromStdString(issue.stepId)))
                         .arg(QString::fromStdString(issue.message));
        }
        auto* warning = new QLabel(
            QStringLiteral("<span style='color:%1'><b>這份草稿有 %2 個問題：</b></span><br>%3")
            .arg(theme().danger.name())
                .arg(issues.size())
                .arg(lines.join(QStringLiteral("<br>"))),
            this);
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }

    auto* buttons = new QDialogButtonBox(this);
    auto* applyButton = buttons->addButton(QStringLiteral("✔  套用到流程"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("保留目前的流程"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(applyButton, &QPushButton::clicked, this, [this] {
        apply_ = true;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

}  // namespace rpa::studio
