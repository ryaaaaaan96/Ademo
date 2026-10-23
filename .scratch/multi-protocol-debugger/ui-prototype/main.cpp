// PROTOTYPE ONLY. Question: can operators compare sessions, edit parameters and
// explicitly save/apply files in dockable windows? No transport or product SDK.
#include <QtWidgets>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlError>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#include <dwmapi.h>
#endif

static QString runtimeDir() {
    const auto path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../runtime");
    QDir().mkpath(path); return QDir(path).absolutePath();
}
static QString stamp() { return QTime::currentTime().toString("HH:mm:ss"); }
static QPushButton *button(const QString &text, QLayout *layout, std::function<void()> action) {
    auto *b = new QPushButton(text); layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, std::move(action)); return b;
}
static bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(data) == data.size() && file.commit();
}
struct Parameter {
    QString name, address, type = "uint16", unit, group, description;
    QString current = "—", draft, initial = "0", status = "尚未读取", updated = "—";
    bool writable = true, pending = false;
};
struct Device {
    QString name, path; int id = 1; QVector<Parameter> parameters;
    bool runtimeDirty = false;
};
static QJsonObject deviceJson(const Device &d, bool useCurrent = false) {
    QJsonArray rows;
    for (const auto &p : d.parameters) rows.append(QJsonObject{
        {"name", p.name}, {"address", p.address}, {"type", p.type}, {"unit", p.unit},
        {"group", p.group}, {"description", p.description}, {"writable", p.writable},
        {"initial", useCurrent && p.current != "—" ? p.current : p.initial}});
    return {{"prototypeFormat", 1}, {"deviceName", d.name}, {"deviceId", d.id}, {"parameters", rows}};
}
static bool parseDevice(const QJsonObject &obj, Device &d, QString &error) {
    if (obj.value("prototypeFormat").toInt() != 1 || !obj.value("parameters").isArray()) {
        error = "需要本原型的 prototypeFormat: 1 和 parameters 数组；不直接兼容产品或参考项目 JSON。"; return false;
    }
    QVector<Parameter> parameters; QSet<QString> keys;
    for (const auto value : obj.value("parameters").toArray()) {
        const auto o = value.toObject(); Parameter p;
        p.name = o.value("name").toString(); p.address = o.value("address").toString();
        p.type = o.value("type").toString("uint16"); p.unit = o.value("unit").toString();
        p.group = o.value("group").toString(); p.description = o.value("description").toString();
        p.writable = o.value("writable").toBool(true); p.initial = o.value("initial").toString("0");
        bool numeric = false; const double initial = p.initial.toDouble(&numeric);
        if (p.address.isEmpty() || keys.contains(p.address) || !numeric || !std::isfinite(initial)) {
            error = "地址不能为空或重复，初始值须为有限数值。"; return false;
        }
        keys.insert(p.address); parameters.append(p);
    }
    if (parameters.isEmpty() || parameters.size() > 256) { error = "原型支持 1–256 行参数。"; return false; }
    d.parameters = parameters; return true;
}
static Device sampleDevice(int id, bool slave, const QString &tag) {
    Device d; d.id = id; d.name = QString("设备 %1 · %2").arg(id).arg(id == 1 ? "电源模块" : "温控模块");
    const QStringList names = {"输出电压", "输出电流", "目标电压", "温度", "", "运行模式"};
    const QStringList values = {"220", "12", "220", "36", "0", "1"};
    const QStringList units = {"V", "A", "V", "°C", "", ""};
    for (int i = 0; i < names.size(); ++i) {
        Parameter p; p.name = names[i]; p.address = QString("HR %1").arg(i);
        p.unit = units[i]; p.initial = values[i]; p.group = i < 4 ? "运行参数" : "基本配置";
        p.description = i == 4 ? "未命名参数：显示寄存器地址" : "演示参数，可在文件编辑页调整";
        p.writable = i != 3; p.current = slave ? p.initial : "—";
        p.status = slave ? "模拟值已加载" : "尚未读取"; d.parameters.append(p);
    }
    d.path = runtimeDir() + QString("/%1-device%2.json").arg(tag).arg(id);
    // Fresh fixture per demo session. Only writes inside this prototype's runtime directory.
    writeJson(d.path, deviceJson(d)); return d;
}
class ValueDelegate : public QStyledItemDelegate {
public:
    int *editingRow;
    ValueDelegate(int *row, QObject *parent) : QStyledItemDelegate(parent), editingRow(row) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        *editingRow = index.row(); return QStyledItemDelegate::createEditor(parent, option, index);
    }
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override {
        *editingRow = -1; QStyledItemDelegate::destroyEditor(editor, index);
    }
};
class PlotWindow : public QMainWindow {
public:
    QQuickWidget *quick; QLabel *state; QVector<int> rows; int deviceIndex;
    PlotWindow(const QString &title, int device, QWidget *parent) : QMainWindow(parent, Qt::Window), deviceIndex(device) {
        setAttribute(Qt::WA_DeleteOnClose); setWindowTitle(title + " · 趋势 / 模拟数据"); resize(860, 480);
        auto *content = new QWidget; setCentralWidget(content);
        auto *layout = new QVBoxLayout(content); auto *bar = new QHBoxLayout;
        state = new QLabel("等待参数更新；打开曲线不会自动启动轮询"); bar->addWidget(state, 1);
        auto *pause = new QPushButton("暂停显示"); pause->setCheckable(true); bar->addWidget(pause);
        button("清空", bar, [this] { if (quick->rootObject()) QMetaObject::invokeMethod(quick->rootObject(), "clearSamples"); });
        button("缩小", bar, [this] { zoom(1.5); }); button("放大", bar, [this] { zoom(0.67); });
        layout->addLayout(bar);
        quick = new QQuickWidget; quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
        quick->setSource(QUrl("qrc:/Chart.qml")); layout->addWidget(quick, 1);
        QObject::connect(pause, &QPushButton::toggled, this, [this, pause](bool checked) {
            if (quick->rootObject()) quick->rootObject()->setProperty("paused", checked);
            pause->setText(checked ? "继续显示" : "暂停显示");
            state->setText(checked ? "显示暂停；通信与轮询继续" : "等待下一次采样更新");
        });
        if (quick->status() == QQuickWidget::Error) {
            QStringList errors; for (const auto &e : quick->errors()) errors << e.toString();
            state->setText(errors.join("\n"));
        }
    }
    void zoom(double factor) { if (quick->rootObject()) QMetaObject::invokeMethod(quick->rootObject(), "zoomBy", Q_ARG(QVariant, QVariant(factor))); }
    void addRows(const QVector<int> &newRows, const Device &device) {
        for (int row : newRows) if (!rows.contains(row) && rows.size() < 4) rows.append(row);
        QStringList names; for (int row : rows) {
            const auto &p = device.parameters[row]; names << (p.name.isEmpty() ? p.address : p.name) + (p.unit.isEmpty() ? "" : " / " + p.unit);
        }
        if (quick->rootObject()) quick->rootObject()->setProperty("names", names);
    }
    void append(const Device &device) {
        QVariantList values;
        for (int row : rows) { bool ok = false; double v = device.parameters[row].current.toDouble(&ok); if (!ok) return; values << v; }
        if (quick->rootObject()) QMetaObject::invokeMethod(quick->rootObject(), "appendSample", Q_ARG(QVariant, QVariant(values)));
        if (!quick->rootObject() || !quick->rootObject()->property("paused").toBool()) state->setText("最近采样 " + stamp() + " · 模拟数据");
    }
};

class SessionPane : public QWidget {
public:
    QString sessionName, protocol, endpoint; bool slave = false, running = false;
    QVector<Device> devices; int selected = 0, editorDevice = 0, editingRow = -1, step = 0;
    bool fileDirty = false, updating = false; QJsonObject editorBase; QString editorPath;
    QComboBox *deviceBox, *groups, *pollScope, *outcome; QSpinBox *period;
    QPushButton *startButton, *pollButton; QLabel *sessionState, *endpointSummary, *notice, *editorTarget, *fileState, *recordSummary;
    QTabWidget *pages; QTableWidget *table, *editor; QPlainTextEdit *records;
    QTimer pollTimer; QHash<int, QVector<int>> polling; QVector<QPointer<PlotWindow>> plots;
    QPointer<QWidget> plotOwner;
    QVector<int> visibleRows; std::function<void()> changed;

