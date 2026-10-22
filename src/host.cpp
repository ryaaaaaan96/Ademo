#include "debugger/host.h"
#include <QDir>
#include <QTimer>

namespace debugger {
PluginCatalog::~PluginCatalog()
{
    protocols_.clear();
    transports_.clear();
    for (auto &loader : loaders_) loader->unload();
}
void PluginCatalog::discover(const QString &directory)
{
    // 仅启动时发现一次；不在运行中替换插件。
    if (!loaders_.empty()) return;
    for (const auto &file : QDir(directory).entryInfoList({"*.dll"}, QDir::Files, QDir::Name)) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        loader->setLoadHints(QLibrary::ResolveAllSymbolsHint);
        auto *object = loader->instance();
        if (!object) {
            diagnostics_ << file.fileName() + ": " + loader->errorString();
            continue;
        }
        auto *p = qobject_cast<ProtocolPlugin *>(object);
        auto *t = qobject_cast<TransportPlugin *>(object);
        if ((!p && !t) || (p && t)) {
            diagnostics_ << file.fileName() + QStringLiteral(": 插件接口无效");
            loader->unload();
            continue;
        }
        const auto d = p ? p->describe() : t->describe();
        if (d.id.isEmpty() || d.build != buildKey() || protocol(d.id) || transport(d.id)) {
            diagnostics_ << file.fileName() + QStringLiteral(": 插件身份冲突或 SDK/构建不兼容");
            loader->unload();
            continue;
        }
        if (p) protocols_ << p;
        if (t) transports_ << t;
        loaders_.push_back(std::move(loader));
    }
}
ProtocolPlugin *PluginCatalog::protocol(const QString &id) const
{
    for (auto *p : protocols_) if (p->describe().id == id) return p;
    return nullptr;
}
TransportPlugin *PluginCatalog::transport(const QString &id) const
{
    for (auto *t : transports_) if (t->describe().id == id) return t;
    return nullptr;
}
QWidget *PluginCatalog::createView(const QString &id, QWidget *parent, SessionAccess access)
{
    auto *p = protocol(id);
    auto *view = p ? p->createView(std::move(access), parent) : nullptr;
    if (view) QObject::connect(view, &QObject::destroyed, [lease = shared_from_this()] {});
    return view;
}
bool PluginCatalog::acquire(const QString &key)
{
    if (occupied_.contains(key)) return false;
    occupied_.insert(key);
    return true;
}
void PluginCatalog::release(const QString &key) { occupied_.remove(key); }
QString stateText(SessionState state)
{
    switch (state) {
    case SessionState::Stopped: return QStringLiteral("已停止");
    case SessionState::Starting: return QStringLiteral("正在打开");
    case SessionState::Running: return QStringLiteral("运行中");
    case SessionState::Error: return QStringLiteral("错误");
    }
    return {};
}
DebugSession::DebugSession(std::shared_ptr<PluginCatalog> catalog, QObject *parent,
                           EndpointFactory factory, std::function<qint64()> clock)
    : QObject(parent), catalog_(std::move(catalog)), endpointFactory_(std::move(factory)), clock_(std::move(clock))
{
    elapsed_.start();
    pollTimer_.setTimerType(Qt::PreciseTimer);
    pollTimer_.setInterval(1);
    connect(&pollTimer_, &QTimer::timeout, this, &DebugSession::poll);
    notificationTimer_.setSingleShot(true);
    notificationTimer_.setInterval(33);
    connect(&notificationTimer_, &QTimer::timeout, this, &DebugSession::changed);
}
DebugSession::~DebugSession() { retire(); }
void DebugSession::retire()
{
    ++generation_;
    pollTimer_.stop();
    for (auto &operation : operations_) {
        if (!operation.finished) {
            operation.finished = true;
            operation.update = {operation.request.id, "cancelled", QStringLiteral("会话已停止，操作已取消"), {}};
        }
    }
    if (protocol_) protocol_->stop();
    if (endpoint_) endpoint_->close();
    endpoint_.reset();
    protocol_.reset();
    if (!key_.isEmpty()) catalog_->release(key_);
    key_.clear();
}
void DebugSession::stop()
{
    retire();
    state_ = SessionState::Stopped;
    diagnostic_ = QStringLiteral("会话已停止，端点已释放");
    emit changed();
}
void DebugSession::fail(const QString &reason)
{
    for (auto &operation : operations_) if (!operation.finished) {
        operation.finished = true;
        operation.update = {operation.request.id, "transport-error", reason, {}};
    }
    retire();
    state_ = SessionState::Error;
    diagnostic_ = reason;
    emit changed();
}
bool DebugSession::start(const SessionConfig &config)
{
    if (state_ == SessionState::Starting || state_ == SessionState::Running) return false;
    retire();
    config_ = config;
    auto *p = catalog_->protocol(config.protocolId);
    auto *t = catalog_->transport(config.transportId);
    if (!p || !t) { fail(QStringLiteral("所需协议或传输插件不可用")); return false; }
    const auto pd = p->describe();
    const auto td = t->describe();
    if (!pd.roles.contains(config.role) || !pd.transports.contains(td.id)
        || !td.capabilities.contains("byte-stream")) {
        fail(QStringLiteral("不支持的协议、角色、传输或必要能力组合")); return false;
    }
    const auto error = t->validate(config.endpoint);
    if (!error.isEmpty()) { fail(error); return false; }
    const auto run = generation_;
    QPointer<DebugSession> self(this);
    ProtocolServices services;
    services.nowUs = [self] { return self ? self->nowUs() : qint64(0); };
    services.send = [self, run](quint64 id, QByteArray bytes) {
        return self && self->send(run, id, bytes);
    };
    services.publish = [self, run](OperationUpdate update) {
        if (self && self->generation_ == run) self->publish(std::move(update));
    };
    services.publishMessage = [self, run](Message message) {
        if (self && self->generation_ == run) self->publishMessage(std::move(message));
    };
    protocol_ = p->createSession(std::move(services));
    if (!protocol_) { fail(QStringLiteral("协议会话创建失败")); return false; }
    const auto preparation = protocol_->prepare(config);
    if (!preparation.isEmpty()) { fail(preparation); return false; }
    const auto key = t->endpointKey(config.endpoint);
    if (key.isEmpty() || !catalog_->acquire(key)) {
        fail(QStringLiteral("端点已被其他会话占用：") + key); return false;
    }
    key_ = key;
    state_ = SessionState::Starting;
    diagnostic_ = QStringLiteral("正在打开端点：") + key;
    // 总是排队处理，避免同步后端回调中销毁正在执行的端点。
    EndpointEvents events;
    events.opened = [self, run](bool ok, QString reason) {
        if (self) QTimer::singleShot(0, self, [self, run, ok, reason] {
            if (self) self->opened(run, ok, reason);
        });
    };
    events.failed = [self, run](QString reason) {
        if (self) QTimer::singleShot(0, self, [self, run, reason] {
            if (self && self->generation_ == run
                && (self->state_ == SessionState::Running || self->state_ == SessionState::Starting))
                self->fail(reason);
        });
    };
    events.received = [self, run](ReceiveChunk chunk) {
        if (!self) return;
        const auto observedUs = self->nowUs();
        QTimer::singleShot(0, self, [self, run, chunk = std::move(chunk), observedUs] {
            if (self) self->received(run, chunk, observedUs);
        });
    };
    events.sent = [self, run](quint64 id, bool ok, QString reason) {
        const auto observedUs = self ? self->nowUs() : 0;
        if (self) QTimer::singleShot(0, self, [self, run, id, ok, reason, observedUs] {
            if (self) self->sent(run, id, ok, reason, observedUs);
        });
    };
    endpoint_ = endpointFactory_ ? endpointFactory_(config.endpoint, std::move(events))
                                : t->createEndpoint(config.endpoint, std::move(events));
    if (!endpoint_) { fail(QStringLiteral("传输端点创建失败")); return false; }
    endpoint_->open();
    // open 的事件已经排队；通知后不再访问成员，允许观察者同步停止或销毁会话。
    emit changed();
    return true;
}
void DebugSession::opened(quint64 run, bool ok, const QString &reason)
{
    if (run != generation_ || state_ != SessionState::Starting) return;
    if (!ok) { fail(reason); return; }
    protocol_->activate();
    state_ = SessionState::Running;
    diagnostic_ = QStringLiteral("端点已打开，等待调试操作；本地发送不表示设备应答");
    if (!clock_) pollTimer_.start();
    emit changed();
}

