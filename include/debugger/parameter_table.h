#pragma once
#include <QTableWidget>
#include <QMap>
#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <functional>

namespace debugger {
struct ParameterColumn { QString id; QString title; };
struct ParameterAction { QString id; QString title; bool enabled = true; };
struct ParameterRow {
    QString id;
    QMap<QString, QString> cells;
    QList<ParameterAction> actions;
    QSet<QString> editableCells;
    QSet<QString> pendingCells;
    QMap<QString, QString> tooltips;
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
    std::function<void(QString parameterId, QString columnId, QString text)> onEdit;
    // 完整编辑提交（取消编辑不触发）；交互回调在编辑器退出后排队执行。
    std::function<void(QString parameterId, QString columnId, QString text)> onCommit;
private:
    void updateCell(int row, int column, const ParameterRow &);
    QList<ParameterColumn> columns_;
    QHash<QString, QPersistentModelIndex> rowPositions_;
    QPointer<QWidget> editor_;
    QPersistentModelIndex editingIndex_;
};
}
