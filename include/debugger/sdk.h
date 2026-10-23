#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QWidget>
#include <QDateTime>
#include <functional>
#include <memory>
#include <optional>

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
    QJsonObject recovery; // 宿主通用重连策略，与协议字段分开保存。
};
struct OperationRequest {
    quint64 id = 0;
    QString action;
    QJsonObject parameters;
};
struct OperationUpdate {
    quint64 id = 0;
    QString state;
    QString detail;
    QJsonObject result;
};
struct TraceRecord {
    quint64 id = 0;
    quint64 generation = 0; // 连接/启停代次；重连不会替换协议运行实例。
    quint64 operationId = 0; // RX 未经协议确认归属时为 0。
    qint64 monotonicUs = 0;
    QDateTime observedUtc;
    QString kind;
    QByteArray bytes;
    QString detail;
    std::optional<qint64> idleBeforeUs; // 仅后端确知的线路静默；QSerialPort 没有此精度。
    QJsonObject metadata; // 实际观测及宿主发送身份；不虚构后端时间精度。
};
struct TraceRange {
    quint64 recordId = 0;
    int offset = 0;
    int length = -1; // -1 表示旧插件未声明区间且来源已不可用。
};
struct Message {
    quint64 id = 0;
    quint64 operationId = 0;
    QList<quint64> sources;
    QString status;
    QString detail;
    QJsonObject fields;
    QList<TraceRange> sourceRanges;
    qint64 monotonicUs = 0;
    QDateTime observedUtc;
};
struct OperationSnapshot {
    OperationRequest request;
    OperationUpdate update;
    bool sendAccepted = false;
    bool localSent = false;
    bool finished = false;
    QList<quint64> observedRefs; // 操作观察窗口中的流量，不冒充已确认的响应归属。
    quint64 omittedObservedRefCount = 0;
    bool localOnly = false;
};
struct LocalState {
    bool retainAfterStop = false;
    bool dirty = false;
    bool busy = false;
    QStringList actions;
};
struct ProtocolServices {
    std::function<qint64()> nowUs;
    std::function<bool(quint64, QByteArray)> send;
    std::function<void(OperationUpdate)> publish;
    std::function<void(Message)> publishMessage;
    std::function<quint64(QString, QJsonObject)> submit; // 协议调度仍由宿主分配逻辑操作 ID。
    std::function<void(QJsonObject)> publishConfiguration; // 更新可保存配置，不由宿主解释协议字段。
    std::function<quint64(QByteArray)> sendPassive;
    // 后台任务仅处理值；完成回到会话上下文，并保留协议/DLL租约。
    std::function<bool(std::function<QJsonObject()>, std::function<void(QJsonObject)>)> background;
};
struct SessionAccess {
    std::function<quint64(QString, QJsonObject)> submit;
    std::function<OperationSnapshot(quint64)> operation;
    std::function<QList<TraceRecord>(quint64)> traces;
    std::function<QList<Message>(quint64)> messages;
    std::function<bool()> running;
    std::function<SessionConfig()> config;
    std::function<bool(QJsonObject)> configure;
    std::function<QJsonObject()> snapshot;
    std::function<void(std::function<bool()>)> setDiscardGuard;
};
struct ReceiveChunk {
    QByteArray bytes;
    std::optional<qint64> idleBeforeUs;
    QJsonObject metadata;
    ReceiveChunk(QByteArray data = {}, std::optional<qint64> idle = {})
        : bytes(std::move(data)), idleBeforeUs(idle) {}
};
struct EndpointEvents {
    std::function<void(bool, QString)> opened;
    std::function<void(QString)> failed;
    std::function<void(ReceiveChunk)> received;
    std::function<void(quint64, bool, QString)> sent;
};
class Endpoint {
public:
    virtual ~Endpoint() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool send(quint64 id, const QByteArray &) = 0; // id 为不透明发送令牌，不是逻辑操作 ID。
};
class ProtocolSession {
public:
    virtual ~ProtocolSession() = default;
    virtual QString prepare(const SessionConfig &) = 0;
    // 后台准备只能读取配置/文件，不得调用 ProtocolServices 或并发访问宿主。
    virtual bool prepareInBackground(const SessionConfig &) const { return false; }
    virtual QJsonObject snapshot() const { return {}; }
    virtual LocalState localState() const { return {}; }
    virtual void activate() = 0;
    virtual void stop() = 0;
    virtual void connectionLost(const QString &) {} // 丢弃连接交互，保留协议运行数据。
    virtual void connectionRestored(bool) {} // 仅恢复允许的后台读取，不重发用户写入。
    virtual void submit(const OperationRequest &) = 0;
    virtual void onInput(const TraceRecord &) = 0;
    virtual void onSent(quint64, bool, const QString &, qint64 observedUs) = 0;
    virtual void tick() = 0;
};
class ProtocolPlugin {
public:
    virtual ~ProtocolPlugin() = default;
    virtual Descriptor describe() const = 0;
    virtual std::unique_ptr<ProtocolSession> createSession(ProtocolServices) = 0;
    virtual QWidget *createView(SessionAccess, QWidget *parent) = 0;
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
#define DebuggerProtocolIid "org.ademo.debugger.Protocol/7.0"
#define DebuggerTransportIid "org.ademo.debugger.Transport/7.0"
Q_DECLARE_INTERFACE(debugger::ProtocolPlugin, DebuggerProtocolIid)
Q_DECLARE_INTERFACE(debugger::TransportPlugin, DebuggerTransportIid)
