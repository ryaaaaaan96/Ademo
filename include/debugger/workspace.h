#pragma once
#include "sdk.h"
#include <QObject>

namespace debugger {
class Workbench;
class PluginCatalog;
struct WorkspaceOperation {
    quint64 id = 0;
    QString state;
    QString detail;
    QString path;
};
class WorkspaceController final : public QObject {
public:
    using Executor = std::function<void(std::function<void()>)>;
    WorkspaceController(Workbench &, std::shared_ptr<PluginCatalog>, Executor = {});
    quint64 open(const QString &path);
    quint64 save(const QString &path);
    WorkspaceOperation operation(quint64) const;
    bool restoreLayout();
private:
    quint64 begin(const QString &path, bool writing);
    QJsonObject capture(const QString &directory) const;
    bool apply(const QJsonObject &, const QString &directory, QString &error);
    void applyLayout(const QJsonObject &, bool restoreOpen);
    void complete(quint64 id, const QString &state, const QString &detail);
    Workbench &window_;
    std::shared_ptr<PluginCatalog> catalog_;
    Executor executor_;
    QJsonObject rawRoot_;
    QList<WorkspaceOperation> operations_;
    quint64 nextId_ = 1;
    bool busy_ = false;
};
}
