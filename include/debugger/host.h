#pragma once
#include "sdk.h"
#include <QObject>
#include <QPointer>
#include <QPluginLoader>
#include <QSet>
#include <vector>

namespace debugger {
// 注册表由会话和视图共同持有，确保 DLL 晚于其创建的对象销毁。
class PluginCatalog : public std::enable_shared_from_this<PluginCatalog> {
public:
    ~PluginCatalog();
    void discover(const QString &directory);
    QList<ProtocolPlugin *> protocols() const { return protocols_; }
    QList<TransportPlugin *> transports() const { return transports_; }
    QStringList diagnostics() const { return diagnostics_; }
    ProtocolPlugin *protocol(const QString &) const;
    TransportPlugin *transport(const QString &) const;
    QWidget *createView(const QString &, QWidget *parent);
    bool acquire(const QString &key);
    void release(const QString &key);
private:
    std::vector<std::unique_ptr<QPluginLoader>> loaders_;
    QList<ProtocolPlugin *> protocols_;
    QList<TransportPlugin *> transports_;
    QStringList diagnostics_;
    QSet<QString> occupied_;
};
enum class SessionState { Stopped, Starting, Running, Error };
QString stateText(SessionState);
class DebugSession : public QObject {
    Q_OBJECT
public:
    using EndpointFactory = std::function<std::unique_ptr<Endpoint>(const QJsonObject &, EndpointEvents)>;
    explicit DebugSession(std::shared_ptr<PluginCatalog>, QObject *parent = nullptr,
                          EndpointFactory endpointFactory = {});
    ~DebugSession() override;
    bool start(const SessionConfig &);
    void stop();
    SessionState state() const { return state_; }
    QString diagnostic() const { return diagnostic_; }
    quint64 generation() const { return generation_; }
    SessionConfig config() const { return config_; }
signals:
    void changed();
private:
    void opened(quint64, bool, const QString &);
    void fail(const QString &);
    void retire();
    std::shared_ptr<PluginCatalog> catalog_;
    std::unique_ptr<ProtocolSession> protocol_;
    std::unique_ptr<Endpoint> endpoint_;
    SessionConfig config_;
    SessionState state_ = SessionState::Stopped;
    QString diagnostic_;
    QString key_;
    quint64 generation_ = 0;
    EndpointFactory endpointFactory_;
};
class Workbench : public QWidget {
public:
    explicit Workbench(std::shared_ptr<PluginCatalog>, QWidget *parent = nullptr,
                       DebugSession::EndpointFactory endpointFactory = {});
    ~Workbench() override;
private:
    std::shared_ptr<PluginCatalog> catalog_;
    DebugSession session_;
    QWidget *view_ = nullptr;
};
}
