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
QWidget *PluginCatalog::createView(const QString &id, QWidget *parent)
{
    auto *p = protocol(id);
    auto *view = p ? p->createView(parent) : nullptr;
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
DebugSession::DebugSession(std::shared_ptr<PluginCatalog> catalog, QObject *parent, EndpointFactory factory)
    : QObject(parent), catalog_(std::move(catalog)), endpointFactory_(std::move(factory)) {}
DebugSession::~DebugSession() { retire(); }
void DebugSession::retire()
{
    ++generation_;
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
    protocol_ = p->createSession();
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
    const auto run = generation_;
    QPointer<DebugSession> self(this);
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
    diagnostic_ = QStringLiteral("端点已打开；本阶段仅支持会话启停，尚未实现协议读写");
    emit changed();
}
}