    SessionPane(QString name, QString proto, QString port, bool isSlave, QWidget *parent = nullptr)
        : QWidget(parent), sessionName(name), protocol(proto), endpoint(port), slave(isSlave) {
        auto *root = new QVBoxLayout(this); root->setContentsMargins(12, 10, 12, 8); root->setSpacing(8);
        auto *header = new QHBoxLayout; sessionState = new QLabel("● 已停止"); sessionState->setObjectName("connectionState");
        endpointSummary = new QLabel(endpoint + " · " + (slave ? "从站模拟" : "主站") + " · " + protocol);
        endpointSummary->setWordWrap(true);
        header->addWidget(sessionState); header->addWidget(endpointSummary, 1);
        startButton = button("启动", header, [this] { if (running) stop(); else start(); });
        button("连接配置", header, [this] { configure(); }); root->addLayout(header);
        auto *deviceBar = new QHBoxLayout; deviceBar->addWidget(new QLabel("设备")); deviceBox = new QComboBox; deviceBox->setMinimumContentsLength(12);
        deviceBar->addWidget(deviceBox, 1); button("＋ 添加设备", deviceBar, [this] { addDeviceDialog(); });
        button("文件编辑", deviceBar, [this] { pages->setCurrentIndex(1); }); root->addLayout(deviceBar);
        pages = new QTabWidget; root->addWidget(pages, 1);
        auto *operationPage = new QWidget; auto *op = new QVBoxLayout(operationPage); op->setContentsMargins(8, 10, 8, 8);
        auto *readBar = new QHBoxLayout; groups = new QComboBox; readBar->addWidget(groups, 1);
        auto *readSelected = button("读取选中", readBar, [this] { read(selectedRows()); }); readSelected->setEnabled(!slave);
        auto *readGroup = button("读取本组", readBar, [this] { read(visibleRows); }); readGroup->setEnabled(!slave);
        button("绘图", readBar, [this] { openPlot(selectedRows()); });
        auto *columns = new QToolButton; columns->setText("显示列"); columns->setPopupMode(QToolButton::InstantPopup); readBar->addWidget(columns);
        op->addLayout(readBar);
        auto *pollBar = new QHBoxLayout; pollScope = new QComboBox; pollScope->addItems({"轮询当前组", "轮询选中项"}); pollBar->addWidget(pollScope);
        period = new QSpinBox; period->setRange(100, 60000); period->setValue(1000); period->setSuffix(" ms"); pollBar->addWidget(period);
        pollButton = button("开始轮询", pollBar, [this] { togglePolling(); });
        outcome = new QComboBox; outcome->addItems({"模拟：成功", "模拟：超时"}); pollBar->addStretch(); pollBar->addWidget(outcome);
        pollScope->setEnabled(!slave); period->setEnabled(!slave); pollButton->setEnabled(!slave); op->addLayout(pollBar);
        table = new QTableWidget(0, 7); table->setObjectName("parameterTable");
        table->setHorizontalHeaderLabels({"名称 / 地址", "类型", "说明", "值", "单位", "操作", "状态 / 更新时间"});
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        table->setItemDelegateForColumn(3, new ValueDelegate(&editingRow, table)); table->verticalHeader()->hide();
        table->setAlternatingRowColors(true); table->verticalHeader()->setDefaultSectionSize(40);
        table->horizontalHeader()->setStretchLastSection(true); table->setColumnWidth(0, 120); table->setColumnWidth(1, 60);
        table->setColumnWidth(2, 170); table->setColumnWidth(3, 90); table->setColumnWidth(4, 48); table->setColumnWidth(5, 114);
        table->hideColumn(1); table->hideColumn(2); op->addWidget(table, 1);
        auto *columnMenu = new QMenu(columns); columns->setMenu(columnMenu);
        for (int i = 0; i < 7; ++i) { auto *a = columnMenu->addAction(table->horizontalHeaderItem(i)->text()); a->setCheckable(true); a->setChecked(!table->isColumnHidden(i)); connect(a, &QAction::toggled, this, [this, i](bool on) { table->setColumnHidden(i, !on); }); }
        auto *foot = new QHBoxLayout;
        button("取消选中待提交值", foot, [this] { for (int i : selectedRows()) devices[selected].parameters[i].pending = false; render(); });
        if (slave) button("保存运行值到文件", foot, [this] { saveRuntime(selected); });
        foot->addStretch(); op->addLayout(foot);
        notice = new QLabel(slave ? "双击值立即修改模拟数据；保存文件独立执行。" : "双击值 → 编辑 → 点击设置。黄色值尚未提交，轮询不会覆盖。 ");
        notice->setWordWrap(true); notice->setObjectName("hint"); op->addWidget(notice); pages->addTab(operationPage, "参数操作");
        makeEditor();
        auto *logHeader = new QHBoxLayout; auto *toggle = new QToolButton; toggle->setText("通信记录（收起）"); toggle->setCheckable(true); toggle->setChecked(true);
        logHeader->addWidget(toggle); recordSummary = new QLabel("本会话 · 仅模拟事件"); logHeader->addWidget(recordSummary, 1);
        button("清空显示", logHeader, [this] { records->clear(); }); root->addLayout(logHeader);
        records = new QPlainTextEdit; records->setReadOnly(true); records->setMaximumBlockCount(300); records->setFixedHeight(112);
        records->setObjectName("records"); root->addWidget(records);
        connect(toggle, &QToolButton::toggled, this, [this, toggle](bool on) { records->setVisible(on); toggle->setText(on ? "通信记录（收起）" : "通信记录（展开）"); });
        connect(groups, &QComboBox::currentTextChanged, this, [this] { render(); });
        connect(deviceBox, &QComboBox::currentIndexChanged, this, [this](int index) { selectDevice(index); });
        connect(pages, &QTabWidget::currentChanged, this, [this](int page) { if (page == 1 && !fileDirty) loadEditor(selected); });
        connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) { editValue(item); });
        connect(&pollTimer, &QTimer::timeout, this, [this] {
            if (!running) return;
            const auto targets = polling;
            for (auto it = targets.cbegin(); it != targets.cend(); ++it) read(it.value(), it.key());
        });
        connect(period, &QSpinBox::valueChanged, this, [this](int interval) { pollTimer.setInterval(interval); });
    }
    ~SessionPane() override { for (auto plot : plots) if (plot) delete plot.data(); }
    void log(const QString &text) { records->appendPlainText(stamp() + "  " + text); }
    QString displayName(const Parameter &p) const { return p.name.isEmpty() ? p.address : p.name; }
    void addDevice(Device device) {
        devices.append(device); QSignalBlocker block(deviceBox); deviceBox->addItem(device.name + QString(" [地址 %1]").arg(device.id));
        if (devices.size() == 1) { selected = 0; rebuildGroups(); loadEditor(0); }
        if (changed) changed();
    }
    bool guardEditor() {
        if (!fileDirty) return true;
        auto answer = QMessageBox::question(this, "文件有未保存修改", "保存文件编辑页的修改？保存不会应用到运行数据。", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save && !saveEditor(false)) return false;
        fileDirty = false; return true;
    }
    void selectDevice(int index) {
        if (index < 0 || index >= devices.size() || index == selected) return;
        if (!guardEditor()) { QSignalBlocker block(deviceBox); deviceBox->setCurrentIndex(selected); return; }
        selected = index; rebuildGroups(); loadEditor(selected);
    }
    void rebuildGroups() {
        if (devices.isEmpty()) return;
        QSignalBlocker block(groups); groups->clear();
        for (const auto &p : devices[selected].parameters) { const QString group = p.group.isEmpty() ? "默认组" : p.group; if (groups->findText(group) < 0) groups->addItem(group); }
        render();
    }
    void render() {
        if (devices.isEmpty()) return;
        updating = true; QSignalBlocker blocker(table); table->setRowCount(0); visibleRows.clear(); editingRow = -1;
        const auto &device = devices[selected];
        for (int i = 0; i < device.parameters.size(); ++i) {
            const auto &p = device.parameters[i]; const auto group = p.group.isEmpty() ? "默认组" : p.group;
            if (group != groups->currentText()) continue;
            const int row = table->rowCount(); table->insertRow(row); visibleRows.append(i);
            QStringList cells = {displayName(p), p.type, p.description, p.pending ? p.draft : p.current, p.unit, "", p.status + "\n" + p.updated};
            for (int col = 0; col < 7; ++col) {
                auto *item = new QTableWidgetItem(cells[col]); item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (col == 3 && (slave || p.writable)) item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setToolTip(p.address + " · " + p.description + "\n最近观测值：" + p.current); table->setItem(row, col, item);
            }
            auto *cell = new QWidget; auto *actions = new QHBoxLayout(cell); actions->setContentsMargins(2, 2, 2, 2); actions->setSpacing(4);
            if (!slave) {
                auto *readButton = button("读", actions, [this, i] { read({i}); }); readButton->setEnabled(running);
                if (p.writable) { auto *writeButton = button("设置", actions, [this, i] { writeValue(i); }); writeButton->setEnabled(running && p.pending); }
            } else { auto *label = new QLabel("即时编辑"); actions->addWidget(label); }
            table->setCellWidget(row, 5, cell);
        }
        updating = false; refreshValues();
        pollButton->setText(polling.contains(selected) ? "停止该设备轮询" : "开始轮询");
    }
    void refreshValues() {
        if (devices.isEmpty()) return;
        QSignalBlocker blocker(table);
        for (int row = 0; row < visibleRows.size(); ++row) {
            const auto &p = devices[selected].parameters[visibleRows[row]];
            if (row != editingRow) table->item(row, 3)->setText(p.pending ? p.draft : p.current);
            table->item(row, 3)->setBackground(p.pending ? QColor("#fff0c2") : QColor(Qt::transparent));
            table->item(row, 3)->setToolTip("最近观测值：" + p.current + (p.pending ? "\n当前显示为待提交值" : ""));
            table->item(row, 6)->setText(p.status + "\n" + p.updated);
            if (auto *cell = table->cellWidget(row, 5)) for (auto *b : cell->findChildren<QPushButton *>()) b->setEnabled(running && (b->text() != "设置" || p.pending));
        }
    }
    QVector<int> selectedRows() const {
        QVector<int> rows; for (auto index : table->selectionModel()->selectedRows()) if (index.row() < visibleRows.size()) rows.append(visibleRows[index.row()]);
        return rows;
    }
    void editValue(QTableWidgetItem *item) {
        if (updating || item->column() != 3 || item->row() >= visibleRows.size()) return;
        auto &p = devices[selected].parameters[visibleRows[item->row()]];
        bool ok = false; const double value = item->text().toDouble(&ok);
        if (!ok || !std::isfinite(value) || (p.type == "uint16" && (value < 0 || value > 65535 || std::floor(value) != value))) {
            notice->setText("数值无效：uint16 需要 0–65535 的整数。原值保留。"); refreshValues(); return;
        }
        if (slave) { p.current = item->text(); p.status = "运行值已改 · 未保存"; p.updated = stamp(); devices[selected].runtimeDirty = true; log("本地模拟值修改 " + p.address + " = " + p.current); }
        else { p.draft = item->text(); p.pending = true; p.status = "待提交"; }
        refreshValues();
    }
    bool start() {
        if (devices.isEmpty()) { notice->setText("请先添加设备。"); return false; }
        if (slave) for (auto &d : devices) {
            QFile file(d.path); QString error; Device loaded = d;
            if (!file.open(QIODevice::ReadOnly) || !parseDevice(QJsonDocument::fromJson(file.readAll()).object(), loaded, error)) {
                notice->setText("从站启动失败：" + d.name + " · 文件缺失或无效 · " + d.path); return false;
            }
        }
        if (slave) {
            if (!guardRuntime()) return false;
            auto loaded = devices;
            for (auto &d : loaded) {
                QFile f(d.path); QString error;
                if (!f.open(QIODevice::ReadOnly) || !parseDevice(QJsonDocument::fromJson(f.readAll()).object(), d, error)) {
                    notice->setText("启动文件校验失败，原运行数据保留：" + d.path); return false;
                }
                for (auto &p : d.parameters) { p.current = p.initial; p.status = "模拟值已加载"; }
            }
            devices = loaded;
            rebuildGroups();
        }
        running = true; startButton->setText("停止"); sessionState->setText("● 运行中"); sessionState->setStyleSheet("color:#087a66;font-weight:600");
        log("会话启动 · 模拟端点 " + endpoint + "（不访问硬件）"); refreshValues(); if (changed) changed(); return true;
    }
    void stop() {
        running = false; polling.clear(); pollTimer.stop(); startButton->setText("启动"); pollButton->setText("开始轮询");
        sessionState->setText("● 已停止"); sessionState->setStyleSheet("color:#63758a");
        for (auto plot : plots) if (plot) plot->state->setText("会话已停止，保留已有曲线");
        log("会话已停止 · 轮询停止，配置保留"); refreshValues(); if (changed) changed();
    }
    void read(QVector<int> rows, int device = -1) {
        if (!running || slave) { notice->setText("请先启动主站会话。"); return; }
        if (device < 0) device = selected;
        if (rows.isEmpty()) { notice->setText("请先选择参数行。"); return; }
        ++step; auto &d = devices[device];
        for (int row : rows) {
            if (row >= d.parameters.size()) continue; auto &p = d.parameters[row];
            if (outcome->currentIndex() == 1) { p.status = p.pending ? "待提交 · 读取超时" : "读取超时 · 旧值"; continue; }
            const double base = p.initial.toDouble();
            p.current = QString::number(qMax(0.0, std::round(base + std::sin(step * 0.24 + row) * (row < 4 ? 3.0 : 0.0))));
            p.status = p.pending ? "待提交 · 收到新观测" : "读取成功"; p.updated = stamp();
        }
        log(QString("%1 · %2 · %3 项 · %4").arg(d.name, "模拟读取").arg(rows.size()).arg(outcome->currentIndex() ? "超时；原观测值保留" : "收到响应"));
        if (device == selected) refreshValues();
        if (!outcome->currentIndex()) for (auto plot : plots) if (plot && plot->deviceIndex == device) plot->append(d);
    }
    void writeValue(int row) {
        if (!running || slave) return; auto &p = devices[selected].parameters[row]; if (!p.pending) return;
        log("模拟设置 " + p.address + " = " + p.draft + " · 只发送一次");
        if (outcome->currentIndex()) { p.status = "结果不确定 · 未自动重试"; notice->setText("模拟超时：保留待提交值，请主动读取核实或决定再次设置。"); }
        else { p.current = p.draft; p.initial = p.draft; p.pending = false; p.status = "写入已确认"; p.updated = stamp(); }
        refreshValues();
    }
    void togglePolling() {
        if (!running || slave) { notice->setText("请先启动主站会话。"); return; }
        if (polling.contains(selected)) polling.remove(selected);
        else { auto rows = pollScope->currentIndex() ? selectedRows() : visibleRows; if (rows.isEmpty()) { notice->setText("请先选择轮询参数。"); return; } polling[selected] = rows; }
        if (polling.isEmpty()) pollTimer.stop(); else pollTimer.start(period->value());
        pollButton->setText(polling.contains(selected) ? "停止该设备轮询" : "开始轮询");
        log(QString("轮询配置更新 · %1 个设备 · %2 ms；切换视图不会停止").arg(polling.size()).arg(period->value()));
    }
    void openPlot(QVector<int> rows) {
        if (rows.isEmpty()) { notice->setText("选中一行或多行，再点击绘图。最多四条曲线。"); return; }
        PlotWindow *plot = nullptr; for (auto candidate : plots) if (candidate && candidate->deviceIndex == selected) plot = candidate;
        if (!plot) { plot = new PlotWindow(sessionName + " / " + devices[selected].name, selected, plotOwner); plots.append(plot); }
        plot->addRows(rows, devices[selected]); plot->show(); plot->raise();
    }

    void makeEditor();
    void fillEditor(const Device &device, const QJsonObject &base, const QString &path);
    void loadEditor(int device);
    QJsonObject editorJson() const;
    bool saveEditor(bool saveAs);
    void applyFile();
    void importFile();
    void generateRows();
    bool saveRuntime(int device);
    bool guardRuntime();
    bool canClose();
    void configure();
    void addDeviceDialog();
};

