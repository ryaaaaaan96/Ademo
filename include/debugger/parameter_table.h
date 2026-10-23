#pragma once
#include <QTableWidget>
#include <QMap>
#include <QHash>
#include <QPersistentModelIndex>
#include <functional>

namespace debugger {
struct ParameterColumn { QString id; QString title; };
struct ParameterAction { QString id; QString title; bool enabled = true; };
struct ParameterRow {
    QString id;
    QMap<QString, QString> cells;
    QList<ParameterAction> actions;
};
// 框架只承载插件给出的单元格和动作，不解释设备、地址、数据类型或组帧。
class ParameterTable : public QTableWidget {
public:
    explicit ParameterTable(QWidget *parent = nullptr);
    void setColumns(const QList<ParameterColumn> &);
    void setRows(const QList<ParameterRow> &);
    void updateRow(const ParameterRow &);
    QStringList selectedParameterIds() const;
    std::function<void(QString parameterId, QString actionId)> onAction;
private:
    QList<ParameterColumn> columns_;
    QHash<QString, QPersistentModelIndex> rowPositions_;
};
}
