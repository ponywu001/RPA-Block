#include "FlowVariablesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace rpa::studio {

FlowVariablesDialog::FlowVariablesDialog(Mode mode,
                                         const std::map<std::string, std::string>& values,
                                         QWidget* parent)
    : QDialog(parent), mode_(mode) {
    setWindowTitle(mode == Mode::Declare ? QStringLiteral("流程變數")
                                         : QStringLiteral("這次執行要用的值"));
    resize(560, 380);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    hint->setText(mode == Mode::Declare
                      ? QStringLiteral(
                            "在積木的文字欄位裡寫 <code>{{變數名}}</code>，執行時就會換成這裡的值。"
                            "<br>這些預設值會跟著流程檔一起走，也是 REST API 對外公告的參數。"
                            "<br><b>不要把密碼寫在這裡</b> —— 留空，改用每次執行時填入或由 API 帶進來。")
                      : QStringLiteral(
                            "這次執行要用的值。<b>不會</b>改到流程本身的預設值。"));
    layout->addWidget(hint);

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({QStringLiteral("變數名稱"), QStringLiteral("值")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);

    for (const auto& [name, value] : values) {
        addRow(QString::fromStdString(name), QString::fromStdString(value));
    }

    if (mode == Mode::Declare) {
        auto* buttonRow = new QWidget(this);
        auto* buttonLayout = new QHBoxLayout(buttonRow);
        buttonLayout->setContentsMargins(0, 0, 0, 0);

        addButton_ = new QPushButton(QStringLiteral("新增變數"), buttonRow);
        removeButton_ = new QPushButton(QStringLiteral("刪除"), buttonRow);
        buttonLayout->addWidget(addButton_);
        buttonLayout->addWidget(removeButton_);
        buttonLayout->addStretch(1);
        layout->addWidget(buttonRow);

        connect(addButton_, &QPushButton::clicked, this, [this] {
            addRow(QString(), QString());
            table_->editItem(table_->item(table_->rowCount() - 1, 0));
        });
        connect(removeButton_, &QPushButton::clicked, this,
                &FlowVariablesDialog::removeSelectedRow);
    } else {
        dontAskAgain_ = new QCheckBox(
            QStringLiteral("這次之後不要再問（直到重開程式）"), this);
        layout->addWidget(dontAskAgain_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void FlowVariablesDialog::addRow(const QString& name, const QString& value) {
    const int row = table_->rowCount();
    table_->insertRow(row);

    auto* nameItem = new QTableWidgetItem(name);
    if (mode_ == Mode::Supply) {
        // The set of inputs belongs to the flow; a run supplies values for it,
        // not new names.
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    }
    table_->setItem(row, 0, nameItem);
    table_->setItem(row, 1, new QTableWidgetItem(value));
}

void FlowVariablesDialog::removeSelectedRow() {
    const auto selected = table_->selectionModel()->selectedRows();
    // Back to front, so removing one does not shift the indices of the rest.
    QList<int> rows;
    for (const auto& index : selected) rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) table_->removeRow(row);
}

std::map<std::string, std::string> FlowVariablesDialog::values() const {
    std::map<std::string, std::string> out;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* nameItem = table_->item(row, 0);
        const QTableWidgetItem* valueItem = table_->item(row, 1);
        if (!nameItem) continue;

        const QString name = nameItem->text().trimmed();
        // A blank name is a row the user added and abandoned; dropping it beats
        // writing an unusable "" key into the flow.
        if (name.isEmpty()) continue;

        out[name.toStdString()] = valueItem ? valueItem->text().toStdString() : std::string{};
    }
    return out;
}

bool FlowVariablesDialog::suppressFuturePrompts() const {
    return dontAskAgain_ && dontAskAgain_->isChecked();
}

}  // namespace rpa::studio