void SessionPane::makeEditor() {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(8, 10, 8, 8);
    editorTarget = new QLabel; editorTarget->setWordWrap(true); layout->addWidget(editorTarget);
    auto *bar = new QHBoxLayout;
    button("打开文件", bar, [this] { importFile(); });
    button("生成地址行", bar, [this] { generateRows(); });
    button("＋ 参数", bar, [this] {
        QSignalBlocker block(editor); int row = editor->rowCount(); editor->insertRow(row);
        const QStringList values = {"", "HR " + QString::number(row), "uint16", "", "", "0", "RW", ""};
        for (int col = 0; col < values.size(); ++col) editor->setItem(row, col, new QTableWidgetItem(values[col]));
        fileDirty = true; fileState->setText("有未保存修改 · 尚未应用");
    });
    button("删除选中", bar, [this] {
        auto rows = editor->selectionModel()->selectedRows(); std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a.row() > b.row(); });
        for (auto index : rows) editor->removeRow(index.row()); fileDirty = true; fileState->setText("有未保存修改 · 尚未应用");
    });
    bar->addStretch(); layout->addLayout(bar);
    editor = new QTableWidget(0, 8); editor->setObjectName("fileEditorTable");
    editor->setHorizontalHeaderLabels({"名称", "地址", "类型", "单位", "分组", "初始值", "权限", "说明"});
    editor->verticalHeader()->hide(); editor->setAlternatingRowColors(true);
    editor->setSelectionBehavior(QAbstractItemView::SelectRows); editor->horizontalHeader()->setStretchLastSection(true);
    for (int col = 0; col < 8; ++col) editor->setColumnWidth(col, col == 0 ? 115 : 80);
    layout->addWidget(editor, 1);
    fileState = new QLabel("文件编辑不改变运行数据"); fileState->setWordWrap(true); layout->addWidget(fileState);
    auto *saveBar = new QHBoxLayout;
    button("保存文件", saveBar, [this] { saveEditor(false); });
    button("另存为", saveBar, [this] { saveEditor(true); }); saveBar->addStretch();
    auto *apply = button("加载／应用到该设备", saveBar, [this] { applyFile(); }); apply->setObjectName("primary");
    layout->addLayout(saveBar);
    auto *help = new QLabel("保存 ≠ 应用 ≠ 设备设置。此文件仅供原型演示，不兼容产品或参考项目格式。"); help->setObjectName("hint"); help->setWordWrap(true); layout->addWidget(help);
    pages->addTab(page, "文件编辑");
    connect(editor, &QTableWidget::itemChanged, this, [this] { fileDirty = true; fileState->setText("有未保存修改 · 运行数据尚未改变"); });
}
void SessionPane::fillEditor(const Device &device, const QJsonObject &base, const QString &path) {
    QSignalBlocker blocker(editor); editorBase = base; editorPath = path; editor->setRowCount(device.parameters.size());
    for (int row = 0; row < device.parameters.size(); ++row) {
        const auto &p = device.parameters[row]; const QStringList values = {p.name, p.address, p.type, p.unit, p.group, p.initial, p.writable ? "RW" : "R", p.description};
        for (int col = 0; col < values.size(); ++col) editor->setItem(row, col, new QTableWidgetItem(values[col]));
    }
    fileDirty = false;
    editorTarget->setText("目标：" + devices[editorDevice].name + "\n编辑文件：" + QDir::toNativeSeparators(editorPath));
    fileState->setText("编辑区与运行数据独立 · 保存后可显式加载／应用");
}
void SessionPane::loadEditor(int device) {
    if (device < 0 || device >= devices.size()) return; editorDevice = device;
    Device contents = devices[device]; QJsonObject data = deviceJson(contents);
    QFile file(contents.path); QString error;
    if (file.open(QIODevice::ReadOnly)) { const auto loaded = QJsonDocument::fromJson(file.readAll()).object(); if (parseDevice(loaded, contents, error)) data = loaded; }
    fillEditor(contents, data, devices[device].path);
}
QJsonObject SessionPane::editorJson() const {
    auto object = editorBase; QJsonArray rows;
    const QStringList keys = {"name", "address", "type", "unit", "group", "initial", "writable", "description"};
    for (int row = 0; row < editor->rowCount(); ++row) {
        QJsonObject p;
        for (int col = 0; col < 8; ++col) {
            const QString value = editor->item(row, col) ? editor->item(row, col)->text() : "";
            if (col == 6) p[keys[col]] = value.trimmed().toUpper() == "RW"; else p[keys[col]] = value;
        }
        rows.append(p);
    }
    object["prototypeFormat"] = 1; object["parameters"] = rows;
    object["deviceName"] = devices[editorDevice].name; object["deviceId"] = devices[editorDevice].id; return object;
}
bool SessionPane::saveEditor(bool saveAs) {
    if (devices.isEmpty()) return false;
    const auto data = editorJson(); Device check = devices[editorDevice]; QString error;
    if (!parseDevice(data, check, error)) { fileState->setText("保存失败：" + error); return false; }
    QString path = editorPath;
    // Never overwrite a user-selected import outside the disposable runtime directory.
    if (saveAs || path.isEmpty() || !QFileInfo(path).absoluteFilePath().startsWith(runtimeDir() + "/", Qt::CaseInsensitive))
        path = QFileDialog::getSaveFileName(this, "另存原型配置（建议放在 runtime）", runtimeDir() + "/edited-device.json", "JSON (*.json)");
    if (path.isEmpty()) return false;
    if (!writeJson(path, data)) { fileState->setText("保存失败，编辑内容保留。"); return false; }
    editorPath = path; fileDirty = false;
    editorTarget->setText("目标：" + devices[editorDevice].name + "\n编辑文件：" + QDir::toNativeSeparators(path));
    fileState->setText("文件已保存 · 尚未应用，运行数据未改变"); log("文件已保存（没有发送设备设置，也没有应用）"); return true;
}
void SessionPane::applyFile() {
    if (devices.isEmpty()) return;
    if (fileDirty) { fileState->setText("请先保存文件，再明确点击加载／应用。"); return; }
    QFile file(editorPath); QString error; Device next = devices[editorDevice];
    if (!file.open(QIODevice::ReadOnly) || !parseDevice(QJsonDocument::fromJson(file.readAll()).object(), next, error)) {
        fileState->setText("应用失败，运行数据保留：" + error); return;
    }
    bool pending = devices[editorDevice].runtimeDirty;
    for (const auto &p : devices[editorDevice].parameters) pending |= p.pending;
    if (pending && QMessageBox::question(this, "替换运行数据", "目标设备有待提交值或未保存模拟值。放弃这些值并加载文件？", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    next.path = editorPath; next.runtimeDirty = false;
    for (auto &p : next.parameters) { p.current = slave ? p.initial : "—"; p.status = slave ? "模拟值已加载" : "尚未读取"; }
    for (auto plot : plots) if (plot && plot->deviceIndex == editorDevice) plot->close();
    polling.remove(editorDevice); if (polling.isEmpty()) pollTimer.stop();
    devices[editorDevice] = next; if (editorDevice == selected) rebuildGroups();
    fileState->setText("已应用到 " + next.name + " · 该设备原轮询已停止，请按新分组重新选择");
    log("显式加载文件到 " + next.name + "；没有执行设备设置，其他设备不受影响");
}
void SessionPane::importFile() {
    if (devices.isEmpty() || !guardEditor()) return;
    const auto path = QFileDialog::getOpenFileName(this, "打开原型参数文件", runtimeDir(), "JSON (*.json)"); if (path.isEmpty()) return;
    QFile file(path); Device contents = devices[editorDevice]; QString error;
    if (!file.open(QIODevice::ReadOnly)) { fileState->setText("文件不可读。"); return; }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (!parseDevice(object, contents, error)) { fileState->setText("导入失败，原数据保留：" + error); return; }
    fillEditor(contents, object, path); fileState->setText("文件已打开，尚未应用到运行设备。");
}
void SessionPane::generateRows() {
    if (devices.isEmpty() || !guardEditor()) return;
    QDialog dialog(this); dialog.setWindowTitle("生成 Modbus 参数行"); auto *layout = new QFormLayout(&dialog);
    auto *area = new QComboBox; area->addItems({"HR", "IR", "Coil", "DI"}); layout->addRow("数据区", area);
    auto *first = new QSpinBox; first->setRange(0, 65535); layout->addRow("起始地址（PDU）", first);
    auto *count = new QSpinBox; count->setRange(1, 256); count->setValue(12); layout->addRow("数量", count);
    auto *group = new QLineEdit; group->setPlaceholderText("留空使用默认组"); layout->addRow("分组", group);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    if (first->value() + count->value() > 65536) { fileState->setText("地址范围超出 65535。"); return; }
    Device next = devices[editorDevice]; next.parameters.clear();
    for (int i = 0; i < count->value(); ++i) { Parameter p; p.address = area->currentText() + " " + QString::number(first->value() + i); p.group = group->text(); p.writable = area->currentIndex() == 0 || area->currentIndex() == 2; next.parameters.append(p); }
    fillEditor(next, deviceJson(next), editorPath); fileDirty = true; fileState->setText("地址行已生成 · 请保存后应用");
}
bool SessionPane::saveRuntime(int device) {
    if (!slave) return true;
    if (device < 0 || device >= devices.size()) {
        notice->setText("请先添加设备，再保存模拟运行值。"); return false;
    }
    auto &d = devices[device];
    if (fileDirty && editorDevice == device) { notice->setText("请先处理文件编辑页的未保存修改，再保存运行值。"); return false; }
    const auto path = QFileDialog::getSaveFileName(this, "保存模拟运行值", d.path, "JSON (*.json)"); if (path.isEmpty()) return false;
    if (!writeJson(path, deviceJson(d, true))) return false;
    d.path = path; d.runtimeDirty = false; for (auto &p : d.parameters) { p.initial = p.current; p.status = "模拟值已保存"; }
    if (device == selected) { refreshValues(); loadEditor(device); } log("模拟值已保存到文件，没有发送设备设置"); return true;
}
bool SessionPane::guardRuntime() {
    for (int i = 0; i < devices.size(); ++i) if (devices[i].runtimeDirty) {
        const auto answer = QMessageBox::question(this, "未保存的模拟值", devices[i].name + " 的模拟值尚未保存。", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save && !saveRuntime(i)) return false;
        devices[i].runtimeDirty = false;
    }
    return true;
}
bool SessionPane::canClose() {
    if (!guardEditor() || !guardRuntime()) return false;
    bool pending = false; for (const auto &d : devices) for (const auto &p : d.parameters) pending |= p.pending;
    if (pending && QMessageBox::question(this, "还有待提交值", "关闭会停止通信并放弃待提交值，保留会话配置。是否继续？", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return false;
    for (auto &d : devices) for (auto &p : d.parameters) p.pending = false;
    stop(); for (auto plot : plots) if (plot) plot->close(); return true;
}
void SessionPane::configure() {
    if (running) { notice->setText("修改连接参数前请先停止此会话。"); return; }
    QDialog dialog(this); dialog.setWindowTitle("连接配置 · 不访问真实硬件"); auto *layout = new QFormLayout(&dialog);
    auto *port = new QLineEdit(endpoint); layout->addRow("演示端点", port);
    auto *baud = new QComboBox; baud->addItems({"115200", "57600", "9600"}); layout->addRow("波特率", baud);
    layout->addRow(new QLabel("原型仅保存端点标签，串口参数和真实连接由正式实现承接。"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        endpoint = port->text();
        endpointSummary->setText(endpoint + " · " + (slave ? "从站模拟" : "主站") + " · " + protocol);
        notice->setText("端点配置已改为 " + endpoint + " · 未启动通信"); if (changed) changed();
    }
}
void SessionPane::addDeviceDialog() {
    if (running) { notice->setText("原型请先停止此会话，再添加设备。"); return; }
    if (!guardEditor()) return;
    bool ok = false; int id = QInputDialog::getInt(this, "添加设备", "设备地址（1–247）", devices.size() + 1, 1, 247, 1, &ok); if (!ok) return;
    for (const auto &d : devices) if (d.id == id) { notice->setText("设备地址已存在。"); return; }
    Device device; device.id = id; device.name = QString("设备 %1").arg(id); device.path = runtimeDir() + QString("/new-%1-%2.json").arg(reinterpret_cast<quintptr>(this)).arg(id);
    Parameter p; p.address = "HR 0"; device.parameters.append(p); addDevice(device);
    deviceBox->setCurrentIndex(devices.size() - 1); pages->setCurrentIndex(1); fileDirty = true;
    fileState->setText("新设备尚无文件：打开已有配置，或生成地址行后保存并应用。");
}

enum class DockDragPhase { Preview, Drop, Cancel };
class SessionWindow : public QMainWindow {
public:
    SessionPane *pane;
    std::function<void()> closed;
    std::function<void(QPoint, DockDragPhase)> dockDrag;
#ifdef Q_OS_WIN
    bool captionMoving = false;
    QRect dragStartGeometry;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override {
        const auto *msg = static_cast<MSG *>(message);
        if (isWindow() && dockDrag) {
            if (msg->message == WM_ENTERSIZEMOVE) { captionMoving = false; dragStartGeometry = frameGeometry(); }
            if (msg->message == WM_MOVING) {
                captionMoving = true; dockDrag(QCursor::pos(), DockDragPhase::Preview);
            }
            if (msg->message == WM_EXITSIZEMOVE || msg->message == WM_CANCELMODE) {
                const bool drop = msg->message == WM_EXITSIZEMOVE && captionMoving
                    && frameGeometry() != dragStartGeometry && !(GetAsyncKeyState(VK_ESCAPE) & 0x8000);
                captionMoving = false; const auto position = QCursor::pos();
                QTimer::singleShot(0, this, [this, position, drop] {
                    if (dockDrag) dockDrag(position, drop ? DockDragPhase::Drop : DockDragPhase::Cancel);
                });
            }
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
#endif
    SessionWindow(SessionPane *p, QWidget *parent) : QMainWindow(parent), pane(p) {
        setWindowTitle(pane->sessionName + " · 模拟数据");
        auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(pane); setCentralWidget(scroll); setMinimumSize(420, 320);
    }
    void closeEvent(QCloseEvent *event) override {
        if (!pane->canClose()) { event->ignore(); return; }
        QMainWindow::closeEvent(event); if (closed) closed();
    }
};
class SessionDock : public QDockWidget {
public:
    SessionPane *pane; SessionWindow *sessionWindow;
    QWidget *container; QVBoxLayout *containerLayout;
    bool detached = false, changing = false;
    std::function<void()> presentationChanged;
    SessionDock(SessionPane *p, QWidget *parent) : QDockWidget(p->sessionName, parent), pane(p) {
        container = new QWidget(this); containerLayout = new QVBoxLayout(container); containerLayout->setContentsMargins(0, 0, 0, 0);
        sessionWindow = new SessionWindow(pane, container); sessionWindow->setWindowFlags(Qt::Widget);
        containerLayout->addWidget(sessionWindow); setWidget(container);
        setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        setMinimumSize(420, 320); setAllowedAreas(Qt::AllDockWidgetAreas);
        sessionWindow->closed = [this] { if (presentationChanged) presentationChanged(); };
        auto *dockAction = sessionWindow->menuBar()->addAction("停靠到工作台");
        connect(dockAction, &QAction::triggered, this, [this] { attach(); });
        connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
            if (floating && !changing) QTimer::singleShot(0, this, [this] { if (QDockWidget::isFloating()) detach(); });
        });
    }
    ~SessionDock() override { delete sessionWindow; }
    bool sessionHidden() const { return detached ? sessionWindow->isHidden() : isHidden(); }
    void showSession() {
        if (detached) {
            if (sessionWindow->isMinimized()) sessionWindow->showNormal(); else sessionWindow->show();
            sessionWindow->raise(); sessionWindow->activateWindow();
        } else { show(); raise(); }
    }
    void hideSession() { if (detached) sessionWindow->hide(); else hide(); }
    void centerFloatingWindow(int offset = 0) {
        const auto available = parentWidget()->screen()->availableGeometry();
        const auto frameExtra = sessionWindow->frameGeometry().size() - sessionWindow->size();
        sessionWindow->resize(qMin(780, available.width() - frameExtra.width()), qMin(820, available.height() - frameExtra.height()));
        const auto frameSize = sessionWindow->frameGeometry().size();
        auto position = parentWidget()->frameGeometry().center() - QPoint(frameSize.width() / 2, frameSize.height() / 2) + QPoint(offset, offset);
        position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - frameSize.width() + 1)));
        position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - frameSize.height() + 1)));
        sessionWindow->move(position);
    }
    void detach() {
        if (detached) return;
        changing = true; QDockWidget::setFloating(false);
        containerLayout->removeWidget(sessionWindow);
        sessionWindow->setParent(parentWidget(), Qt::Window);
        detached = true; hide(); sessionWindow->show(); centerFloatingWindow(); changing = false;
        if (presentationChanged) presentationChanged();
    }
    void attach() {
        if (!detached) return;
        const bool visible = !sessionWindow->isHidden();
        sessionWindow->showNormal(); sessionWindow->hide();
        sessionWindow->setParent(container, Qt::Widget); containerLayout->addWidget(sessionWindow);
        detached = false; sessionWindow->show(); setVisible(visible); if (visible) raise();
        if (presentationChanged) presentationChanged();
    }
    void closeEvent(QCloseEvent *event) override {
        if (!pane->canClose()) { event->ignore(); return; }
        QDockWidget::closeEvent(event);
    }
};
class Workbench : public QMainWindow {
public:
    QVector<SessionDock *> docks; QTreeWidget *tree; QDockWidget *navigation; QLabel *layoutLabel;
    QLabel *dockPreview;
    int layoutIndex = 0, nextId = 1; bool shuttingDown = false;
    Workbench() {
        setWindowTitle("多协议调试工作台 · 界面原型（模拟数据）");
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        resize(qMin(1660, available.width() - 48), qMin(980, available.height() - 64));
        setDockNestingEnabled(true); setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
        auto *emptyArea = new QLabel("拖动会话窗口到此处停靠"); emptyArea->setAlignment(Qt::AlignCenter); setCentralWidget(emptyArea);
        dockPreview = new QLabel(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        dockPreview->setAttribute(Qt::WA_ShowWithoutActivating); dockPreview->setAttribute(Qt::WA_TranslucentBackground);
        dockPreview->setAlignment(Qt::AlignCenter);
        dockPreview->setStyleSheet("background:rgba(75,155,225,100);border:2px solid #2784c5;color:#123b60;font-size:18px;");
        auto *toolbar = addToolBar("工作区"); toolbar->setMovable(false); toolbar->setObjectName("workspaceBar");
        auto *brand = new QLabel("  多协议调试工作台  "); brand->setObjectName("brand"); toolbar->addWidget(brand);
        toolbar->addAction("＋ 新建会话", this, [this] { newSession(); });
        toolbar->addAction("保存工作区", this, [this] { saveWorkspace(); });
        toolbar->addAction("恢复工作区", this, [this] { restoreWorkspace(); }); toolbar->addSeparator();
        toolbar->addAction("左右平铺", this, [this] { arrange(0); }); toolbar->addAction("上下平铺", this, [this] { arrange(1); });
        toolbar->addAction("独立浮窗", this, [this] { arrange(2); });
        auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); toolbar->addWidget(spacer);
        auto *badge = new QLabel("  原型 / 模拟数据 · 不连接硬件  "); badge->setObjectName("prototypeBadge"); toolbar->addWidget(badge);
        navigation = new QDockWidget("会话与设备", this); navigation->setObjectName("navigation"); navigation->setFeatures(QDockWidget::NoDockWidgetFeatures);
        tree = new QTreeWidget; tree->setHeaderHidden(true); tree->setMinimumWidth(170); tree->setMaximumWidth(250); navigation->setWidget(tree);
        addDockWidget(Qt::LeftDockWidgetArea, navigation);
        connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
            const int index = item->data(0, Qt::UserRole).toInt(); if (index < 0 || index >= docks.size()) return;
            const int device = item->data(0, Qt::UserRole + 1).toInt();
            auto *dock = docks[index]; dock->showSession();
            if (device >= 0) dock->pane->deviceBox->setCurrentIndex(device);
        });
        layoutLabel = new QLabel("布局 A · 左右对照"); statusBar()->addWidget(layoutLabel);
        statusBar()->addPermanentWidget(new QLabel("拖动独立会话到工作台，按预览松开可停靠平铺；关闭停止通信、配置保留。"));
        addSample("电源调试", "Modbus RTU", "COM3", false);
        addSample("从站模拟", "Modbus RTU", "COM4", true);
        QTimer::singleShot(100, this, [this] { arrange(2); });
    }
    SessionDock *addPane(SessionPane *pane, const QString &id = {}) {
        pane->plotOwner = this;
        auto *dock = new SessionDock(pane, this); dock->setObjectName(id.isEmpty() ? QString("session_%1").arg(nextId++) : id);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        if (!docks.isEmpty()) splitDockWidget(docks.last(), dock, Qt::Horizontal);
        docks.append(dock); pane->changed = [this] { refreshTree(); }; dock->presentationChanged = [this] { refreshTree(); };
        dock->sessionWindow->dockDrag = [this, dock](QPoint position, DockDragPhase phase) { dragSession(dock, position, phase); };
        connect(dock, &QDockWidget::visibilityChanged, this, [this] { refreshTree(); });
        refreshTree(); return dock;
    }
    void dragSession(SessionDock *source, QPoint globalPosition, DockDragPhase phase) {
        if (phase == DockDragPhase::Cancel || !source->detached || source->sessionWindow->isHidden() || source->sessionWindow->isMinimized()
            || !isVisible() || isMinimized() || QApplication::activeModalWidget()) {
            dockPreview->hide(); return;
        }
        const auto position = mapFromGlobal(globalPosition);
        const QRect area = rect().adjusted(navigation->geometry().right() + 8, findChild<QToolBar *>("workspaceBar")->height() + 4, -8, -statusBar()->height() - 4);
        if (!area.contains(position)) { dockPreview->hide(); return; }
#ifdef Q_OS_WIN
        const auto host = reinterpret_cast<HWND>(winId());
        RECT client; POINT origin{0, 0};
        if (!GetClientRect(host, &client) || !ClientToScreen(host, &origin)) { dockPreview->hide(); return; }
        const POINT point{origin.x + MulDiv(position.x(), client.right, width()), origin.y + MulDiv(position.y(), client.bottom, height())};
        HWND hit = nullptr;
        for (auto candidate = GetTopWindow(nullptr); candidate; candidate = GetWindow(candidate, GW_HWNDNEXT)) {
            if (candidate == reinterpret_cast<HWND>(source->sessionWindow->winId()) || candidate == reinterpret_cast<HWND>(dockPreview->winId())
                || !IsWindowVisible(candidate) || IsIconic(candidate) || (GetWindowLongPtr(candidate, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) continue;
            DWORD cloaked = 0; DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            RECT bounds;
            if (!cloaked && GetWindowRect(candidate, &bounds) && PtInRect(&bounds, point)) { hit = candidate; break; }
        }
        if (hit != host) { dockPreview->hide(); return; }
#endif
        SessionDock *anchor = nullptr;
        for (auto *candidate : docks)
            if (candidate != source && !candidate->detached && !candidate->isHidden() && candidate->geometry().contains(position)) { anchor = candidate; break; }
        QRect preview = anchor ? anchor->geometry().intersected(area) : area;
        int edge = 0;
        if (anchor) {
            const double distances[] = {double(position.x() - preview.left()) / preview.width(), double(preview.right() - position.x()) / preview.width(),
                double(position.y() - preview.top()) / preview.height(), double(preview.bottom() - position.y()) / preview.height()};
            for (int i = 1; i < 4; ++i) if (distances[i] < distances[edge]) edge = i;
            if (edge < 2) { const int width = preview.width() / 2; if (edge == 1) preview.setLeft(preview.right() - width + 1); else preview.setWidth(width); }
            else { const int height = preview.height() / 2; if (edge == 3) preview.setTop(preview.bottom() - height + 1); else preview.setHeight(height); }
        }
        if (phase == DockDragPhase::Preview) {
            const QStringList labels = {"松开后靠左平铺", "松开后靠右平铺", "松开后靠上平铺", "松开后靠下平铺"};
            dockPreview->setText(anchor ? labels[edge] : "松开后停靠到工作台");
            dockPreview->setGeometry(QRect(mapToGlobal(preview.topLeft()), preview.size())); dockPreview->show(); dockPreview->raise(); return;
        }
        dockPreview->hide(); source->attach();
        if (anchor) {
            const auto orientation = edge < 2 ? Qt::Horizontal : Qt::Vertical;
            splitDockWidget(anchor, source, orientation);
            if (edge == 0 || edge == 2) splitDockWidget(source, anchor, orientation);
            resizeDocks({source, anchor}, {400, 400}, orientation);
        } else addDockWidget(Qt::RightDockWidgetArea, source);
        layoutLabel->setText("自定义布局 · 拖动停靠");
    }
    void addSample(QString name, QString protocol, QString endpoint, bool slave) {
        auto *pane = new SessionPane(name, protocol, endpoint, slave);
        const auto tag = QString("session%1-%2").arg(nextId).arg(QDateTime::currentMSecsSinceEpoch());
        pane->addDevice(sampleDevice(1, slave, tag)); if (!slave) pane->addDevice(sampleDevice(2, false, tag));
        addPane(pane);
    }
    void refreshTree() {
        bool hasDockedSession = false;
        for (auto *dock : docks) if (!dock->detached && !dock->isHidden()) { hasDockedSession = true; break; }
        centralWidget()->setVisible(!hasDockedSession);
        tree->clear();
        for (int i = 0; i < docks.size(); ++i) {
            auto *pane = docks[i]->pane;
            auto *item = new QTreeWidgetItem(tree, {QString("%1 %2").arg(pane->running ? "●" : "○", pane->sessionName)});
            item->setToolTip(0, pane->protocol + " / " + pane->endpoint); item->setData(0, Qt::UserRole, i); item->setData(0, Qt::UserRole + 1, -1);
            for (int j = 0; j < pane->devices.size(); ++j) {
                auto *child = new QTreeWidgetItem(item, {pane->devices[j].name}); child->setData(0, Qt::UserRole, i); child->setData(0, Qt::UserRole + 1, j);
            }
            item->setExpanded(true);
        }
    }
    void arrange(int variant) {
        dockPreview->hide();
        layoutIndex = variant; const QStringList labels = {"布局 A · 左右对照", "布局 B · 上下对照", "布局 C · 独立浮窗"}; layoutLabel->setText(labels[variant]);
        for (auto *dock : docks) { dock->attach(); dock->setFloating(false); addDockWidget(Qt::RightDockWidgetArea, dock); }
        if (variant < 2) {
            for (int i = 1; i < docks.size(); ++i) splitDockWidget(docks[i - 1], docks[i], variant == 0 ? Qt::Horizontal : Qt::Vertical);
            QList<QDockWidget *> list; QList<int> sizes; for (auto *dock : docks) if (!dock->sessionHidden()) { list << dock; sizes << 600; }
            if (!list.isEmpty()) resizeDocks(list, sizes, variant == 0 ? Qt::Horizontal : Qt::Vertical);
        } else {
            int count = 0; for (auto *dock : docks) if (!dock->sessionHidden()) ++count;
            int index = 0; for (auto *dock : docks) if (!dock->sessionHidden()) { dock->detach(); dock->centerFloatingWindow((2 * index++ - count + 1) * 12); }
        }
        resizeDocks({navigation}, {190}, Qt::Horizontal);
    }
    void newSession() {
        QDialog dialog(this); dialog.setWindowTitle("新建调试会话"); auto *form = new QFormLayout(&dialog);
        auto *name = new QLineEdit(QString("调试会话 %1").arg(nextId)); form->addRow("名称", name);
        auto *protocol = new QComboBox; protocol->addItems({"Modbus RTU", "演示协议（非 UPP）"}); form->addRow("协议插件", protocol);
        auto *role = new QComboBox; role->addItems({"主站", "从站"}); form->addRow("角色", role);
        auto *endpoint = new QLineEdit(QString("COM%1").arg(nextId + 2)); form->addRow("演示端点", endpoint);
        form->addRow(new QLabel("仅创建模拟会话；新设备从文件编辑页配置。"));
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        auto *pane = new SessionPane(name->text(), protocol->currentText(), endpoint->text(), role->currentIndex() == 1);
        auto *dock = addPane(pane); dock->detach();
        if (protocol->currentIndex()) { pane->table->horizontalHeaderItem(0)->setText("参数 / 命令"); pane->table->showColumn(2); pane->notice->setText("示例插件复用表格并显示说明列；没有实现任何 UPP 报文。"); }
    }
    void saveWorkspace() {
        QJsonArray sessions;
        for (auto *dock : docks) {
            auto *p = dock->pane; QJsonArray devices;
            for (const auto &device : p->devices) { auto json = deviceJson(device); json["path"] = device.path; devices.append(json); }
            sessions.append(QJsonObject{{"id", dock->objectName()}, {"name", p->sessionName}, {"protocol", p->protocol}, {"endpoint", p->endpoint}, {"slave", p->slave}, {"devices", devices}, {"visible", !dock->sessionHidden()}, {"detached", dock->detached}, {"windowGeometry", QString::fromLatin1(dock->sessionWindow->saveGeometry().toBase64())}});
        }
        const bool ok = writeJson(runtimeDir() + "/workspace.json", {{"sessions", sessions}, {"layout", QString::fromLatin1(saveState().toBase64())}, {"geometry", QString::fromLatin1(saveGeometry().toBase64())}});
        statusBar()->showMessage(ok ? "原型工作区已保存到 runtime/workspace.json；未保存的参数文件不在此操作中保存。" : "工作区保存失败", 8000);
    }
    void restoreWorkspace() {
        QFile file(runtimeDir() + "/workspace.json"); if (!file.open(QIODevice::ReadOnly)) { statusBar()->showMessage("请先保存原型工作区。", 5000); return; }
        const auto object = QJsonDocument::fromJson(file.readAll()).object(); if (!object.value("sessions").isArray()) return;
        for (auto *dock : docks) if (!dock->pane->canClose()) return;
        const auto old = docks; docks.clear(); for (auto *dock : old) { removeDockWidget(dock); dock->hideSession(); dock->hide(); dock->deleteLater(); }
        for (auto entry : object.value("sessions").toArray()) {
            const auto s = entry.toObject(); auto *pane = new SessionPane(s["name"].toString(), s["protocol"].toString(), s["endpoint"].toString(), s["slave"].toBool());
            for (auto dv : s["devices"].toArray()) {
                const auto data = dv.toObject(); Device d; d.name = data["deviceName"].toString(); d.id = data["deviceId"].toInt(); d.path = data["path"].toString(); QString error;
                if (parseDevice(data, d, error)) { for (auto &p : d.parameters) if (pane->slave) p.current = p.initial; pane->addDevice(d); }
            }
            auto *dock = addPane(pane, s["id"].toString());
            if (s["detached"].toBool()) { dock->detach(); dock->sessionWindow->restoreGeometry(QByteArray::fromBase64(s["windowGeometry"].toString().toLatin1())); }
            if (s["visible"].toBool()) dock->showSession(); else dock->hideSession(); ++nextId;
        }
        restoreGeometry(QByteArray::fromBase64(object["geometry"].toString().toLatin1())); restoreState(QByteArray::fromBase64(object["layout"].toString().toLatin1()));
        refreshTree(); statusBar()->showMessage("工作区已恢复；所有会话保持停止，请手动启动。", 8000);
    }
    void closeEvent(QCloseEvent *event) override {
        dockPreview->hide();
        if (!shuttingDown) for (auto *dock : docks) if (!dock->pane->canClose()) { event->ignore(); return; }
        shuttingDown = true; QMainWindow::closeEvent(event);
    }
};

static const char *style = R"(
QWidget { font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 13px; color: #22344a; }
QMainWindow { background: #e8eef4; }
QToolBar#workspaceBar { background: #142b46; padding: 10px; spacing: 8px; border: 0; }
QToolBar#workspaceBar QToolButton { color: #eaf3ff; background: #223f5b; border: 1px solid #49617b; padding: 7px 10px; border-radius: 4px; }
QToolBar#workspaceBar QToolButton:hover { background: #294663; }
QLabel#brand { color: white; font-size: 17px; font-weight: 600; }
QLabel#prototypeBadge { color: #80dbcc; }
QDockWidget { font-weight: 600; }
QDockWidget::title { background: #d7e4ef; padding: 8px; text-align: left; }
QDockWidget > QWidget { background: #f8fafc; }
QTabWidget::pane { border: 1px solid #d7e1eb; background: white; }
QTabBar::tab { padding: 9px 20px; background: #e9eff5; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { background: white; color: #087d73; border-bottom: 2px solid #139d90; }
QPushButton, QToolButton { padding: 5px 8px; border: 1px solid #cbd7e3; border-radius: 4px; background: white; }
QPushButton:hover, QToolButton:hover { background: #e6f3f1; border-color: #65b4aa; }
QPushButton:disabled { color: #a3afbc; background: #f1f4f7; border-color: #e1e6ec; }
QPushButton#primary { color: white; background: #087f75; border-color: #087f75; }
QComboBox, QSpinBox, QLineEdit { padding: 5px; border: 1px solid #cbd7e3; border-radius: 3px; background: white; }
QTableWidget { border: 1px solid #e0e6ee; background: white; alternate-background-color: #f5f8fb; gridline-color: #e8edf2; selection-background-color: #d5e9f8; selection-color: #17364e; }
QHeaderView::section { background: #edf3f8; padding: 7px 5px; border: 0; border-bottom: 1px solid #d6e0ea; color: #4e657d; }
QTreeWidget { background: #f5f8fb; border: none; }
QTreeWidget::item { padding: 9px 3px; }
QTreeWidget::item:selected { color: #096d66; background: #deefec; }
QPlainTextEdit#records { font-family: Consolas, 'Microsoft YaHei UI'; font-size: 11px; background: white; color: #22344a; border: 1px solid #d7e1eb; border-radius: 3px; padding: 4px; }
QLabel#hint { color: #687d92; font-size: 11px; }
QStatusBar { background: #e2eaf2; padding: 3px; }
)";

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    // The offscreen Windows platform does not enumerate system fonts automatically.
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/consola.ttf");
    QApplication::setFont(QFont("Microsoft YaHei", 9));
    QApplication::setStyle("Fusion"); app.setStyleSheet(style);
    Workbench window; window.show();
    if (app.arguments().contains("--capture")) {
        const auto output = runtimeDir() + "/captures"; QDir().mkpath(output);
        QFile captureLog(output + "/qt-messages.log"); if (!captureLog.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3; captureLog.close();
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            QFile log(runtimeDir() + "/captures/qt-messages.log");
            if (log.open(QIODevice::WriteOnly | QIODevice::Append)) { log.write(message.toUtf8()); log.write("\n"); }
        });
        QTimer::singleShot(450, &window, [&window, output] {
            SessionPane emptySlave("新建空从站", "Modbus RTU", "COM9", true);
            emptySlave.resize(780, 820); emptySlave.show();
            for (auto *action : emptySlave.findChildren<QPushButton *>())
                if (action->text() == "保存运行值到文件") action->click();
            emptySlave.grab().save(output + "/05-empty-slave-save.png");
            window.arrange(0);
            auto *master = window.docks[0]->pane; auto *slave = window.docks[1]->pane;
            master->start(); slave->start(); master->read(master->visibleRows);
            auto &p = master->devices[0].parameters[2]; p.draft = "230"; p.pending = true; p.status = "待提交"; master->refreshValues();
            window.grab().save(output + "/01-workbench.png");
            master->pages->setCurrentIndex(1); window.grab().save(output + "/02-file-editor.png");
            master->pages->setCurrentIndex(0); master->openPlot({0, 1});
            for (int i = 0; i < 65; ++i) master->read(master->visibleRows);
            auto *dock = window.docks[0]; master->togglePolling(); dock->detach();
            auto *native = dock->sessionWindow;
            const auto controls = Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint;
            if ((native->windowFlags() & controls) != controls) qFatal("Session lacks native window controls");
            native->showMaximized();
            if (!native->isMaximized() || !master->running || !master->pollTimer.isActive()) qFatal("Maximize interrupted session");
            native->showNormal(); native->showMinimized();
            if (!native->isMinimized() || !master->running || !master->pollTimer.isActive() || !slave->running) qFatal("Minimize interrupted session");
            window.tree->itemClicked(window.tree->topLevelItem(0), 0);
            if (native->isMinimized() || native->isHidden() || !master->running) qFatal("Navigation restore failed");
            dock->attach();
            if (dock->detached || dock->isHidden() || !master->running) qFatal("Dock restore failed");
            const auto dockOptions = window.dockOptions(); window.setDockOptions(QMainWindow::AllowNestedDocks);
            for (auto plot : master->plots) if (plot) plot->hide();
            window.raise();
            for (int edge = 0; edge < 4; ++edge) {
                dock->detach(); window.layout()->activate();
                const auto target = window.docks[1]->geometry();
                const QPoint points[] = {QPoint(target.left() + 12, target.center().y()), QPoint(target.right() - 12, target.center().y()),
                    QPoint(target.center().x(), target.top() + 12), QPoint(target.center().x(), target.bottom() - 12)};
                const auto dropPosition = window.mapToGlobal(points[edge]);
                if (edge == 0) {
                    QMainWindow blocker(&window, Qt::Window); blocker.setGeometry(QRect(dropPosition - QPoint(40, 40), QSize(160, 160)));
                    blocker.show(); blocker.raise();
                    native->dockDrag(dropPosition, DockDragPhase::Preview);
                    if (window.dockPreview->isVisible()) qFatal("Covered workbench accepted hover");
                    native->dockDrag(dropPosition, DockDragPhase::Drop);
                    if (!dock->detached) qFatal("Covered workbench accepted drop");
                    blocker.hide(); native->raise();
                }
                native->dockDrag(dropPosition, DockDragPhase::Preview);
                if (!window.dockPreview->isVisible() || !dock->detached) qFatal("Hover preview failed");
                native->dockDrag(dropPosition, DockDragPhase::Cancel);
                if (window.dockPreview->isVisible() || !dock->detached) qFatal("Dock drag cancellation failed");
                native->dockDrag(dropPosition, DockDragPhase::Preview);
                native->dockDrag(window.mapToGlobal(QPoint(-10, -10)), DockDragPhase::Drop);
                if (window.dockPreview->isVisible() || !dock->detached) qFatal("Outside drop docked session");
                native->dockDrag(dropPosition, DockDragPhase::Drop); window.layout()->activate();
                if (dock->detached || dock->isHidden() || !master->running || !master->pollTimer.isActive()
                    || !slave->running || !master->devices[0].parameters[2].pending) qFatal("Drag dock changed session state");
                const auto sourceCenter = dock->geometry().center(), targetCenter = window.docks[1]->geometry().center();
                const bool positioned = edge == 0 ? sourceCenter.x() < targetCenter.x() : edge == 1 ? sourceCenter.x() > targetCenter.x()
                    : edge == 2 ? sourceCenter.y() < targetCenter.y() : sourceCenter.y() > targetCenter.y();
                if (!positioned) qFatal("Drag dock direction mismatch");
            }
            window.grab().save(output + "/11-drag-docked-workbench.png");
            dock->detach(); window.docks[1]->detach(); window.layout()->activate();
            window.docks[1]->sessionWindow->move(window.screen()->availableGeometry().topRight() - QPoint(100, 0));
            const auto emptyDropPosition = window.centralWidget()->mapToGlobal(window.centralWidget()->rect().center());
            window.raise(); native->raise(); native->dockDrag(emptyDropPosition, DockDragPhase::Preview);
            if (!window.dockPreview->isVisible()) qFatal("Empty workbench hover failed");
            native->dockDrag(emptyDropPosition, DockDragPhase::Drop);
            if (dock->detached || !master->running || !master->pollTimer.isActive()) qFatal("Empty workbench drop failed");
            window.docks[1]->attach();
            window.setDockOptions(dockOptions);
            for (auto plot : master->plots) if (plot) plot->show();
            master->togglePolling();
        });
        QTimer::singleShot(1500, &window, [&window, output] {
            auto *master = window.docks[0]->pane;
            if (!master->plots.isEmpty() && master->plots[0]) master->plots[0]->grab().save(output + "/03-qml-chart.png");
            window.arrange(2);
        });
        QTimer::singleShot(2000, &window, [&window, output, &app] {
            auto *dock = window.docks[0]; auto *native = dock->sessionWindow;
            QFile placement(output + "/centered-floating-placement.txt");
            if (!placement.open(QIODevice::WriteOnly)) qFatal("Cannot record floating placement");
            QTextStream placementText(&placement); placementText << "Workbench: " << window.frameGeometry().x() << ',' << window.frameGeometry().y()
                << ' ' << window.frameGeometry().width() << 'x' << window.frameGeometry().height() << '\n';
            QPoint centerSum;
            for (auto *session : window.docks) {
                const auto frame = session->sessionWindow->frameGeometry(); centerSum += frame.center();
                if (!window.screen()->availableGeometry().contains(frame)) qFatal("Centered floating window outside screen");
                placementText << session->pane->sessionName << ": " << frame.x() << ',' << frame.y() << ' ' << frame.width() << 'x' << frame.height() << '\n';
            }
            if ((centerSum / window.docks.size() - window.frameGeometry().center()).manhattanLength() > 2) qFatal("Floating group not centered");
            placementText.flush(); placement.close();
            const auto before = native->geometry(); native->showMaximized(); native->showNormal();
            if (!dock->detached || native->geometry() != before) qFatal("Native maximize restore failed");
            auto *slaveDock = window.docks[1]; const int deviceCount = slaveDock->pane->devices.size();
            slaveDock->sessionWindow->close();
            if (slaveDock->pane->running || !slaveDock->sessionHidden() || !dock->pane->running) qFatal("Native close affected wrong session");
            slaveDock->showSession();
            if (slaveDock->pane->running || slaveDock->sessionHidden() || slaveDock->pane->devices.size() != deviceCount) qFatal("Closed session restore failed");
            native->raise(); native->activateWindow();
        });
        QTimer::singleShot(2300, &window, [&window, output] {
            auto *native = window.docks[0]->sessionWindow;
            native->grab().save(output + "/09-qmainwindow-session-content.png");
            auto *plot = window.docks[0]->pane->plots[0].data();
            const auto controls = Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint;
            if ((plot->windowFlags() & controls) != controls) qFatal("Plot lacks native window controls");
            plot->showMaximized(); if (!plot->isMaximized()) qFatal("Plot maximize failed");
            plot->showNormal(); plot->showMinimized(); if (!plot->isMinimized()) qFatal("Plot minimize failed");
            if (!window.docks[0]->pane->running) qFatal("Plot window state interrupted session");
            plot->showNormal(); plot->raise(); plot->activateWindow();
        });
        QTimer::singleShot(2600, &window, [&window, output, &app] {
            auto *plot = window.docks[0]->pane->plots[0].data();
            plot->grab().save(output + "/10-qmainwindow-plot-content.png");
            QFile result(output + "/capture-result.txt"); if (!result.open(QIODevice::WriteOnly)) { app.exit(3); return; }
            auto *pane = window.docks[0]->pane;
            bool ready = !pane->plots.isEmpty() && pane->plots[0] && pane->plots[0]->quick->status() == QQuickWidget::Ready;
            plot->close(); if (!pane->running) qFatal("Plot close interrupted session");
            result.write(ready ? "QQuickWidget Ready; captures completed. No hardware used.\n" : "QML load failed.\n"); result.close();
            app.exit(ready ? 0 : 2);
        });
    }
    return app.exec();
}
