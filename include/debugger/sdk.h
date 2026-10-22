#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>

namespace debugger {
inline QString buildKey() { return QStringLiteral(DEBUGGER_BUILD_KEY); }
struct Field {
    QString key;
    QString label;
    QStringList choices; // 空列表表示可编辑文本。
    QString defaultValue;
};
struct Descriptor {
    QString id;
    QString name;
    QString build;
    QStringList roles;
    QStringList transports;
    QStringList capabilities;
    QList<Field> fields;
};
struct SessionConfig {
    QString protocolId;
    QString transportId;
    QString role;
    QJsonObject endpoint;
    QJsonObject protocol;
};
struct EndpointEvents {
    std::function<void(bool, QString)> opened;
    std::function<void(QString)> failed;
};
class Endpoint {
public:
    virtual ~Endpoint() = default;
    virtual void open() = 0;
    virtual void close() = 0;
};
class ProtocolSession {
public:
    virtual ~ProtocolSession() = default;
    virtual QString prepare(const SessionConfig &) = 0;
    virtual void activate() = 0;
    virtual void stop() = 0;
};
class ProtocolPlugin {
public:
    virtual ~ProtocolPlugin() = default;
    virtual Descriptor describe() const = 0;
    virtual std::unique_ptr<ProtocolSession> createSession() = 0;
    virtual QWidget *createView(QWidget *parent) = 0;
};
class TransportPlugin {
public:
    virtual ~TransportPlugin() = default;
    virtual Descriptor describe() const = 0;
    virtual QString validate(const QJsonObject &) const = 0;
    virtual QString endpointKey(const QJsonObject &) const = 0;
    virtual std::unique_ptr<Endpoint> createEndpoint(const QJsonObject &, EndpointEvents) = 0;
};
}
#define DebuggerProtocolIid "org.ademo.debugger.Protocol/1.0"
#define DebuggerTransportIid "org.ademo.debugger.Transport/1.0"
Q_DECLARE_INTERFACE(debugger::ProtocolPlugin, DebuggerProtocolIid)
Q_DECLARE_INTERFACE(debugger::TransportPlugin, DebuggerTransportIid)
