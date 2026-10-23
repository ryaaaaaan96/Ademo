#include "debugger/parameter_table.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QTimer>

namespace debugger {
namespace {
class ParameterDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<void(QWidget *, QModelIndex)> opened;
    std::function<void(QModelIndex, QString)> committed;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override {
        auto *editor = new QLineEdit(parent);
        if (opened) opened(editor, index);
        return editor;
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        const auto text = static_cast<QLineEdit *>(editor)->text();
        QStyledItemDelegate::setModelData(editor, model, index);
        if (committed) committed(index, text);
    }
};
}
ParameterTable::ParameterTable(QWidget *parent) : QTableWidget(parent)
{
    setAccessibleName(QStringLiteral("参数表"));
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto *delegate = new ParameterDelegate(this);
    delegate->opened = [this](QWidget *editor, const QModelIndex &index) {
        editor_ = editor; editingIndex_ = index;
        const auto id = index.data(Qt::UserRole).toString(), column = columns_[index.column()].id;
        connect(static_cast<QLineEdit *>(editor), &QLineEdit::textEdited, this, [this, id, column](const QString &text) {
            if (onEdit) onEdit(id, column, text);
        });
    };
    delegate->committed = [this](const QModelIndex &index, const QString &text) {
        const auto id = index.data(Qt::UserRole).toString(), column = columns_[index.column()].id;
        QTimer::singleShot(0, this, [this, id, column, text] { if (onCommit) onCommit(id, column, text); });
    };
    setItemDelegate(delegate);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setMinimumSectionSize(65);
    setAlternatingRowColors(true);
    setMinimumHeight(170);
    verticalHeader()->hide();
}
void ParameterTable::setColumns(const QList<ParameterColumn> &columns)
{
    rowPositions_.clear(); clear(); setRowCount(0); columns_ = columns;
    QStringList titles;
    for (const auto &column : columns_) titles << column.title;
    setColumnCount(titles.size()); setHorizontalHeaderLabels(titles);
    for (int column = 0; column < columns_.size(); ++column) {
        const auto id = columns_[column].id;
        setColumnWidth(column, id == "description" ? 140 : id == "updated" ? 150 : 85);
        if (id == "actions") horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
}
void ParameterTable::setRows(const QList<ParameterRow> &rows)
{
    rowPositions_.clear(); setRowCount(0);
    for (const auto &row : rows) {
        const int index = rowCount(); insertRow(index);
        for (int c = 0; c < columns_.size(); ++c) {
            auto *item = new QTableWidgetItem(row.cells.value(columns_[c].id));
            item->setData(Qt::UserRole, row.id); setItem(index, c, item);
            updateCell(index, c, row);
            if (columns_[c].id != "actions") continue;
            auto *container = new QWidget(this);
            auto *layout = new QHBoxLayout(container); layout->setContentsMargins(2, 2, 2, 2);
            for (const auto &action : row.actions) {
                auto *button = new QPushButton(action.title, container);
                button->setProperty("parameterAction", action.id);
                button->setEnabled(action.enabled); layout->addWidget(button);
                connect(button, &QPushButton::clicked, this, [this, id = row.id, actionId = action.id] {
                    if (onAction) onAction(id, actionId);
                });
            }
            setCellWidget(index, c, container);
            container->setMinimumWidth(container->sizeHint().width());
        }
        setRowHeight(index, 36);
        if (columnCount()) rowPositions_.insert(row.id, model()->index(index, 0));
    }
}
void ParameterTable::updateRow(const ParameterRow &row)
{
    const auto position = rowPositions_.value(row.id);
    if (position.isValid()) {
        const int r = position.row();
        for (int c = 0; c < columns_.size(); ++c) {
            updateCell(r, c, row);
            if (auto *container = cellWidget(r, c)) for (auto *button : container->findChildren<QPushButton *>())
                for (const auto &action : row.actions) if (button->property("parameterAction") == action.id)
                    button->setEnabled(action.enabled);
        }
    }
}
void ParameterTable::updateCell(int r, int c, const ParameterRow &row)
{
    // 不向活动编辑器对应的模型写回刷新内容，避免 Qt 重置尚未提交的文本。
    if (editor_ && editingIndex_ == model()->index(r, c)) return;
    const auto column = columns_[c].id;
    auto *cell = item(r, c);
    const auto text = row.cells.value(column);
    if (cell->text() != text) cell->setText(text);
    const auto flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled
        | (row.editableCells.contains(column) ? Qt::ItemIsEditable : Qt::NoItemFlags);
    if (cell->flags() != flags) cell->setFlags(flags);
    const auto tooltip = row.tooltips.value(column);
    if (cell->toolTip() != tooltip) cell->setToolTip(tooltip);
    const QBrush background = row.pendingCells.contains(column) ? QBrush(QColor("#fff2b3")) : QBrush();
    if (cell->background() != background) cell->setBackground(background);
}
QStringList ParameterTable::selectedParameterIds() const
{
    QStringList ids;
    for (const auto &index : selectionModel()->selectedRows())
        ids << item(index.row(), 0)->data(Qt::UserRole).toString();
    return ids;
}
}
