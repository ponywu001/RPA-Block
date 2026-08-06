#include "CommandPreviewDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace rpa::studio {

CommandPreviewDialog::CommandPreviewDialog(const QString& title,
                                           const QString& command,
                                           const QString& note,
                                           QWidget* parent)
    : QDialog(parent), command_(command) {
    setWindowTitle(title);
    resize(760, note.isEmpty() ? 300 : 400);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("已複製到剪貼簿。下面是完整內容，可以直接選取。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    view_ = new QPlainTextEdit(command, this);
    view_->setReadOnly(true);
    // Wrapped, so a long single-line curl command is fully visible rather than
    // running off the edge -- which is what hid the -d argument.
    view_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(view_, 1);

    if (!note.isEmpty()) {
        auto* noteLabel = new QLabel(note, this);
        noteLabel->setWordWrap(true);
        noteLabel->setTextFormat(Qt::RichText);
        layout->addWidget(noteLabel);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copyAgain = buttons->addButton(QStringLiteral("再複製一次"),
                                         QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(copyAgain, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(command_); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

}  // namespace rpa::studio
