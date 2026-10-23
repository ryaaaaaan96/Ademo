#include "debugger/host.h"
#include <QApplication>
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
#include <QTreeWidget>
#include <QToolBar>
#include <QShowEvent>
#include <QScreen>
#include "session_window.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

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
    auto *recordToggle = new QPushButton(QStringLiteral("通信记录"), this);
    recordToggle->setCheckable(true); recordToggle->setChecked(true);
    recordToggle->setToolTip(QStringLiteral("展开或折叠本会话记录，不停止通信"));
    connect(recordToggle, &QPushButton::toggled, basicRecords, &QWidget::setVisible);
    outer->addWidget(recordToggle);
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
    : QMainWindow(parent), catalog_(std::move(catalog)), factory_(std::move(factory)), clock_(std::move(clock))
{
    setWindowTitle(QStringLiteral("多协议调试工具 · 多会话工作台"));
    const auto available = screen()->availableGeometry();
    resize(qMin(1500, available.width() - 48), qMin(940, available.height() - 64));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
    auto *empty = new QLabel(QStringLiteral("从左侧打开会话，或将浮动窗口拖到此处停靠"), this);
    empty->setAlignment(Qt::AlignCenter); setCentralWidget(empty);
    dockPreview_ = new QLabel(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    dockPreview_->setAttribute(Qt::WA_ShowWithoutActivating); dockPreview_->setAttribute(Qt::WA_TranslucentBackground);
    dockPreview_->setAlignment(Qt::AlignCenter);
    dockPreview_->setStyleSheet("background:rgba(75,155,225,100);border:2px solid #2784c5;color:#123b60;font-size:18px;");
    auto *toolbar = addToolBar(QStringLiteral("工作区")); toolbar->setMovable(false);
    toolbar->setObjectName("workspaceBar");
    auto *controls = new QWidget(toolbar); auto *layout = new QVBoxLayout(controls);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *add = new QPushButton(QStringLiteral("新增独立会话"), controls);
    add->setObjectName("addSession");
    auto *top = new QHBoxLayout; top->addWidget(add);
    auto addArrangement = [this, top, controls](const QString &text, std::function<void()> operation) {
        auto *action = new QAction(text, this); connect(action, &QAction::triggered, this, std::move(operation));
        auto *button = new QPushButton(text, controls); connect(button, &QPushButton::clicked, action, &QAction::trigger);
        top->addWidget(button);
    };
    addArrangement(QStringLiteral("左右平铺"), [this] { tileSessions(Qt::Horizontal); });
    addArrangement(QStringLiteral("上下平铺"), [this] { tileSessions(Qt::Vertical); });
    addArrangement(QStringLiteral("独立浮窗"), [this] {
        int visible = 0; for (auto *dock : docks_) if (dock->open()) ++visible;
        int position = 0;
        for (auto *dock : docks_) if (dock->open()) {
            dock->detach(); dock->present(); dock->centerFloating((2 * position++ - visible + 1) * 12);
        }
    });
    top->addStretch(); layout->addLayout(top);
    auto *status = new QLabel(controls); status->setObjectName("workspaceStatus"); status->setWordWrap(true);
    layout->addWidget(status);
    workspace_ = std::make_unique<WorkspaceController>(*this, catalog_, std::move(workspaceExecutor));
    auto *files = new QHBoxLayout;
    auto *path = new QLineEdit(controls); path->setObjectName("workspacePath"); path->setPlaceholderText(QStringLiteral("工作区 JSON 绝对路径"));
    auto *open = new QPushButton(QStringLiteral("打开工作区（不启动通信）"), controls); open->setObjectName("workspaceOpen");
    auto *save = new QPushButton(QStringLiteral("保存 / 另存工作区"), controls); save->setObjectName("workspaceSave");
    files->addWidget(path, 1); files->addWidget(open); files->addWidget(save); layout->addLayout(files);
    toolbar->addWidget(controls);
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
    navigationDock_ = new QDockWidget(QStringLiteral("会话与设备"), this);
    navigationDock_->setObjectName("navigation"); navigationDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    navigation_ = new QTreeWidget(navigationDock_);
    navigation_->setAccessibleName(QStringLiteral("会话与设备")); navigation_->setHeaderHidden(true);
    navigation_->setMinimumWidth(160); navigationDock_->setWidget(navigation_);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock_);
    connect(navigation_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        // present() 可能刷新树；先复制用户所选身份，再触发任何窗口操作。
        const int index = item->data(0, Qt::UserRole).toInt();
        const auto device = item->data(0, Qt::UserRole + 1).toString();
        if (device.isEmpty()) selectSession(index); else selectDevice(index, device);
    });
    auto *timer = new QTimer(this); timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &Workbench::refreshNavigation); timer->start();
    connect(add, &QPushButton::clicked, this, [this] { addSession(); });
    addSession();
}
Workbench::~Workbench() { clearSessions(); }
void Workbench::clearSessions()
{
    dockPreview_->hide();
    const auto old = docks_; docks_.clear(); currentSession_ = -1;
    for (auto *dock : old) {
        dock->presentationChanged = {}; dock->requestClose = {};
        removeDockWidget(dock); delete dock;
    }
    navigationSignature_.clear(); refreshNavigation();
}
int Workbench::addSession(const SessionConfig &config, const QString &title)
{
    if (docks_.size() >= 16) return -1;
    auto *pane = new SessionPane(catalog_, this, factory_, clock_);
    pane->setProperty("workspaceBase", QDir::currentPath()); pane->loadConfiguration(config);
    const auto name = title.isEmpty() ? QStringLiteral("会话 %1").arg(nextSessionNumber_++) : title;
    auto *dock = new SessionDock(pane, name, this);
    dock->setObjectName(QStringLiteral("session_%1").arg(docks_.size()));
    const int index = docks_.size(); docks_.append(dock);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->requestClose = [this, dock] { return closeSession(docks_.indexOf(dock)); };
    dock->presentationChanged = [this] { refreshNavigation(); };
    dock->window()->dockDrag = [this, dock](QPoint position, DockDragPhase phase) { dragSession(dock, position, phase); };
    dock->detach(); currentSession_ = index;
    if (isVisible()) { dock->present(); dock->centerFloating(); }
    refreshNavigation(); return index;
}
int Workbench::sessionCount() const { return docks_.size(); }
QWidget *Workbench::sessionView(int index) const
{
    return index >= 0 && index < docks_.size() ? docks_[index]->content() : nullptr;
}
QMainWindow *Workbench::sessionWindow(int index) const
{
    return index >= 0 && index < docks_.size() ? docks_[index]->window() : nullptr;
}
QString Workbench::sessionTitle(int index) const
{
    return index >= 0 && index < docks_.size() ? docks_[index]->windowTitle() : QString{};
}
bool Workbench::isSessionOpen(int index) const
{
    return index >= 0 && index < docks_.size() && docks_[index]->open();
}
bool Workbench::isSessionFloating(int index) const
{
    return index >= 0 && index < docks_.size() && docks_[index]->detached();
}
DebugSession *Workbench::session(int index) const
{
    auto *pane = static_cast<SessionPane *>(sessionView(index)); return pane ? &pane->session_ : nullptr;
}
ParameterView *Workbench::parameterView(int index) const
{
    auto *pane = static_cast<SessionPane *>(sessionView(index)); return pane ? pane->parameterView_ : nullptr;
}
SessionConfig Workbench::sessionConfiguration(int index) const
{
    auto *pane = static_cast<SessionPane *>(sessionView(index)); return pane ? pane->configuration() : SessionConfig{};
}
int Workbench::currentSessionIndex() const { return currentSession_; }
void Workbench::selectSession(int index)
{
    if (index < 0 || index >= docks_.size()) return;
    currentSession_ = index; docks_[index]->present(); refreshNavigation();
}
bool Workbench::selectDevice(int index, const QString &deviceId)
{
    QPointer<Workbench> alive(this);
    auto *parameters = parameterView(index);
    if (!parameters || !parameters->selectDevice(deviceId) || !alive) return false;
    selectSession(index); return true;
}
void Workbench::floatSession(int index)
{
    if (index < 0 || index >= docks_.size()) return;
    auto *dock = docks_[index]; dock->detach(); selectSession(index); dock->centerFloating();
}
void Workbench::dockSession(int index)
{
    if (index < 0 || index >= docks_.size()) return;
    docks_[index]->attach(); selectSession(index);
}
void Workbench::tileSessions(Qt::Orientation orientation)
{
    dockPreview_->hide();
    QList<QDockWidget *> visible; QList<int> sizes;
    for (auto *dock : docks_) if (dock->open()) {
        dock->attach(); addDockWidget(Qt::RightDockWidgetArea, dock); dock->present();
        if (!visible.isEmpty()) splitDockWidget(visible.last(), dock, orientation);
        visible << dock; sizes << 600;
    }
    if (!visible.isEmpty()) resizeDocks(visible, sizes, orientation);
    resizeDocks({navigationDock_}, {190}, Qt::Horizontal); refreshNavigation();
}
void Workbench::dragSession(SessionDock *source, QPoint globalPosition, DockDragPhase phase)
{
    if (phase == DockDragPhase::Cancel || !source->detached() || !source->open()
        || source->window()->isHidden() || source->window()->isMinimized()
        || !isVisible() || isMinimized() || QApplication::activeModalWidget()) {
        dockPreview_->hide(); return;
    }
    const auto position = mapFromGlobal(globalPosition);
    const QRect area = rect().adjusted(navigationDock_->geometry().right() + 8,
        findChild<QToolBar *>("workspaceBar")->geometry().bottom() + 4, -8, -8);
    if (!area.contains(position) || area.width() < 2 || area.height() < 2) { dockPreview_->hide(); return; }
#ifdef Q_OS_WIN
    const auto host = reinterpret_cast<HWND>(winId());
    RECT client; POINT origin{0, 0};
    if (!GetClientRect(host, &client) || !ClientToScreen(host, &origin)) { dockPreview_->hide(); return; }
    const POINT point{origin.x + MulDiv(position.x(), client.right, width()), origin.y + MulDiv(position.y(), client.bottom, height())};
    HWND hit = nullptr;
    for (auto candidate = GetTopWindow(nullptr); candidate; candidate = GetWindow(candidate, GW_HWNDNEXT)) {
        if (candidate == reinterpret_cast<HWND>(source->window()->winId()) || candidate == reinterpret_cast<HWND>(dockPreview_->winId())
            || !IsWindowVisible(candidate) || IsIconic(candidate) || (GetWindowLongPtr(candidate, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) continue;
        DWORD cloaked = 0; DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        RECT bounds;
        if (!cloaked && GetWindowRect(candidate, &bounds) && PtInRect(&bounds, point)) { hit = candidate; break; }
    }
    if (hit != host) { dockPreview_->hide(); return; }
#endif
    SessionDock *anchor = nullptr;
    for (auto *candidate : docks_) if (candidate != source && !candidate->detached() && candidate->open()
        && candidate->geometry().contains(position)) { anchor = candidate; break; }
    QRect preview = anchor ? anchor->geometry().intersected(area) : area;
    if (preview.width() < 2 || preview.height() < 2) { dockPreview_->hide(); return; }
    int edge = 0;
    if (anchor) {
        const double distances[] = {double(position.x() - preview.left()) / preview.width(), double(preview.right() - position.x()) / preview.width(),
            double(position.y() - preview.top()) / preview.height(), double(preview.bottom() - position.y()) / preview.height()};
        for (int i = 1; i < 4; ++i) if (distances[i] < distances[edge]) edge = i;
        if (edge < 2) { const int width = preview.width() / 2; if (edge == 1) preview.setLeft(preview.right() - width + 1); else preview.setWidth(width); }
        else { const int height = preview.height() / 2; if (edge == 3) preview.setTop(preview.bottom() - height + 1); else preview.setHeight(height); }
    }
    if (phase == DockDragPhase::Preview) {
        const QStringList labels{QStringLiteral("松开后靠左平铺"), QStringLiteral("松开后靠右平铺"), QStringLiteral("松开后靠上平铺"), QStringLiteral("松开后靠下平铺")};
        dockPreview_->setText(anchor ? labels[edge] : QStringLiteral("松开后停靠到工作台"));
        dockPreview_->setGeometry(QRect(mapToGlobal(preview.topLeft()), preview.size())); dockPreview_->show(); dockPreview_->raise(); return;
    }
    dockPreview_->hide(); source->attach();
    if (anchor) {
        const auto orientation = edge < 2 ? Qt::Horizontal : Qt::Vertical;
        splitDockWidget(anchor, source, orientation);
        if (edge == 0 || edge == 2) splitDockWidget(source, anchor, orientation);
        resizeDocks({source, anchor}, {400, 400}, orientation);
    } else addDockWidget(Qt::RightDockWidgetArea, source);
    refreshNavigation();
}
void Workbench::refreshNavigation()
{
    bool anyDocked = false;
    QStringList signature;
    for (int i = 0; i < docks_.size(); ++i) {
        auto *dock = docks_[i]; anyDocked |= dock->open() && !dock->detached();
        signature << QString::number(i) << dock->windowTitle() << stateText(session(i)->state()) << QString::number(dock->open());
        if (auto *view = parameterView(i)) {
            signature << view->selectedDevice();
            for (const auto &device : view->devices()) signature << device.id << device.name;
        }
    }
    centralWidget()->setVisible(!anyDocked);
    findChild<QPushButton *>("addSession")->setEnabled(docks_.size() < 16);
    signature << QString::number(currentSession_);
    const auto key = signature.join(QChar(0x1f));
    if (key == navigationSignature_) return;
    navigationSignature_ = key;
    const QSignalBlocker blocker(navigation_); navigation_->clear();
    for (int i = 0; i < docks_.size(); ++i) {
        auto *dock = docks_[i];
        const auto suffix = dock->open() ? stateText(session(i)->state()) : QStringLiteral("已关闭 · 点击重开");
        auto *item = new QTreeWidgetItem(navigation_, {dock->windowTitle() + " · " + suffix});
        item->setData(0, Qt::UserRole, i); item->setToolTip(0, sessionConfiguration(i).protocolId);
        if (i == currentSession_) navigation_->setCurrentItem(item);
        if (auto *view = parameterView(i)) for (const auto &device : view->devices()) {
            auto *child = new QTreeWidgetItem(item, {device.name});
            child->setData(0, Qt::UserRole, i); child->setData(0, Qt::UserRole + 1, device.id);
            if (i == currentSession_ && device.id == view->selectedDevice()) navigation_->setCurrentItem(child);
        }
        item->setExpanded(true);
    }
}
quint64 Workbench::openWorkspace(const QString &path) { return workspace_->open(path); }
quint64 Workbench::saveWorkspace(const QString &path) { return workspace_->save(path); }
WorkspaceOperation Workbench::workspaceOperation(quint64 id) const { return workspace_->operation(id); }
bool Workbench::closeSession(int index)
{
    if (index < 0 || index >= docks_.size()) return false;
    QPointer<Workbench> alive(this);
    auto *dock = docks_[index]; auto *target = session(index);
    if (!target->resolveUnsaved() || !alive) return false;
    target->stop(); dock->hideSession(); refreshNavigation(); return true;
}
void Workbench::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    int visible = 0; for (auto *dock : docks_) if (dock->open()) ++visible;
    int position = 0;
    for (auto *dock : docks_) if (dock->open()) {
        dock->present();
        if (!presented_) dock->centerFloating((2 * position++ - visible + 1) * 12);
    }
    presented_ = true;
}
void Workbench::closeEvent(QCloseEvent *event)
{
    dockPreview_->hide();
    QPointer<Workbench> alive(this);
    for (int i = 0; i < sessionCount(); ++i) {
        const bool resolved = session(i)->resolveUnsaved();
        if (!alive || !resolved) { event->ignore(); return; }
    }
    // 后续模态提示期间较早会话可能有新修改，提交关闭前不泵事件地重查。
    for (int i = 0; i < sessionCount(); ++i) if (session(i)->hasUnsavedOrBusy()) {
        findChild<QLabel *>("workspaceStatus")->setText(QStringLiteral("关闭已取消：确认期间出现新的未保存修改或后台任务，数据已保留。"));
        selectSession(i); event->ignore(); return;
    }
    for (int i = 0; i < sessionCount(); ++i) { session(i)->stop(); docks_[i]->hideSession(); }
    event->accept();
}
}
