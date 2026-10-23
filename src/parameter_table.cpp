#include "debugger/parameter_table.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>

namespace debugger {
ParameterTable::ParameterTable(QWidget *parent) : QTableWidget(parent)
{
    setAccessibleName(QStringLiteral("参数表"));
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    verticalHeader()->hide();
}
void ParameterTable::setColumns(const QList<ParameterColumn> &columns)
{
    clear(); setRowCount(0); columns_ = columns;
    QStringList titles;
    for (const auto &column : columns_) titles << column.title;
    setColumnCount(titles.size()); setHorizontalHeaderLabels(titles);
}
void ParameterTable::setRows(const QList<ParameterRow> &rows)
{
    setRowCount(0);
    for (const auto &row : rows) {
        const int index = rowCount(); insertRow(index);
        for (int c = 0; c < columns_.size(); ++c) {
            auto *item = new QTableWidgetItem(row.cells.value(columns_[c].id));
            item->setData(Qt::UserRole, row.id); setItem(index, c, item);
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
        }
        setRowHeight(index, 36);
    }
}
void ParameterTable::updateRow(const ParameterRow &row)
{
    for (int r = 0; r < rowCount(); ++r) {
        if (!item(r, 0) || item(r, 0)->data(Qt::UserRole).toString() != row.id) continue;
        for (int c = 0; c < columns_.size(); ++c) {
            auto *cell = item(r, c);
            const auto text = row.cells.value(columns_[c].id);
            if (cell->text() != text) cell->setText(text);
            if (auto *container = cellWidget(r, c)) for (auto *button : container->findChildren<QPushButton *>())
                for (const auto &action : row.actions) if (button->property("parameterAction") == action.id)
                    button->setEnabled(action.enabled);
        }
        return;
    }
}
QStringList ParameterTable::selectedParameterIds() const
{
    QStringList ids;
    for (const auto &index : selectionModel()->selectedRows())
        ids << item(index.row(), 0)->data(Qt::UserRole).toString();
    return ids;
}
}