qint64 DebugSession::nowUs() const { return clock_ ? clock_() : elapsed_.nsecsElapsed() / 1000; }
void DebugSession::poll() { if (state_ == SessionState::Running && protocol_) protocol_->tick(); }
quint64 DebugSession::appendTrace(const QString &kind, const QByteArray &bytes, quint64 operationId,
                                  const QString &detail, qint64 observedUs)
{
    const auto id = nextTraceId_++;
    traces_.append({id, generation_, operationId, observedUs < 0 ? nowUs() : observedUs,
                    QDateTime::currentDateTimeUtc(), kind, bytes, detail});
    if (traces_.size() > 256) { traces_.removeFirst(); ++expiredTraces_; }
    for (auto &operation : operations_) {
        if ((!operation.finished && (kind == "rx" || operation.request.id == operationId))
            || operation.request.id == operationId) {
            operation.observedRefs.append(id);
            if (operation.observedRefs.size() > 512) {
                operation.observedRefs.removeFirst();
                ++operation.omittedObservedRefCount;
            }
        }
    }
    if (!notificationTimer_.isActive()) notificationTimer_.start();
    return id;
}
void DebugSession::received(quint64 run, ReceiveChunk input, qint64 observedUs)
{
    if (run != generation_ || !protocol_ || (state_ != SessionState::Running && state_ != SessionState::Starting)) return;
    for (qsizetype offset = 0; offset < input.bytes.size(); offset += 4096) {
        const auto chunk = input.bytes.mid(offset, 4096);
        const auto id = appendTrace("rx", chunk, 0, {}, observedUs);
        traces_.last().idleBeforeUs = offset == 0 ? input.idleBeforeUs : std::nullopt;
        protocol_->onInput(*trace(id)); // 原始记录分配稳定身份后，才进入协议。
    }
}
bool DebugSession::send(quint64 run, quint64 id, const QByteArray &bytes)
{
    if (run != generation_ || state_ != SessionState::Running || !endpoint_ || bytes.size() > 256) return false;
    appendTrace("send-request", bytes, id, QStringLiteral("请求本地发送，尚未证明发送完成或设备应答"));
    const bool accepted = endpoint_->send(id, bytes);
    for (auto &operation : operations_) if (operation.request.id == id) operation.sendAccepted = accepted;
    appendTrace(accepted ? "send-accepted" : "send-rejected", {}, id);
    return accepted;
}
void DebugSession::sent(quint64 run, quint64 id, bool ok, const QString &reason, qint64 observedUs)
{
    if (run != generation_ || state_ != SessionState::Running || !protocol_) return;
    const auto operation = this->operation(id);
    if (!operation.sendAccepted || operation.localSent || operation.finished) return;
    appendTrace(ok ? "local-sent" : "local-send-failed", {}, id, reason, observedUs);
    for (auto &entry : operations_) if (entry.request.id == id) entry.localSent = ok;
    protocol_->onSent(id, ok, reason, observedUs);
}
void DebugSession::publish(OperationUpdate update)
{
    for (auto &operation : operations_) if (operation.request.id == update.id && !operation.finished) {
        operation.finished = update.state != "queued" && update.state != "submitted" && update.state != "sent";
        operation.update = std::move(update);
        break;
    }
    if (!notificationTimer_.isActive()) notificationTimer_.start();
}
void DebugSession::publishMessage(Message message)
{
    message.id = nextMessageId_++;
    messages_.append(std::move(message));
    if (messages_.size() > 256) messages_.removeFirst();
    if (!notificationTimer_.isActive()) notificationTimer_.start();
}
quint64 DebugSession::submit(const QString &action, const QJsonObject &parameters)
{
    const auto id = nextOperationId_++;
    const OperationRequest request{id, action, parameters};
    if (operations_.size() >= 128) {
        for (qsizetype i = 0; i < operations_.size(); ++i) if (operations_[i].finished) {
            operations_.removeAt(i); break;
        }
    }
    operations_.append({request, {id, "queued", QStringLiteral("操作已提交，等待协议校验"), {}}, false, false, false, {}});
    if (state_ != SessionState::Running || !protocol_) publish({id, "invalid", QStringLiteral("请先启动会话"), {}});
    else protocol_->submit(request);
    return id;
}
OperationSnapshot DebugSession::operation(quint64 id) const
{
    for (const auto &operation : operations_) if (operation.request.id == id) return operation;
    return {};
}
QList<TraceRecord> DebugSession::traces(quint64 id) const
{
    if (!id) return traces_;
    const auto refs = operation(id).observedRefs;
    QList<TraceRecord> result;
    for (const auto &record : traces_) if (refs.contains(record.id)) result.append(record);
    return result;
}
QList<Message> DebugSession::messages(quint64 id) const
{
    if (!id) return messages_;
    const auto refs = operation(id).observedRefs;
    QList<Message> result;
    for (const auto &message : messages_) {
        bool observed = message.operationId == id;
        for (auto ref : message.sources) if (refs.contains(ref)) observed = true;
        if (observed) result.append(message);
    }
    return result;
}
std::optional<TraceRecord> DebugSession::trace(quint64 id) const
{
    for (const auto &record : traces_) if (record.id == id) return record;
    return std::nullopt;
}
SessionAccess DebugSession::access()
{
    QPointer<DebugSession> self(this);
    return {[self](QString action, QJsonObject parameters) { return self ? self->submit(action, parameters) : 0; },
            [self](quint64 id) { return self ? self->operation(id) : OperationSnapshot{}; },
            [self](quint64 id) { return self ? self->traces(id) : QList<TraceRecord>{}; },
            [self](quint64 id) { return self ? self->messages(id) : QList<Message>{}; },
            [self] { return self && self->state() == SessionState::Running; }};
}
}
