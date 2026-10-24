#include "debugger/host.h"
#include "session_window.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSemaphore>
#include <QTabWidget>
#include <QThreadPool>
#include <QTimer>

namespace debugger {
namespace {
QString validateEncodedLayout(const QJsonObject &layout, const QStringList &keys)
{
    for (const auto &key : keys) if (layout.contains(key)) {
        const auto value = layout.value(key);
        if (!value.isString() || value.toString().size() > 256 * 1024)
            return QStringLiteral("布局 %1 必须为有界 Base64 字符串").arg(key);
        if (!QByteArray::fromBase64Encoding(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors))
            return QStringLiteral("布局 %1 的 Base64 无效").arg(key);
    }
    return {};
}
QString validateWindowLayout(const QJsonObject &document)
{
    // Qt 的不透明布局在临时窗口上先完整校验，不能拿正在通信的工作区试恢复。
    QMainWindow probe;
    const auto root = document.value("layout").toObject();
    const auto geometry = QByteArray::fromBase64(root.value("geometry").toString().toLatin1());
    if (!geometry.isEmpty() && !probe.restoreGeometry(geometry)) return QStringLiteral("工作台窗口几何无效");
    const auto state = QByteArray::fromBase64(root.value("dockState").toString().toLatin1());
    if (!state.isEmpty() && !probe.restoreState(state)) return QStringLiteral("工作台停靠布局无效");
    for (const auto &entry : document.value("sessions").toArray()) {
        const auto saved = QByteArray::fromBase64(entry.toObject().value("layout").toObject().value("windowGeometry").toString().toLatin1());
        if (!saved.isEmpty() && !probe.restoreGeometry(saved)) return QStringLiteral("调试窗口几何无效");
    }
    return {};
}
constexpr qint64 maximumBytes = 8 * 1024 * 1024;
QSemaphore workspaceSlots(4);
struct JobLease { ~JobLease() { workspaceSlots.release(); } };
QString validate(const QJsonObject &document)
{
    if (!document.value("version").isDouble() || document.value("version").toInt(-1) != 1)
        return QStringLiteral("工作区 version 必须为整数 1");
    if (!document.value("sessions").isArray() || document.value("sessions").toArray().size() > 16)
        return QStringLiteral("sessions 必须为至多 16 项的数组");
    for (const auto &entry : document.value("sessions").toArray()) {
        if (!entry.isObject()) return QStringLiteral("每个会话须为对象");
        const auto item = entry.toObject();
        if (!item.value("configuration").isObject()) return QStringLiteral("缺少会话 configuration 对象");
        const auto config = item.value("configuration").toObject();
        for (const auto &key : {"protocolId", "transportId", "role"})
            if (!config.value(key).isString() || config.value(key).toString().isEmpty() || config.value(key).toString().size() > 256)
                return QStringLiteral("会话 %1 必须为非空字符串且不超过 256 字符").arg(key);
        for (const auto &key : {"endpoint", "protocol", "recovery"})
            if (!config.value(key).isObject()) return QStringLiteral("会话 %1 必须为对象").arg(key);
        const auto recovery = config.value("recovery").toObject();
        for (const auto &key : {"autoReconnect", "restorePolling"})
            if (recovery.contains(key) && !recovery.value(key).isBool()) return QStringLiteral("重连开关必须为布尔值");
        if (recovery.contains("intervalMs") && (recovery.value("intervalMs").toInt(-1) < 10 || recovery.value("intervalMs").toInt(-1) > 60000))
            return QStringLiteral("重连间隔必须为 10..60000 ms 整数");
        if (item.contains("referenceBase") && (!item.value("referenceBase").isString() || !QDir::isAbsolutePath(item.value("referenceBase").toString())))
            return QStringLiteral("不透明配置的 referenceBase 必须为绝对目录");
        if (item.contains("title") && !item.value("title").isString()) return QStringLiteral("会话 title 必须为字符串");
        if (item.contains("layout") && !item.value("layout").isObject()) return QStringLiteral("会话 layout 必须为对象");
        const auto layout = item.value("layout").toObject();
        for (const auto &key : {"floating", "open"})
            if (layout.contains(key) && !layout.value(key).isBool()) return QStringLiteral("窗口布局开关必须为布尔值");
        if (layout.contains("selectedDevice") && !layout.value("selectedDevice").isString()) return QStringLiteral("选择设备必须为字符串");
        const auto layoutError = validateEncodedLayout(layout, {"windowGeometry"});
        if (!layoutError.isEmpty()) return layoutError;
    }
    if (document.contains("layout") && !document.value("layout").isObject()) return QStringLiteral("工作区 layout 必须为对象");
    return validateEncodedLayout(document.value("layout").toObject(), {"geometry", "dockState"});
}
SessionConfig decode(const QJsonObject &config)
{
    return {config.value("protocolId").toString(), config.value("transportId").toString(), config.value("role").toString(),
        config.value("endpoint").toObject(), config.value("protocol").toObject(), config.value("recovery").toObject()};
}
QJsonObject overlay(QJsonObject raw, const SessionConfig &config)
{
    raw["protocolId"] = config.protocolId; raw["transportId"] = config.transportId; raw["role"] = config.role;
    raw["endpoint"] = config.endpoint; raw["protocol"] = config.protocol; raw["recovery"] = config.recovery;
    return raw;
}
QJsonObject fileOperation(const QString &path, bool writing, const QJsonObject &snapshot)
{
    if (writing) {
        const auto validationError = validate(snapshot);
        if (!validationError.isEmpty()) return {{"error", validationError}};
        const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
        if (bytes.size() > maximumBytes) return {{"error", QStringLiteral("工作区超过 8 MiB 上限")}};
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return {{"error", file.errorString()}};
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{"error", file.errorString()}};
    const auto bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || !file.atEnd() || file.error() != QFileDevice::NoError)
        return {{"error", QStringLiteral("读取失败或工作区超过 8 MiB 上限")}};
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) return {{"error", QStringLiteral("JSON 无效：") + parse.errorString()}};
    const auto error = validate(json.object());
    return error.isEmpty() ? QJsonObject{{"document", json.object()}} : QJsonObject{{"error", error}};
}
}
WorkspaceController::WorkspaceController(Workbench &window, std::shared_ptr<PluginCatalog> catalog, Executor executor)
    : QObject(&window), window_(window), catalog_(std::move(catalog)), executor_(std::move(executor)) {}
