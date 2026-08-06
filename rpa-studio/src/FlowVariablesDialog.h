#pragma once

#include <QDialog>
#include <map>
#include <string>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace rpa::studio {

/// Editor for a flow's inputs.
///
/// Serves two jobs that look the same and are not:
///
///   Declare — edit the defaults stored in the flow. These travel with the file
///             and are what the REST API advertises to callers.
///   Supply  — fill in values for one run, leaving the declared defaults alone.
///             This is the "ask me each time" case: a password, an order number,
///             a date that differs on every run.
///
/// Keeping both in one dialog means the names and the layout match whichever way
/// you arrive at it.
class FlowVariablesDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        Declare,  ///< editing the flow's own defaults
        Supply,   ///< collecting values for a single run
    };

    FlowVariablesDialog(Mode mode,
                        const std::map<std::string, std::string>& values,
                        QWidget* parent = nullptr);

    /// In Declare mode: the new set of defaults, including added and removed
    /// names. In Supply mode: the values to use for this run.
    std::map<std::string, std::string> values() const;

    /// Supply mode only: the user asked not to be prompted again this session.
    bool suppressFuturePrompts() const;

private:
    void addRow(const QString& name, const QString& value);
    void removeSelectedRow();

    Mode mode_;
    QTableWidget* table_;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QCheckBox* dontAskAgain_ = nullptr;
};

}  // namespace rpa::studio
