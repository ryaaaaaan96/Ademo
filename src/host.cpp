#include "debugger/host.h"
#include "trace_codec.h"
#include <QDir>
#include <QTimer>
#include <QUuid>
#include <QCoreApplication>
#include <QSemaphore>
#include <QThreadPool>

namespace debugger {
namespace {
QSemaphore preparationSlots(4);
QSemaphore localJobSlots(4);
struct PreparationJob {
    std::shared_ptr<PluginCatalog> catalog;
    std::shared_ptr<ProtocolSession> protocol;
    ~PreparationJob() { preparationSlots.release(); }
};
struct LocalJob {
    std::shared_ptr<PluginCatalog> catalog;
    std::shared_ptr<ProtocolSession> protocol;
    ~LocalJob() { localJobSlots.release(); }
};
}
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
                           EndpointFactory factory, std::function<qint64()> clock, BackgroundExecutor executor)
    : QObject(parent), catalog_(std::move(catalog)), endpointFactory_(std::move(factory)), clock_(std::move(clock)),
      preparationExecutor_(std::move(executor))
{
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    elapsed_.start();
    pollTimer_.setTimerType(Qt::PreciseTimer);
    pollTimer_.setInterval(1);
    connect(&pollTimer_, &QTimer::timeout, this, &DebugSession::poll);
    notificationTimer_.setSingleShot(true);
    notificationTimer_.setInterval(33);
    connect(&notificationTimer_, &QTimer::timeout, this, &DebugSession::changed);
}
DebugSession::~DebugSession() { retire(); }
void DebugSession::retire(bool retainLocal)
{
    const bool keep = retainLocal && protocol_ && !preparing_ && protocol_->localState().retainAfterStop;
    ++generation_;
    pollTimer_.stop();
    sendAttempts_.clear();
    for (auto &operation : operations_) {
        if (!operation.finished && !(keep && operation.localOnly)) {
            operation.finished = true;
            operation.update = {operation.request.id, "cancelled", QStringLiteral("会话已停止，操作已取消"), {}};
            recordOperation(operation);
        }
    }
    if (protocol_ && !preparing_) protocol_->stop();
    preparing_ = false;
    passiveSends_.clear();
    passiveDeadlines_.clear();
    if (endpoint_) endpoint_->close();
    endpoint_.reset();
    if (!keep) protocol_.reset();
    if (!key_.isEmpty()) catalog_->release(key_);
    key_.clear();
}
void DebugSession::stop()
{
    retire(true);
    state_ = SessionState::Stopped;
    diagnostic_ = QStringLiteral("会话已停止，端点已释放");
    emit changed();
}
void DebugSession::fail(const QString &reason)
{
    for (auto &operation : operations_) if (!operation.finished && !operation.localOnly) {
        operation.finished = true;
        operation.update = {operation.request.id, "transport-error", reason, {}};
        recordOperation(operation);
    }
    retire(true);
    state_ = SessionState::Error;
    diagnostic_ = reason;
    emit changed();
}
bool DebugSession::start(const SessionConfig &config)
{
    if (state_ == SessionState::Starting || state_ == SessionState::Running) return false;
    QPointer<DebugSession> guard(this);
    if (!resolveUnsaved() || !guard) return false;
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
        if (self && self->ownsProtocol(run)) self->publish(std::move(update));
    };
    services.publishMessage = [self, run](Message message) {
        if (self && self->ownsProtocol(run)) self->publishMessage(std::move(message));
    };
    services.submit = [self, run](QString action, QJsonObject parameters) {
        return self && self->generation_ == run ? self->submit(action, parameters) : quint64(0);
    };
    services.publishConfiguration = [self, run](QJsonObject configuration) {
        if (self && self->ownsProtocol(run)) self->config_.protocol = std::move(configuration);
    };
    services.sendPassive = [self, run](QByteArray bytes) { return self ? self->sendPassive(run, bytes) : 0; };
    services.background = [self, run](std::function<QJsonObject()> task, std::function<void(QJsonObject)> completion) {
        return self && self->background(run, std::move(task), std::move(completion));
    };
    protocolRun_ = run;
    protocol_ = p->createSession(std::move(services));
    if (!protocol_) { fail(QStringLiteral("协议会话创建失败")); return false; }
    if (protocol_->prepareInBackground(config)) {
        if (!preparationSlots.tryAcquire()) { fail(QStringLiteral("后台准备繁忙（最多 4 个任务），请稍后重试")); return false; }
        auto job = std::make_shared<PreparationJob>();
        job->catalog = catalog_;
        job->protocol = protocol_;
        preparing_ = true;
        state_ = SessionState::Starting;
        diagnostic_ = QStringLiteral("正在后台读取并校验全部启动数据文件");
        auto work = [job, config, self, run] {
            const auto error = job->protocol->prepare(config);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [job, self, run, error] {
                if (self && self->generation_ == run && self->preparing_) self->prepared(run, error);
            }, Qt::QueuedConnection);
        };
        if (preparationExecutor_) preparationExecutor_(std::move(work));
        else QThreadPool::globalInstance()->start(std::move(work));
        emit changed();
        return true;
    }
    const auto preparation = protocol_->prepare(config);
    return prepared(run, preparation);
}
bool DebugSession::prepared(quint64 run, const QString &error)
{
    if (run != generation_) return false;
    preparing_ = false;
    if (!error.isEmpty()) { fail(error); return false; }
    const auto config = config_;
    auto *t = catalog_->transport(config.transportId);
    QPointer<DebugSession> self(this);
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
void DebugSession::poll() {
    if (state_ != SessionState::Running || !protocol_) return;
    const auto ids = passiveDeadlines_.keys();
    for (auto id : ids) if (nowUs() >= passiveDeadlines_.value(id))
        sent(generation_, id, false, QStringLiteral("被动应答本地发送超时，未自动重发"), nowUs());
    protocol_->tick();
}
quint64 DebugSession::appendTrace(const QString &kind, const QByteArray &bytes, quint64 operationId,
                                  const QString &detail, qint64 observedUs, QJsonObject metadata,
                                  std::optional<qint64> idleBeforeUs)
{
    const auto id = nextTraceId_++;
    traces_.append({id, generation_, operationId, observedUs < 0 ? nowUs() : observedUs,
                    QDateTime::currentDateTimeUtc(), kind, bytes, detail, idleBeforeUs, std::move(metadata)});
    if (traces_.size() > traceLimits_.records) { traces_.removeFirst(); ++expiredTraces_; }
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
    recordTrace(traces_.last());
    if (!notificationTimer_.isActive()) notificationTimer_.start();
    return id;
}
void DebugSession::received(quint64 run, ReceiveChunk input, qint64 observedUs)
{
    if (run != generation_ || !protocol_ || (state_ != SessionState::Running && state_ != SessionState::Starting)) return;
    for (qsizetype offset = 0; offset < input.bytes.size(); offset += 4096) {
        const auto chunk = input.bytes.mid(offset, 4096);
        const auto id = appendTrace("rx", chunk, 0, {}, observedUs, input.metadata, offset == 0 ? input.idleBeforeUs : std::nullopt);
        protocol_->onInput(*trace(id)); // 原始记录分配稳定身份后，才进入协议。
    }
}
bool DebugSession::send(quint64 run, quint64 id, const QByteArray &bytes)
{
    if (run != generation_ || state_ != SessionState::Running || !endpoint_ || bytes.size() > 256) return false;
    auto &attempt = sendAttempts_[id];
    ++attempt.attempt;
    attempt.token = attempt.attempt == 1 ? id : allocateSendToken();
    attempt.completed = false;
    const auto token = attempt.token;
    const QJsonObject metadata{{"attempt", attempt.attempt}, {"sendId", QString::number(token)}};
    appendTrace("send-request", bytes, id, QStringLiteral("请求本地发送，尚未证明发送完成或设备应答"), -1, metadata);
    const bool accepted = endpoint_->send(token, bytes);
    for (auto &operation : operations_) if (operation.request.id == id) {
        operation.sendAccepted = accepted;
        operation.localSent = false;
    }
    appendTrace(accepted ? "send-accepted" : "send-rejected", {}, id, {}, -1, metadata);
    return accepted;
}
quint64 DebugSession::sendPassive(quint64 run, const QByteArray &bytes)
{
    if (run != generation_ || state_ != SessionState::Running || !endpoint_ || bytes.isEmpty()
        || bytes.size() > 256 || passiveSends_.size() >= 1) return 0;
    const auto id = allocateSendToken();
    appendTrace("send-request", bytes, 0, QStringLiteral("被动应答，本地发送 ID %1").arg(id));
    const bool accepted = endpoint_->send(id, bytes);
    appendTrace(accepted ? "send-accepted" : "send-rejected", {}, 0, QStringLiteral("本地发送 ID %1").arg(id));
    if (accepted) { passiveSends_.insert(id); passiveDeadlines_.insert(id, nowUs() + 1000000); return id; }
    return 0;
}
void DebugSession::sent(quint64 run, quint64 id, bool ok, const QString &reason, qint64 observedUs)
{
    if (run != generation_ || state_ != SessionState::Running || !protocol_) return;
    if (passiveSends_.remove(id)) {
        const bool late = observedUs >= passiveDeadlines_.value(id);
        passiveDeadlines_.remove(id);
        const auto detail = late ? QStringLiteral("被动应答本地发送超时，迟到完成不延长期限") : reason;
        appendTrace(ok && !late ? "local-sent" : "local-send-failed", {}, 0, QStringLiteral("本地发送 ID %1：%2").arg(id).arg(detail), observedUs);
        protocol_->onSent(id, ok && !late, detail, observedUs);
        return;
    }
    auto current = sendAttempts_.end();
    for (auto it = sendAttempts_.begin(); it != sendAttempts_.end(); ++it)
        if (it->token == id) { current = it; break; }
    if (current == sendAttempts_.end()) {
        appendTrace("stale-send-result", {}, 0, QStringLiteral("旧发送令牌 %1 的回调已忽略").arg(id), observedUs);
        return;
    }
    const auto operationId = current.key();
    const auto operation = this->operation(operationId);
    if (!operation.sendAccepted || current->completed || operation.finished) return;
    current->completed = true;
    appendTrace(ok ? "local-sent" : "local-send-failed", {}, operationId, reason, observedUs,
                {{"attempt", current->attempt}, {"sendId", QString::number(id)}});
    for (auto &entry : operations_) if (entry.request.id == operationId) entry.localSent = ok;
    protocol_->onSent(operationId, ok, reason, observedUs);
}
void DebugSession::publish(OperationUpdate update)
{
    for (auto &operation : operations_) if (operation.request.id == update.id && !operation.finished) {
        operation.finished = update.state != "queued" && update.state != "submitted" && update.state != "sent" && update.state != "retry-wait" && update.state != "working";
        operation.update = std::move(update);
        recordOperation(operation);
        break;
    }
    if (!notificationTimer_.isActive()) notificationTimer_.start();
}
void DebugSession::publishMessage(Message message)
{
    message.id = nextMessageId_++;
    message.monotonicUs = nowUs();
    message.observedUtc = QDateTime::currentDateTimeUtc();
    message.sources = message.sources.mid(0, 256);
    message.sourceRanges = message.sourceRanges.mid(0, 256);
    if (message.sourceRanges.isEmpty()) for (auto id : message.sources) {
        const auto record = trace(id);
        message.sourceRanges.append({id, 0, record ? int(record->bytes.size()) : -1});
    }
    for (const auto &range : message.sourceRanges)
        if (!message.sources.contains(range.recordId)) message.sources.append(range.recordId);
    messages_.append(std::move(message));
    if (recorder_) recorder_->append(tracecodec::encode(messages_.last()));
    if (messages_.size() > traceLimits_.messages) { messages_.removeFirst(); ++expiredMessages_; }
    if (!notificationTimer_.isActive()) notificationTimer_.start();
}
quint64 DebugSession::submit(const QString &action, const QJsonObject &parameters)
{
    const auto id = nextOperationId_++;
    const OperationRequest request{id, action, parameters};
    if (operations_.size() >= traceLimits_.operations) {
        for (qsizetype i = 0; i < operations_.size(); ++i) if (operations_[i].finished) {
            sendAttempts_.remove(operations_[i].request.id);
            operations_.removeAt(i); ++expiredOperations_; break;
        }
    }
    // 未完成操作不能被淘汰；满时拒绝且不增加常驻条目。
    if (operations_.size() >= traceLimits_.operations) return 0;
    operations_.append({request, {id, "queued", QStringLiteral("操作已提交，等待协议校验"), {}}, false, false, false, {}});
    const bool local = protocol_ && !preparing_ && protocol_->localState().actions.contains(action);
    operations_.last().localOnly = local;
    recordOperation(operations_.last());
    if (!protocol_ || (state_ != SessionState::Running && !local)) publish({id, "invalid", QStringLiteral("请先启动会话"), {}});
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
            [self] { return self && self->state() == SessionState::Running; },
            [self] { return self ? self->config() : SessionConfig{}; },
            [self](QJsonObject config) { return self && self->configure(std::move(config)); },
            [self] { return self ? self->snapshot() : QJsonObject{}; },
            [self](std::function<bool()> guard) { if (self) self->discardGuard_ = std::move(guard); }};
}
bool DebugSession::configure(QJsonObject protocol)
{
    if (state_ == SessionState::Starting || state_ == SessionState::Running) return false;
    if (protocol_ && protocol_->localState().busy) return false;
    if (!resolveUnsaved()) return false;
    // 新启动配置生效后撤下旧本地模型，避免旧绑定的文件作业写回覆盖新配置。
    protocol_.reset();
    config_.protocol = std::move(protocol);
    emit changed();
    return true;
}
QJsonObject DebugSession::snapshot() const
{
    return protocol_ && !preparing_ ? protocol_->snapshot() : QJsonObject{};
}
bool DebugSession::ownsProtocol(quint64 run) const { return protocol_ && protocolRun_ == run; }
bool DebugSession::resolveUnsaved()
{
    if (resolvingUnsaved_) return false;
    if (!protocol_ || preparing_) return true;
    if (protocol_->localState().busy) {
        diagnostic_ = QStringLiteral("后台文件操作尚未结束，请等待后再重新加载或关闭"); emit changed(); return false;
    }
    if (!protocol_->localState().dirty) return true;
    QPointer<DebugSession> guard(this);
    const auto decision = discardGuard_;
    resolvingUnsaved_ = true;
    const bool resolved = decision && decision();
    if (!guard) return false;
    resolvingUnsaved_ = false;
    if (resolved && protocol_ && !protocol_->localState().dirty && !protocol_->localState().busy) return true;
    diagnostic_ = QStringLiteral("保留未保存修改；请保存、放弃或取消后再重新加载或关闭"); emit changed(); return false;
}
bool DebugSession::background(quint64 run, std::function<QJsonObject()> task, std::function<void(QJsonObject)> completion)
{
    if (!ownsProtocol(run) || preparing_ || !localJobSlots.tryAcquire()) return false;
    auto job = std::make_shared<LocalJob>(); job->catalog = catalog_; job->protocol = protocol_;
    QPointer<DebugSession> self(this);
    auto work = [job, self, run, task = std::move(task), completion = std::move(completion)] {
        QJsonObject result;
        try { result = task(); } catch (const std::exception &error) { result = {{"error", QString::fromUtf8(error.what())}}; }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [job, self, run, completion, result] {
            if (self && self->ownsProtocol(run)) completion(result);
        }, Qt::QueuedConnection);
    };
    if (preparationExecutor_) preparationExecutor_(std::move(work));
    else QThreadPool::globalInstance()->start(std::move(work));
    return true;
}
}