quint64 WorkspaceController::open(const QString &path) { return begin(path, false); }
quint64 WorkspaceController::save(const QString &path) { return begin(path, true); }
WorkspaceOperation WorkspaceController::operation(quint64 id) const
{
    for (const auto &item : operations_) if (item.id == id) return item;
    return {};
}
bool WorkspaceController::restoreLayout()
{
    QPointer<WorkspaceController> alive(this);
    auto *status = window_.findChild<QLabel *>("workspaceStatus");
    if (busy_ || rawRoot_.isEmpty()) {
        status->setText(QStringLiteral("请先完成打开或保存工作区，再恢复布局。")); return false;
    }
    applyLayout(rawRoot_, false);
    if (!alive) return false;
    status->setText(QStringLiteral("已恢复最后打开／保存的布局；当前窗口开闭、数据与通信保持不变。"));
    return true;
}
void WorkspaceController::complete(quint64 id, const QString &state, const QString &detail)
{
    busy_ = false;
    for (auto &item : operations_) if (item.id == id) { item.state = state; item.detail = detail; }
    window_.findChild<QLabel *>("workspaceStatus")->setText(detail);
}
quint64 WorkspaceController::begin(const QString &input, bool writing)
{
    auto *status = window_.findChild<QLabel *>("workspaceStatus");
    if (busy_) { status->setText(QStringLiteral("工作区文件任务尚未结束，请等待后重试")); return 0; }
    if (input.isEmpty() || !QDir::isAbsolutePath(input)) { status->setText(QStringLiteral("工作区路径必须为非空绝对路径")); return 0; }
    if (!workspaceSlots.tryAcquire()) { status->setText(QStringLiteral("后台工作区任务繁忙（最多 4 个），请稍后重试")); return 0; }
    auto lease = std::make_shared<JobLease>();
    const auto path = QDir::cleanPath(input), directory = QFileInfo(path).absolutePath();
    const auto captured = writing ? capture(directory) : QJsonObject{};
    const auto id = nextId_++;
    if (operations_.size() == 16) operations_.removeFirst();
    operations_.append({id, "working", QStringLiteral("后台处理工作区文件，通信继续"), path});
    busy_ = true; status->setText(operations_.last().detail);
    QPointer<WorkspaceController> self(this);
    auto work = [lease, self, id, path, directory, writing, captured] {
        const auto result = fileOperation(path, writing, captured);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [lease, self, id, directory, writing, captured, result] {
            if (!self) return;
            const auto error = result.value("error").toString();
            if (!error.isEmpty()) { self->complete(id, "file-error", error); return; }
            if (writing) { self->rawRoot_ = captured; self->complete(id, "success", QStringLiteral("工作区配置快照已保存；后续配置修改需再次保存，未写入设备")); return; }
            const auto layoutError = validateWindowLayout(result.value("document").toObject());
            if (!layoutError.isEmpty()) { self->complete(id, "file-error", layoutError); return; }
            QString reason;
            const bool loaded = self->apply(result.value("document").toObject(), directory, reason);
            if (!self) return;
            self->complete(id, loaded ? "success" : "cancelled", loaded ? QStringLiteral("工作区已恢复，所有会话均未启动；请由操作人员启动通信") : reason);
        }, Qt::QueuedConnection);
    };
    if (executor_) executor_(std::move(work)); else QThreadPool::globalInstance()->start(std::move(work));
    return id;
}
QJsonObject WorkspaceController::capture(const QString &directory) const
{
    QJsonObject document = rawRoot_;
    document["version"] = 1;
    QJsonArray sessions;
    for (int index = 0; index < window_.sessionCount(); ++index) {
        auto *pane = window_.sessionView(index);
        auto item = pane->property("workspaceRaw").value<QJsonObject>();
        auto config = window_.sessionConfiguration(index);
        auto *protocol = catalog_->protocol(config.protocolId); auto *transport = catalog_->transport(config.transportId);
        if (protocol && transport) {
            config.protocol = protocol->relocateConfiguration(config.protocol, {}, directory);
            config.endpoint = transport->relocateConfiguration(config.endpoint, {}, directory);
            item.remove("referenceBase");
        } else {
            if (!item.contains("referenceBase")) item["referenceBase"] = pane->property("workspaceBase").toString();
        }
        item["title"] = window_.sessionTitle(index);
        item["configuration"] = overlay(item.value("configuration").toObject(), config);
        auto layout = item.value("layout").toObject();
        auto *scroll = pane->findChild<QScrollArea *>("sessionContent");
        layout["scrollX"] = scroll->horizontalScrollBar()->value(); layout["scrollY"] = scroll->verticalScrollBar()->value();
        layout["floating"] = window_.isSessionFloating(index);
        layout["open"] = window_.isSessionOpen(index);
        layout["windowGeometry"] = QString::fromLatin1(window_.sessionWindow(index)->saveGeometry().toBase64());
        if (auto *view = window_.parameterView(index)) layout["selectedDevice"] = view->selectedDevice();
        item["layout"] = layout;
        sessions.append(item);
    }
    document["sessions"] = sessions;
    auto layout = document.value("layout").toObject();
    layout["geometry"] = QString::fromLatin1(window_.saveGeometry().toBase64());
    layout["dockState"] = QString::fromLatin1(window_.saveState().toBase64());
    layout["activeSession"] = window_.currentSessionIndex();
    document["layout"] = layout;
    return document;
}
bool WorkspaceController::apply(const QJsonObject &document, const QString &directory, QString &error)
{
    if (window_.workspaceDialogDepth_) {
        error = QStringLiteral("工作区文件选择尚未结束，未替换当前会话。请关闭选择器后重试。"); return false;
    }
    QPointer<WorkspaceController> alive(this);
    for (int index = 0; index < window_.sessionCount(); ++index) {
        const bool resolved = window_.session(index)->resolveUnsaved();
        if (!alive) return false;
        if (!resolved) {
            error = QStringLiteral("工作区替换已取消，现有会话和未保存数据保留"); return false;
        }
    }
    // 后续会话的模态确认期间，前面的会话仍可能收到设备写入。
    for (int index = 0; index < window_.sessionCount(); ++index) if (window_.session(index)->hasUnsavedOrBusy()) {
        error = QStringLiteral("确认期间出现新的未保存数据或后台任务，未替换工作区"); return false;
    }
    window_.clearSessions();
    window_.findChild<QPushButton *>("addSession")->setEnabled(true);
    rawRoot_ = document;
    for (const auto &entry : document.value("sessions").toArray()) {
        auto item = entry.toObject();
        const auto base = item.value("referenceBase").toString(directory);
        auto config = decode(item.value("configuration").toObject());
        if (auto *plugin = catalog_->protocol(config.protocolId)) config.protocol = plugin->relocateConfiguration(config.protocol, base, {});
        if (auto *plugin = catalog_->transport(config.transportId)) config.endpoint = plugin->relocateConfiguration(config.endpoint, base, {});
        const auto index = window_.addSession(config, item.value("title").toString());
        auto *pane = window_.sessionView(index);
        pane->setProperty("workspaceRaw", item); pane->setProperty("workspaceBase", base);
        auto *scroll = pane->findChild<QScrollArea *>("sessionContent");
        const auto layout = item.value("layout").toObject();
        QTimer::singleShot(0, scroll, [scroll, layout] {
            scroll->horizontalScrollBar()->setValue(layout.value("scrollX").toInt());
            scroll->verticalScrollBar()->setValue(layout.value("scrollY").toInt());
        });
    }
    applyLayout(document, true);
    return true;
}
void WorkspaceController::applyLayout(const QJsonObject &document, bool restoreOpen)
{
    const auto catalogLease = catalog_;
    QPointer<WorkspaceController> alive(this);
    const auto layout = document.value("layout").toObject();
    const auto geometry = QByteArray::fromBase64(layout.value("geometry").toString().toLatin1());
    if (!geometry.isEmpty()) window_.restoreGeometry(geometry);
    const auto entries = document.value("sessions").toArray();
    for (int index = 0; index < qMin(entries.size(), window_.sessionCount()); ++index) {
        const auto item = entries[index].toObject().value("layout").toObject();
        auto *dock = window_.docks_[index];
        if (item.value("floating").toBool(true)) dock->detach(); else dock->attach();
    }
    const auto dockState = QByteArray::fromBase64(layout.value("dockState").toString().toLatin1());
    if (!dockState.isEmpty()) window_.restoreState(dockState);
    for (int index = 0; index < qMin(entries.size(), window_.sessionCount()); ++index) {
        const auto item = entries[index].toObject().value("layout").toObject();
        auto *dock = window_.docks_[index];
        const auto saved = QByteArray::fromBase64(item.value("windowGeometry").toString().toLatin1());
        if (dock->detached()) dock->restoreFloatingGeometry(saved);
        if (restoreOpen) {
            if (auto *view = window_.parameterView(index); view && item.contains("selectedDevice")) {
                view->selectDevice(item.value("selectedDevice").toString());
                if (!alive) return;
            }
            if (!item.value("open").toBool(true)) dock->hideSession();
        }
        if (dock->open() && window_.isVisible()) dock->present();
        else if (!dock->open()) dock->hideSession();
    }
    // 已恢复原生几何，第一次显示不得再次套用新建窗口的居中策略。
    if (layout.contains("dockState")) window_.presented_ = true;
    const int active = layout.value("activeSession").toInt(0);
    if (active >= 0 && active < window_.sessionCount()) window_.currentSession_ = active;
    window_.refreshNavigation();
}
}






