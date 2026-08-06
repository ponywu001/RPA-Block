#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;

namespace rpa::studio {

/// Shows a generated shell command in full.
///
/// Replaces a QMessageBox, which sizes itself to its text and simply cut a long
/// curl command off at the dialog edge -- taking the `-d` argument with it, so
/// the parameters looked absent when they were there all along. A wrapping,
/// selectable, monospace box shows the whole thing and lets it be read.
class CommandPreviewDialog : public QDialog {
    Q_OBJECT

public:
    /// `note` is rich text shown above the command; pass an empty string for none.
    CommandPreviewDialog(const QString& title,
                         const QString& command,
                         const QString& note,
                         QWidget* parent = nullptr);

private:
    QPlainTextEdit* view_;
    QString command_;
};

}  // namespace rpa::studio
