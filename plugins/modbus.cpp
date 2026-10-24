#include "debugger/sdk.h"
#include "rtu_session.h"
#include "slave_view.h"
#include "slave_parameters.h"
#include "master_parameters.h"
#include "master_controls.h"
#include "debugger/confirmation.h"
#include <QCloseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QMessageBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDir>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QTabWidget>

using namespace debugger;
class RtuView final : public QWidget {
public:
    RtuView(SessionAccess access, QWidget *parent, bool commandsOnly = false) : QWidget(parent), access_(std::move(access)), commandsOnly_(commandsOnly) {
        setObjectName("protocolView");
        auto *outer = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(240);
        auto *content = new QWidget(scroll);
        scroll->setWidget(content);
        outer->addWidget(scroll);
        auto *layout = new QVBoxLayout(content);
        auto *settings = new QPushButton(QStringLiteral("会话读取重试与多设备轮询配置…"), this);
        settings->setObjectName("pollSettings");
        layout->addWidget(settings); settings->setVisible(!commandsOnly);
        QObject::connect(settings, &QPushButton::clicked, this, [this] { showSettings(); });
        auto *form = new QFormLayout;
        unit_ = spin("unit", 1, 247, 1); unit_->setAccessibleName(QStringLiteral("功能码操作设备地址"));
        start_ = spin("registerStart", 0, 65535, 0); start_->setAccessibleName(QStringLiteral("功能码操作PDU地址"));
        count_ = spin("registerCount", 1, 125, 1);
        area_ = new QComboBox(this);
        area_->setObjectName("readArea");
        area_->addItem(QStringLiteral("线圈（01，布尔）"), "read-coils");
        area_->addItem(QStringLiteral("离散输入（02，布尔）"), "read-discrete");
        area_->addItem(QStringLiteral("保持寄存器（03，16 位）"), "read-holding");
        area_->addItem(QStringLiteral("输入寄存器（04，16 位）"), "read-input");
        area_->setCurrentIndex(2);
        form->addRow(QStringLiteral("从站地址"), unit_);
        form->addRow(QStringLiteral("数据区"), area_);
        form->addRow(QStringLiteral("PDU 起始地址（从 0 开始）"), start_);
        form->addRow(QStringLiteral("读取数量"), count_);
        layout->addLayout(form);
        read_ = new QPushButton(QStringLiteral("读取保持寄存器（03）"), this);
        read_->setObjectName("readHolding");
        layout->addWidget(read_);
        auto *writeForm = new QFormLayout;
        writeAction_ = new QComboBox(this);
        writeAction_->setObjectName("writeAction"); writeAction_->setAccessibleName(QStringLiteral("设备写入功能"));
        writeAction_->addItem(QStringLiteral("单线圈（05）"), "write-single-coil");
        writeAction_->addItem(QStringLiteral("单保持寄存器（06）"), "write-single-register");
        writeAction_->addItem(QStringLiteral("多线圈（0F）"), "write-multiple-coils");
        writeAction_->addItem(QStringLiteral("多保持寄存器（10）"), "write-multiple-registers");
        writeValues_ = new QLineEdit("true", this);
        writeValues_->setObjectName("writeValues"); writeValues_->setAccessibleName(QStringLiteral("设备写入值"));
        writeValues_->setPlaceholderText(QStringLiteral("单值 true / 42；多个值 [true,false] / [42,43]"));
        writeForm->addRow(QStringLiteral("设备写入功能（沿用上方设备与 PDU 起址）"), writeAction_);
        writeForm->addRow(QStringLiteral("写入值（JSON）"), writeValues_);
        layout->addLayout(writeForm);
        writeButton_ = new QPushButton(QStringLiteral("执行设备写入（不会自动重试）"), this);
        writeButton_->setObjectName("writeDevice");
        layout->addWidget(writeButton_);
        result_ = new QLabel(QStringLiteral("读取默认额外重试 2 次、应答超时 1000 ms、间隔 100 ms；设备写入不自动重试。"), this);
        result_->setObjectName("readResult");
        result_->setWordWrap(true);
        layout->addWidget(result_);
        if (!commandsOnly_) {
            observations_ = new QTableWidget(0, 4, this);
            observations_->setObjectName("masterObservations");
            observations_->setHorizontalHeaderLabels({QStringLiteral("从站"), QStringLiteral("数据区"),
                QStringLiteral("PDU 范围"), QStringLiteral("最近成功观测值")});
            observations_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            observations_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            observations_->setMinimumHeight(130);
            layout->addWidget(observations_);
            details_ = new QPlainTextEdit(this);
            details_->setObjectName("operationTrace");
            details_->setReadOnly(true);
            details_->setMinimumHeight(150);
            layout->addWidget(details_);
        }
        QObject::connect(read_, &QPushButton::clicked, this, [this] {
            if (access_.submit) operationId_ = access_.submit(area_->currentData().toString(),
                {{"unit", unit_->value()}, {"start", start_->value()}, {"count", count_->value()}});
            if (!operationId_) result_->setText(QStringLiteral("宿主操作容量已满，本次读取未提交"));
            refresh();
        });
        QObject::connect(area_, &QComboBox::currentIndexChanged, this, [this] {
            count_->setMaximum(area_->currentIndex() <= 1 ? 2000 : 125);
            read_->setText(QStringLiteral("读取%1").arg(area_->currentText()));
        });
        QObject::connect(writeButton_, &QPushButton::clicked, this, [this] {
            const auto action = writeAction_->currentData().toString();
            const bool multiple = action.startsWith("write-multiple");
            // 用单元素数组解析标量，避免将无效 JSON 静默转换为 0/false。
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson(("[" + writeValues_->text() + "]").toUtf8(), &error);
            const auto wrapper = parsed.array();
            if (error.error != QJsonParseError::NoError || wrapper.size() != 1) {
                result_->setText(QStringLiteral("写入值不是有效 JSON，尚未提交设备写入"));
                return;
            }
            QJsonObject parameters{{"unit", unit_->value()}, {"start", start_->value()}};
            parameters[multiple ? "values" : "value"] = wrapper[0];
            submittedRevision_ = draftRevision_;
            if (access_.submit) submittedWrite_ = operationId_ = access_.submit(action, parameters);
            refresh();
        });
        if (commandsOnly_) {
            parent->installEventFilter(this);
            connect(writeValues_, &QLineEdit::textEdited, this, [this] { draft_ = true; ++draftRevision_; });
            // 草稿身份包含写入目标；旧目标的确认不能清除已转向另一设备/地址/功能的输入。
            auto targetChanged = [this] { if (draft_) ++draftRevision_; };
            connect(unit_, &QSpinBox::valueChanged, this, targetChanged);
            connect(start_, &QSpinBox::valueChanged, this, targetChanged);
            connect(writeAction_, &QComboBox::currentIndexChanged, this, targetChanged);
            if (access_.registerViewState) access_.registerViewState(this,
                [this] { return LocalState{false, draft_, resolving_, {}}; }, [this] { return resolveDraft(); });
        }
        auto *timer = new QTimer(this);
        timer->setInterval(50);
        QObject::connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        timer->start();
        refresh();
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        const auto lifetime = access_.lifetime;
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (auto *window = qobject_cast<QWidget *>(watched)) window->close();
            return true;
        }
        if (event->type() == QEvent::Close) {
            QPointer<RtuView> alive(this);
            if (!resolveDraft() || !alive) { event->ignore(); return true; }
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    bool resolveDraft() {
        const auto lifetime = access_.lifetime;
        if (resolving_) return false;
        if (!draft_) return true;
        const auto revision = draftRevision_;
        QPointer<RtuView> alive(this); resolving_ = true;
        const auto answer = showConfirmation(this, QMessageBox::Warning, QStringLiteral("未提交功能码写入"),
            QStringLiteral("写入输入尚未确认。放弃只丢弃此输入，不撤回已提交的通信。"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (!alive) return false;
        resolving_ = false;
        if (revision != draftRevision_ || answer != QMessageBox::Discard) return false;
        draft_ = false; ++draftRevision_; writeValues_->clear(); return true;
    }
    void showSettings() {
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("pollSettingsDialog");
        dialog->setWindowTitle(QStringLiteral("会话读取重试与轮询"));
        dialog->resize(800, 520);
        auto *layout = new QVBoxLayout(dialog);
        auto *form = new QFormLayout;
        const auto configuration = access_.config ? access_.config().protocol : QJsonObject{};
        const auto editor = [dialog](const char *name, int low, int high, int value) {
            auto *spin = new QSpinBox(dialog); spin->setObjectName(name); spin->setRange(low, high); spin->setValue(value); return spin;
        };
        auto *retries = editor("extraReadRetries", 0, 10, configuration.value("extraReadRetries").toInt(2));
        auto *timeout = editor("responseTimeoutMs", 1, 60000, configuration.value("responseTimeoutMs").toInt(1000));
        auto *gap = editor("retryIntervalMs", 0, 60000, configuration.value("retryIntervalMs").toInt(100));
        auto *cycle = editor("pollIntervalMs", 10, 3600000, 1000);
        form->addRow(QStringLiteral("额外读取重试（0 表示单次）"), retries);
        form->addRow(QStringLiteral("单次应答超时（ms）"), timeout);
        form->addRow(QStringLiteral("重试间隔（ms）"), gap);
        form->addRow(QStringLiteral("新增任务轮询周期（ms，与重试间隔独立）"), cycle);
        layout->addLayout(form);
        auto *table = new QTableWidget(0, 5, dialog);
        table->setObjectName("pollTasks");
        table->setHorizontalHeaderLabels({QStringLiteral("从站"), QStringLiteral("数据区"), QStringLiteral("PDU 起址"),
            QStringLiteral("数量"), QStringLiteral("周期 ms")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        const auto addTask = [table](const QJsonObject &task, const QString &label) {
            if (table->rowCount() >= 64) return;
            const int row = table->rowCount(); table->insertRow(row);
            const QStringList fields{QString::number(task.value("unit").toInt()), label,
                QString::number(task.value("start").toInt()), QString::number(task.value("count").toInt()),
                QString::number(task.value("intervalMs").toInt())};
            for (int column = 0; column < 5; ++column) table->setItem(row, column, new QTableWidgetItem(fields[column]));
            table->item(row, 0)->setData(Qt::UserRole, task);
        };
        for (const auto &task : configuration.value("polls").toArray()) {
            const auto value = task.toObject(); const int index = area_->findData(value.value("action").toString());
            addTask(value, index >= 0 ? area_->itemText(index) : value.value("action").toString());
        }
        layout->addWidget(table);
        auto *add = new QPushButton(QStringLiteral("添加主面板当前设备/数据区/范围到轮询"), dialog);
        add->setObjectName("addPollTask"); layout->addWidget(add);
        QObject::connect(add, &QPushButton::clicked, dialog, [this, cycle, addTask] {
            addTask({{"unit", unit_->value()}, {"action", area_->currentData().toString()}, {"start", start_->value()},
                {"count", count_->value()}, {"intervalMs", cycle->value()}}, area_->currentText());
        });
        auto *remove = new QPushButton(QStringLiteral("删除选中轮询任务"), dialog);
        remove->setObjectName("removePollTask"); layout->addWidget(remove);
        QObject::connect(remove, &QPushButton::clicked, dialog, [table] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
        auto *status = new QLabel(QStringLiteral("周期从上次操作结束计时；配置列表为空时停止轮询。运行中请等待总线排队操作结束后应用。"), dialog);
        status->setObjectName("pollSettingsStatus"); status->setWordWrap(true); layout->addWidget(status);
        auto *apply = new QPushButton(QStringLiteral("应用会话配置"), dialog);
        apply->setObjectName("applyPollSettings"); layout->addWidget(apply);
        QObject::connect(apply, &QPushButton::clicked, dialog, [this, table, retries, timeout, gap, status] {
            auto settings = access_.config ? access_.config().protocol : QJsonObject{};
            QJsonArray tasks;
            for (int row = 0; row < table->rowCount(); ++row) tasks.append(table->item(row, 0)->data(Qt::UserRole).toJsonObject());
            settings["extraReadRetries"] = retries->value(); settings["responseTimeoutMs"] = timeout->value();
            settings["retryIntervalMs"] = gap->value(); settings["polls"] = tasks;
            if (access_.running && access_.running()) {
                const auto id = access_.submit("configure-polling", settings);
                status->setText(access_.operation(id).update.detail);
            } else status->setText(access_.configure && access_.configure(settings)
                ? QStringLiteral("配置已保存到会话，启动后生效") : QStringLiteral("当前无法修改会话配置"));
        });
        dialog->show();
    }
    QSpinBox *spin(const QString &name, int minimum, int maximum, int value) {
        auto *editor = new QSpinBox(this);
        editor->setObjectName(name);
        editor->setRange(minimum, maximum);
        editor->setValue(value);
        return editor;
    }
    void observe(const QJsonObject &result) {
        const auto unit = QString::number(result.value("unit").toInt());
        const auto area = result.value("area").toString();
        const int start = result.value("start").toInt();
        const auto registers = result.value("values").toArray();
        QStringList values;
        for (int i = 0; i < registers.size(); ++i)
            values << QStringLiteral("PDU %1 = %2").arg(start + i).arg(registers[i].isBool()
                ? (registers[i].toBool() ? "true" : "false") : QString::number(registers[i].toInt()));
        int row = 0;
        for (; row < observations_->rowCount(); ++row)
            if (observations_->item(row, 0)->text() == unit && observations_->item(row, 1)->text() == area) break;
        if (row == observations_->rowCount()) observations_->insertRow(row);
        observations_->setItem(row, 0, new QTableWidgetItem(unit));
        observations_->setItem(row, 1, new QTableWidgetItem(area));
        observations_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1..%2").arg(start).arg(start + registers.size() - 1)));
        observations_->setItem(row, 3, new QTableWidgetItem(values.join(QStringLiteral("；"))));
    }
    void refresh() {
        const bool running = access_.running && access_.running() && (!access_.config || access_.config().role == "master");
        const auto canSelectResult = [this] {
            const auto current = access_.operation ? access_.operation(operationId_) : OperationSnapshot{};
            return !current.request.id || current.finished;
        };
        if (!commandsOnly_ && access_.messages) for (const auto &message : access_.messages(0)) {
            if (message.id <= observedMessageId_ || !message.fields.value("operationResult").toBool()) continue;
            observedMessageId_ = message.id;
            if (message.status == "success" && !message.fields.value("values").toArray().isEmpty()
                && !message.fields.value("freshnessUncertain").toBool()) observe(message.fields);
            if (message.operationId && canSelectResult()) operationId_ = message.operationId;
            if (message.fields.value("freshnessUncertain").toBool() && canSelectResult())
                operationId_ = message.fields.value("candidateOperationId").toString().toULongLong();
        }
        const auto operation = access_.operation ? access_.operation(operationId_) : OperationSnapshot{};
        if (draft_ && submittedWrite_ && access_.operation) {
            const auto written = access_.operation(submittedWrite_);
            if (written.update.state == "success" && submittedRevision_ == draftRevision_) draft_ = false;
        }
        read_->setEnabled(running && (!operation.request.id || operation.finished));
        writeButton_->setEnabled(running && (!operation.request.id || operation.finished));
        if (!operationId_) return;
        if (!operation.request.id) {
            result_->setText(QStringLiteral("操作 #%1 已超过保留上限，结果已过期；可继续提交操作。").arg(operationId_));
            if (details_) details_->setPlainText(QStringLiteral("操作快照已过期；不会将缺失快照解释为仍在进行。"));
            return;
        }
        QStringList values;
        const auto registers = operation.update.result.value("values").toArray();
        const int start = operation.request.parameters.value("start").toInt();
        for (int i = 0; i < registers.size(); ++i)
            values << QStringLiteral("PDU %1 = %2").arg(start + i).arg(registers[i].isBool()
                ? (registers[i].toBool() ? "true" : "false") : QString::number(registers[i].toInt()));
        QString detail = operation.update.detail;
        if (operation.request.action.startsWith("write-") && operation.sendAccepted
            && operation.update.state != "success" && operation.update.state != "exception")
            detail += QStringLiteral("\n设备写入尚未确认，结果不确定；不会自动重试，停止也不能撤回已提交的字节。");
        result_->setText(QStringLiteral("操作 #%1 · %2\n%3\n%4")
            .arg(operationId_).arg(operation.update.state, detail, values.join("；")));
        if (commandsOnly_) return;
        QStringList lines{QStringLiteral("本次操作观察窗口（未匹配的 RX 不表示已确认归属）")};
        if (operation.omittedObservedRefCount)
            lines << QStringLiteral("另有 %1 条较早观察引用已超过保留上限，对应原始记录已过期").arg(operation.omittedObservedRefCount);
        const auto records = access_.traces(operationId_);
        QList<quint64> available;
        for (const auto &record : records) {
            available.append(record.id);
            lines << QStringLiteral("#%1 epoch=%2 %3 us %4 %5 %6")
                .arg(record.id).arg(record.generation).arg(record.monotonicUs)
                .arg(record.kind, QString::fromLatin1(record.bytes.toHex(' ')), record.detail);
        }
        for (auto ref : operation.observedRefs) if (!available.contains(ref))
            lines << QStringLiteral("#%1 来源记录已过期，原始字节不可用").arg(ref);
        for (const auto &message : access_.messages(operationId_)) {
            QStringList refs;
            for (auto ref : message.sources) refs << QString::number(ref);
            lines << QStringLiteral("消息 #%1 op=%2 [%3] sources=[%4] %5 %6")
                .arg(message.id).arg(message.operationId).arg(message.status, refs.join(','), message.detail,
                    QString::fromUtf8(QJsonDocument(message.fields).toJson(QJsonDocument::Compact)));
        }
        const auto text = lines.join('\n');
        if (text != details_->toPlainText()) details_->setPlainText(text);
    }
    SessionAccess access_;
    bool commandsOnly_ = false, draft_ = false, resolving_ = false;
    quint64 draftRevision_ = 0, submittedRevision_ = 0, submittedWrite_ = 0;
    quint64 operationId_ = 0;
    quint64 observedMessageId_ = 0;
    QComboBox *area_;
    QTableWidget *observations_ = nullptr;
    QSpinBox *unit_;
    QSpinBox *start_;
    QSpinBox *count_;
    QPushButton *read_;
    QComboBox *writeAction_;
    QLineEdit *writeValues_;
    QPushButton *writeButton_;
    QLabel *result_;
    QPlainTextEdit *details_ = nullptr;
};
QWidget *debugger::createAdvancedMasterView(SessionAccess access, QWidget *parent) {
    return new RtuView(std::move(access), parent, true);
}
class modbusPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"modbus.rtu", QStringLiteral("Modbus RTU"), buildKey(), {"master", "slave"},
                {"serial"}, {"lifecycle", "read-coils", "read-discrete", "read-holding", "read-input", "write-single-coil", "write-single-register",
                               "write-multiple-coils", "write-multiple-registers", "configure-polling", "stop-polling"}, {}, {"byte-stream"}, true};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override {
        return std::make_unique<RtuSession>(std::move(services));
    }
    QWidget *createView(SessionAccess access, QWidget *parent) override {
        auto *tabs = new QTabWidget(parent);
        tabs->setObjectName("protocolTabs");
        tabs->addTab(new RtuView(access, tabs), QStringLiteral("主站观测与设备写入"));
        tabs->addTab(createSlaveView(std::move(access), tabs), QStringLiteral("从站文件与模拟数据"));
        return tabs;
    }
    ParameterView *createParameterView(SessionAccess access, QWidget *parent) override {
        if (access.config && access.config().role == "slave") return createSlaveParameterView(std::move(access), parent);
        return createMasterParameterView(std::move(access), parent);
    }
    QJsonObject relocateConfiguration(QJsonObject config, const QString &fromBase, const QString &toBase) const override {
        const auto relocate = [&fromBase, &toBase](const QString &path) {
            const auto absolute = QDir::cleanPath(QDir::isAbsolutePath(path) ? path : QDir(fromBase).absoluteFilePath(path));
            return toBase.isEmpty() ? absolute : QDir(toBase).relativeFilePath(absolute);
        };
        if (config.value("parameterFiles").isObject()) {
            auto sources = config.value("parameterFiles").toObject();
            for (auto it = sources.begin(); it != sources.end(); ++it)
                if (it.value().isString() && !it.value().toString().isEmpty()) it.value() = relocate(it.value().toString());
            config["parameterFiles"] = sources;
        }
        if (!config.value("devices").isArray()) return config;
        auto devices = config.value("devices").toArray();
        for (int i = 0; i < devices.size(); ++i) {
            if (!devices[i].isObject()) continue;
            auto device = devices[i].toObject();
            const auto path = device.value("path");
            if (!path.isString() || path.toString().isEmpty()) continue;
            device["path"] = relocate(path.toString());
            devices[i] = device;
        }
        config["devices"] = devices;
        return config;
    }
};
#include "modbus.moc"
