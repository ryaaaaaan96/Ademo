#include "debugger/host.h"
#include <QComboBox>
#include <QCloseEvent>
#include <QCheckBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QFileDialog>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QStackedWidget>

namespace debugger {
class SessionPane final : public QWidget {
public:
    SessionPane(std::shared_ptr<PluginCatalog>, QWidget *, DebugSession::EndpointFactory, std::function<qint64()>);
    ~SessionPane() override;
    void loadConfiguration(const SessionConfig &);
    SessionConfig configuration() const;
    DebugSession session_;
    ParameterView *parameterView_ = nullptr;
private:
    std::shared_ptr<PluginCatalog> catalog_;
    QWidget *view_ = nullptr;
    SessionConfig original_;
    std::function<void()> rebuildProtocol_, refresh_;
};
SessionPane::SessionPane(std::shared_ptr<PluginCatalog> catalog, QWidget *parent,
                     DebugSession::EndpointFactory factory, std::function<qint64()> clock)
    : QWidget(parent), session_(catalog, nullptr, std::move(factory), std::move(clock)), catalog_(std::move(catalog))
{
    setWindowTitle(QStringLiteral("多协议调试工具 · RTU 会话"));
    resize(680, 540);
    auto *outer = new QVBoxLayout(this);
    auto *connection = new QDialog(this);
    connection->setWindowTitle(QStringLiteral("连接配置"));
    auto *connectionLayout = new QVBoxLayout(connection);
    auto *configure = new QPushButton(QStringLiteral("连接配置…"), this);
    connect(configure, &QPushButton::clicked, connection, [connection] { connection->show(); connection->raise(); });
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("sessionContent");
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    auto *formWidget = new QWidget(this);
    auto *form = new QFormLayout(formWidget);
    auto *protocol = new QComboBox(formWidget);
    auto *transport = new QComboBox(formWidget);
    auto *role = new QComboBox(formWidget);
    protocol->setObjectName("protocol");
    transport->setObjectName("transport");
    role->setObjectName("role");
    for (auto *p : catalog_->protocols()) protocol->addItem(p->describe().name, p->describe().id);
    for (auto *t : catalog_->transports()) transport->addItem(t->describe().name, t->describe().id);
    form->addRow(QStringLiteral("协议"), protocol);
    form->addRow(QStringLiteral("传输"), transport);
    form->addRow(QStringLiteral("角色"), role);
    connectionLayout->addWidget(formWidget);
    auto *endpointWidget = new QWidget(this);
    auto *endpointForm = new QFormLayout(endpointWidget);
    connectionLayout->addWidget(endpointWidget);
    auto editors = std::make_shared<QMap<QString, QWidget *>>();
    auto updateTransport = [this, transport, endpointForm, editors] {
        while (endpointForm->rowCount()) endpointForm->removeRow(0);
        editors->clear();
        auto *t = catalog_->transport(transport->currentData().toString());
        if (!t) return;
        for (const auto &field : t->describe().fields) {
            QWidget *editor;
            if (field.choices.isEmpty()) {
                editor = new QLineEdit(field.defaultValue);
            } else {
                auto *combo = new QComboBox;
                combo->addItems(field.choices);
                combo->setCurrentText(field.defaultValue);
                editor = combo;
            }
            editor->setObjectName(field.key);
            editors->insert(field.key, editor);
            endpointForm->addRow(field.label, editor);
        }
    };
    auto selectedProtocol = std::make_shared<int>(protocol->currentIndex());
    auto updateProtocol = [this, protocol, role, layout, selectedProtocol] {
        QPointer<SessionPane> alive(this);
        const bool resolved = !view_ || session_.resolveUnsaved();
        if (!alive) return;
        if (!resolved) {
            const QSignalBlocker blocker(protocol);
            protocol->setCurrentIndex(*selectedProtocol);
            return;
        }
        *selectedProtocol = protocol->currentIndex();
        role->clear();
        delete view_;
        view_ = nullptr;
        parameterView_ = nullptr;
        auto *p = catalog_->protocol(protocol->currentData().toString());
        if (!p) return;
        for (const auto &r : p->describe().roles) role->addItem(r, r);
        auto *pages = new QWidget(this);
        auto *pageLayout = new QVBoxLayout(pages);
        auto *stack = new QStackedWidget(pages);
        parameterView_ = catalog_->createParameterView(p->describe().id, stack, session_.access());
        if (parameterView_) stack->addWidget(parameterView_);
        auto *legacy = catalog_->createView(p->describe().id, stack, session_.access());
        if (legacy) {
            stack->addWidget(legacy);
            if (parameterView_) {
                auto *compatibility = new QPushButton(QStringLiteral("兼容操作页"), pages);
                compatibility->setCheckable(true);
                pageLayout->addWidget(compatibility);
                connect(compatibility, &QPushButton::toggled, stack, [stack, legacy, parameters = parameterView_](bool checked) {
                    stack->setCurrentWidget(checked ? legacy : parameters);
                });
            }
        }
        pageLayout->addWidget(stack, 1);
        view_ = pages;
        layout->insertWidget(0, view_, 1);
    };
    rebuildProtocol_ = updateProtocol;
    connect(transport, &QComboBox::currentIndexChanged, this, updateTransport);
    connect(protocol, &QComboBox::currentIndexChanged, this, updateProtocol);
    updateTransport();
    auto *start = new QPushButton(QStringLiteral("启动会话"), this);
    auto *stop = new QPushButton(QStringLiteral("停止并释放端点"), this);
    auto *reconnect = new QPushButton(QStringLiteral("按当前运行配置重连（保留运行数据）"), this);
    reconnect->setObjectName("reconnect");
    start->setObjectName("start");
    stop->setObjectName("stop");
    auto *status = new QLabel(this);
    status->setObjectName("status");
    status->setWordWrap(true);
    auto *diagnostics = new QLabel(catalog_->diagnostics().join('\n'), this);
    diagnostics->setWordWrap(true);
    auto *controls = new QHBoxLayout;
    controls->addWidget(start); controls->addWidget(stop); controls->addWidget(configure);
    outer->addLayout(controls);
    connectionLayout->addWidget(reconnect);
    auto *recovery = new QHBoxLayout;
    auto *automatic = new QCheckBox(QStringLiteral("断线自动重连"), this);
    automatic->setObjectName("autoReconnect");
    auto *restore = new QCheckBox(QStringLiteral("重连后恢复轮询（不补发写入）"), this);
    restore->setObjectName("restorePolling"); restore->setChecked(true);
    auto *interval = new QSpinBox(this);
    interval->setObjectName("reconnectIntervalMs"); interval->setRange(10, 60000); interval->setValue(1000);
    interval->setSuffix(QStringLiteral(" ms"));
    recovery->addWidget(automatic); recovery->addWidget(restore); recovery->addWidget(interval);
    connectionLayout->addLayout(recovery);
    auto *done = new QDialogButtonBox(QDialogButtonBox::Close, connection);
    connect(done, &QDialogButtonBox::rejected, connection, &QDialog::hide);
    connectionLayout->addWidget(done);
    auto updateRecovery = [this, automatic, restore, interval] {
        auto policy = session_.config().recovery;
        policy["autoReconnect"] = automatic->isChecked(); policy["restorePolling"] = restore->isChecked();
        policy["intervalMs"] = interval->value(); session_.configureRecovery(policy);
    };
    connect(automatic, &QCheckBox::toggled, this, updateRecovery);
    connect(restore, &QCheckBox::toggled, this, updateRecovery);
    connect(interval, &QSpinBox::valueChanged, this, updateRecovery);
    outer->addWidget(status);
    outer->addWidget(diagnostics);
    outer->addWidget(scroll, 1);
    updateProtocol();
    auto *basicRecords = new QPlainTextEdit(this);
    basicRecords->setAccessibleName(QStringLiteral("本会话通信记录"));
    basicRecords->setReadOnly(true); basicRecords->setMaximumBlockCount(100);
    basicRecords->setMinimumHeight(100); basicRecords->setMaximumHeight(160);
    basicRecords->setStyleSheet("QPlainTextEdit { background: white; color: #223344; }");
    outer->addWidget(new QLabel(QStringLiteral("本会话通信记录"), this));
    outer->addWidget(basicRecords);
    auto updateBasicRecords = [this, basicRecords] {
        const auto records = session_.traces();
        QStringList lines;
        if (session_.expiredTraceCount()) lines << QStringLiteral("较早的 %1 条原始记录已过期").arg(session_.expiredTraceCount());
        const auto start = qMax(qsizetype(0), records.size() - 80);
        for (auto i = start; i < records.size(); ++i) {
            const auto &r = records[i];
            lines << QStringLiteral("%1 · #%2 · %3 · %4 · %5").arg(r.observedUtc.toLocalTime().toString("HH:mm:ss.zzz"))
                .arg(r.id).arg(r.kind, QString::fromLatin1(r.bytes.toHex(' ')), r.detail);
        }
        const auto text = lines.join('\n');
        if (text != basicRecords->toPlainText()) basicRecords->setPlainText(text);
    };
    connect(&session_, &DebugSession::changed, basicRecords, updateBasicRecords);
    updateBasicRecords();
    layout->addWidget(new TracePanel(session_, this));
    layout->addWidget(new RecordingPanel(session_, this));
    auto refresh = [this, formWidget, endpointWidget, protocol, transport, role, start, stop, reconnect, status] {

        const bool active = session_.state() == SessionState::Starting || session_.state() == SessionState::Running;
        formWidget->setEnabled(!active);
        endpointWidget->setEnabled(!active);
        auto *p = catalog_->protocol(protocol->currentData().toString());
        auto *t = catalog_->transport(transport->currentData().toString());
        const bool available = p && t && p->describe().roles.contains(role->currentData().toString())
            && p->describe().transports.contains(t->describe().id);
        start->setEnabled(!active && available);
        stop->setEnabled(active || session_.canReconnect());
        reconnect->setEnabled(session_.canReconnect());
        status->setText(stateText(session_.state()) + "\n" + (available ? session_.diagnostic()
            : QStringLiteral("所需插件/角色/支持组合不可用，仅禁用此会话；原配置完整保留。\n") + catalog_->diagnostics().join('\n')));
    };
    refresh_ = refresh;
    connect(&session_, &DebugSession::changed, this, refresh);
    connect(role, &QComboBox::currentIndexChanged, this, refresh);
    connect(transport, &QComboBox::currentIndexChanged, this, refresh);
    connect(start, &QPushButton::clicked, this, [this, protocol, transport, role, editors] {
        SessionConfig config;
        config.protocolId = protocol->currentData().toString();
        config.transportId = transport->currentData().toString();
        config.role = role->currentData().toString();
        config.protocol = session_.config().protocol;
        config.recovery = session_.config().recovery;
        for (auto it = editors->cbegin(); it != editors->cend(); ++it) {
            if (auto *line = qobject_cast<QLineEdit *>(it.value())) config.endpoint[it.key()] = line->text();
            else if (auto *combo = qobject_cast<QComboBox *>(it.value())) config.endpoint[it.key()] = combo->currentText();
        }
        session_.start(config);
    });
    connect(stop, &QPushButton::clicked, &session_, &DebugSession::stop);
    connect(reconnect, &QPushButton::clicked, this, [this] { session_.reconnect(); });
    refresh();
}
SessionPane::~SessionPane()
{
    // 在会话与 DLL 租约释放前销毁插件视图。
    delete view_;
    session_.stop();
}
SessionConfig SessionPane::configuration() const
{
    auto config = original_;
    config.protocol = session_.config().protocol;
    config.recovery = session_.config().recovery;
    config.protocolId = findChild<QComboBox *>("protocol")->currentData().toString();
    config.transportId = findChild<QComboBox *>("transport")->currentData().toString();
    config.role = findChild<QComboBox *>("role")->currentData().toString();
    if (auto *transport = catalog_->transport(config.transportId)) for (const auto &field : transport->describe().fields) {
        if (auto *line = findChild<QLineEdit *>(field.key)) config.endpoint[field.key] = line->text();
        else if (auto *combo = findChild<QComboBox *>(field.key)) config.endpoint[field.key] = combo->currentText();
    }
    return config;
}
void SessionPane::loadConfiguration(const SessionConfig &config)
{
    original_ = config;
    session_.configure(config.protocol);
    for (const auto &item : QList<QPair<const char *, QString>>{{"protocol", config.protocolId}, {"transport", config.transportId}}) {
        auto *combo = findChild<QComboBox *>(item.first);
        int index = combo->findData(item.second);
        if (index < 0 && !item.second.isEmpty()) { combo->addItem(QStringLiteral("不可用：") + item.second, item.second); index = combo->count() - 1; }
        if (index >= 0) combo->setCurrentIndex(index);
    }
    rebuildProtocol_();
    auto *role = findChild<QComboBox *>("role");
    int roleIndex = role->findData(config.role);
    if (roleIndex < 0 && !config.role.isEmpty()) { role->addItem(QStringLiteral("不可用：") + config.role, config.role); roleIndex = role->count() - 1; }
    if (roleIndex >= 0) role->setCurrentIndex(roleIndex);
    if (auto *transport = catalog_->transport(config.transportId)) for (const auto &field : transport->describe().fields) {
        if (!config.endpoint.contains(field.key)) continue;
        if (auto *line = findChild<QLineEdit *>(field.key)) line->setText(config.endpoint.value(field.key).toString());
        else if (auto *combo = findChild<QComboBox *>(field.key)) {
            const auto value = config.endpoint.value(field.key).toString();
            if (combo->findText(value) < 0) combo->addItem(value);
            combo->setCurrentText(value);
        }
    }
    const QSignalBlocker a(findChild<QCheckBox *>("autoReconnect"));
    const QSignalBlocker b(findChild<QCheckBox *>("restorePolling"));
    const QSignalBlocker c(findChild<QSpinBox *>("reconnectIntervalMs"));
    findChild<QCheckBox *>("autoReconnect")->setChecked(config.recovery.value("autoReconnect").toBool());
    findChild<QCheckBox *>("restorePolling")->setChecked(config.recovery.value("restorePolling").toBool(true));
    findChild<QSpinBox *>("reconnectIntervalMs")->setValue(config.recovery.value("intervalMs").toInt(1000));
    session_.configureRecovery(config.recovery);
    refresh_();
}
Workbench::Workbench(std::shared_ptr<PluginCatalog> catalog, QWidget *parent,
                     DebugSession::EndpointFactory factory, std::function<qint64()> clock, WorkspaceController::Executor workspaceExecutor)
    : QWidget(parent), catalog_(std::move(catalog)), factory_(std::move(factory)), clock_(std::move(clock))
{
    setWindowTitle(QStringLiteral("多协议调试工具 · 多会话工作台"));
    resize(1000, 900);
    auto *layout = new QVBoxLayout(this);
    auto *add = new QPushButton(QStringLiteral("新增独立会话"), this);
    add->setObjectName("addSession");
    layout->addWidget(add);
    auto *status = new QLabel(this);
    status->setObjectName("workspaceStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    workspace_ = std::make_unique<WorkspaceController>(*this, catalog_, std::move(workspaceExecutor));
    auto *files = new QHBoxLayout;
    auto *path = new QLineEdit(this); path->setObjectName("workspacePath"); path->setPlaceholderText(QStringLiteral("工作区 JSON 绝对路径"));
    auto *open = new QPushButton(QStringLiteral("打开工作区（不启动通信）"), this); open->setObjectName("workspaceOpen");
    auto *save = new QPushButton(QStringLiteral("保存 / 另存工作区"), this); save->setObjectName("workspaceSave");
    files->addWidget(path, 1); files->addWidget(open); files->addWidget(save); layout->addLayout(files);
    connect(open, &QPushButton::clicked, this, [this, path] {
        auto selected = path->text();
        if (selected.isEmpty()) selected = QFileDialog::getOpenFileName(this, QStringLiteral("打开工作区"), {}, "JSON (*.json)");
        if (!selected.isEmpty()) { path->setText(selected); openWorkspace(selected); }
    });
    connect(save, &QPushButton::clicked, this, [this, path] {
        auto selected = path->text();
        if (selected.isEmpty()) selected = QFileDialog::getSaveFileName(this, QStringLiteral("保存工作区"), {}, "JSON (*.json)");
        if (!selected.isEmpty()) { path->setText(selected); saveWorkspace(selected); }
    });
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("sessionTabs");
    tabs_->setTabsClosable(true);
    tabs_->setUsesScrollButtons(true);
    layout->addWidget(tabs_);
    connect(add, &QPushButton::clicked, this, [this] { addSession(); });
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeSession(index); });
    addSession();
}
Workbench::~Workbench()
{
    while (tabs_->count()) { auto *pane = tabs_->widget(0); tabs_->removeTab(0); delete pane; }
}
int Workbench::addSession(const SessionConfig &config, const QString &title)
{
    if (tabs_->count() >= 16) return -1;
    auto *pane = new SessionPane(catalog_, tabs_, factory_, clock_);
    pane->setProperty("workspaceBase", QDir::currentPath());
    pane->loadConfiguration(config);
    const int index = tabs_->addTab(pane, title.isEmpty() ? QStringLiteral("会话 %1").arg(nextSessionNumber_++) : title);
    tabs_->setCurrentIndex(index);
    findChild<QPushButton *>("addSession")->setEnabled(tabs_->count() < 16);
    return index;
}
int Workbench::sessionCount() const { return tabs_->count(); }
DebugSession *Workbench::session(int index) const
{
    auto *pane = dynamic_cast<SessionPane *>(tabs_->widget(index));
    return pane ? &pane->session_ : nullptr;
}
ParameterView *Workbench::parameterView(int index) const
{
    auto *pane = dynamic_cast<SessionPane *>(tabs_->widget(index));
    return pane ? pane->parameterView_ : nullptr;
}
SessionConfig Workbench::sessionConfiguration(int index) const
{
    auto *pane = dynamic_cast<SessionPane *>(tabs_->widget(index));
    return pane ? pane->configuration() : SessionConfig{};
}
int Workbench::currentSessionIndex() const { return tabs_->currentIndex(); }
void Workbench::selectSession(int index) { tabs_->setCurrentIndex(index); }
quint64 Workbench::openWorkspace(const QString &path) { return workspace_->open(path); }
quint64 Workbench::saveWorkspace(const QString &path) { return workspace_->save(path); }
WorkspaceOperation Workbench::workspaceOperation(quint64 id) const { return workspace_->operation(id); }
bool Workbench::closeSession(int index)
{
    auto *pane = dynamic_cast<SessionPane *>(tabs_->widget(index));
    if (!pane || !pane->session_.resolveUnsaved()) return false;
    tabs_->removeTab(index);
    delete pane;
    findChild<QPushButton *>("addSession")->setEnabled(true);
    return true;
}
void Workbench::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < tabs_->count(); ++i) if (!session(i)->resolveUnsaved()) { event->ignore(); return; }
    // 之前的会话可能在后续模态确认期间被设备再次修改；此处不再泵事件或重复弹窗。
    for (int i = 0; i < tabs_->count(); ++i) if (session(i)->hasUnsavedOrBusy()) {
        findChild<QLabel *>("workspaceStatus")->setText(QStringLiteral("关闭已取消：确认期间出现新的未保存修改或后台任务，数据已保留。"));
        tabs_->setCurrentIndex(i);
        event->ignore();
        return;
    }
    for (int i = 0; i < tabs_->count(); ++i) session(i)->stop();
    event->accept();
}
}
