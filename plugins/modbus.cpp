#include "debugger/sdk.h"
#include "rtu_session.h"
#include "slave_view.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QTabWidget>

using namespace debugger;
class RtuView final : public QWidget {
public:
    RtuView(SessionAccess access, QWidget *parent) : QWidget(parent), access_(std::move(access)) {
        setObjectName("protocolView");
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        unit_ = spin("unit", 1, 247, 1);
        start_ = spin("registerStart", 0, 65535, 0);
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
        writeAction_->setObjectName("writeAction");
        writeAction_->addItem(QStringLiteral("单线圈（05）"), "write-single-coil");
        writeAction_->addItem(QStringLiteral("单保持寄存器（06）"), "write-single-register");
        writeAction_->addItem(QStringLiteral("多线圈（0F）"), "write-multiple-coils");
        writeAction_->addItem(QStringLiteral("多保持寄存器（10）"), "write-multiple-registers");
        writeValues_ = new QLineEdit("true", this);
        writeValues_->setObjectName("writeValues");
        writeValues_->setPlaceholderText(QStringLiteral("单值 true / 42；多个值 [true,false] / [42,43]"));
        writeForm->addRow(QStringLiteral("设备写入功能（沿用上方设备与 PDU 起址）"), writeAction_);
        writeForm->addRow(QStringLiteral("写入值（JSON）"), writeValues_);
        layout->addLayout(writeForm);
        writeButton_ = new QPushButton(QStringLiteral("执行设备写入（不会自动重试）"), this);
        writeButton_->setObjectName("writeDevice");
        layout->addWidget(writeButton_);
        result_ = new QLabel(QStringLiteral("启动会话后读取；本地发送不等于设备应答。应答超时 1000 ms，本项不自动重试。"), this);
        result_->setObjectName("readResult");
        result_->setWordWrap(true);
        layout->addWidget(result_);
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
        QObject::connect(read_, &QPushButton::clicked, this, [this] {
            if (access_.submit) operationId_ = access_.submit(area_->currentData().toString(),
                {{"unit", unit_->value()}, {"start", start_->value()}, {"count", count_->value()}});
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
            if (access_.submit) operationId_ = access_.submit(action, parameters);
            refresh();
        });
        auto *timer = new QTimer(this);
        timer->setInterval(50);
        QObject::connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        timer->start();
        refresh();
    }
private:
    QSpinBox *spin(const QString &name, int minimum, int maximum, int value) {
        auto *editor = new QSpinBox(this);
        editor->setObjectName(name);
        editor->setRange(minimum, maximum);
        editor->setValue(value);
        return editor;
    }
    void refresh() {
        const bool running = access_.running && access_.running()
            && (!access_.config || access_.config().role == "master");
        const auto operation = access_.operation ? access_.operation(operationId_) : OperationSnapshot{};
        read_->setEnabled(running && (!operationId_ || operation.finished));
        writeButton_->setEnabled(running && (!operationId_ || operation.finished));
        if (!operationId_) return;
        QStringList values;
        const auto registers = operation.update.result.value("values").toArray();
        const int start = operation.request.parameters.value("start").toInt();
        for (int i = 0; i < registers.size(); ++i)
            values << QStringLiteral("PDU %1 = %2").arg(start + i).arg(registers[i].isBool()
                ? (registers[i].toBool() ? "true" : "false") : QString::number(registers[i].toInt()));
        if (operation.update.state == "success" && !registers.isEmpty() && observedOperationId_ != operationId_) {
            observedOperationId_ = operationId_;
            const auto unit = QString::number(operation.update.result.value("unit").toInt());
            const auto area = operation.update.result.value("area").toString();
            int row = 0;
            for (; row < observations_->rowCount(); ++row)
                if (observations_->item(row, 0)->text() == unit && observations_->item(row, 1)->text() == area) break;
            if (row == observations_->rowCount()) observations_->insertRow(row);
            observations_->setItem(row, 0, new QTableWidgetItem(unit));
            observations_->setItem(row, 1, new QTableWidgetItem(area));
            observations_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1..%2").arg(start).arg(start + registers.size() - 1)));
            observations_->setItem(row, 3, new QTableWidgetItem(values.join(QStringLiteral("；"))));
        }
        QString detail = operation.update.detail;
        if (operation.request.action.startsWith("write-") && operation.sendAccepted
            && operation.update.state != "success" && operation.update.state != "exception")
            detail += QStringLiteral("\n设备写入尚未确认，结果不确定；不会自动重试，停止也不能撤回已提交的字节。");
        result_->setText(QStringLiteral("操作 #%1 · %2\n%3\n%4")
            .arg(operationId_).arg(operation.update.state, detail, values.join("；")));
        QStringList lines{QStringLiteral("本次操作观察窗口（未匹配的 RX 不表示已确认归属）")};
        if (operation.omittedObservedRefCount)
            lines << QStringLiteral("另有 %1 条较早观察引用已超过保留上限，对应原始记录已过期").arg(operation.omittedObservedRefCount);
        const auto records = access_.traces(operationId_);
        QList<quint64> available;
        for (const auto &record : records) {
            available.append(record.id);
            lines << QStringLiteral("#%1 run=%2 %3 us %4 %5 %6")
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
    quint64 operationId_ = 0;
    quint64 observedOperationId_ = 0;
    QComboBox *area_;
    QTableWidget *observations_;
    QSpinBox *unit_;
    QSpinBox *start_;
    QSpinBox *count_;
    QPushButton *read_;
    QComboBox *writeAction_;
    QLineEdit *writeValues_;
    QPushButton *writeButton_;
    QLabel *result_;
    QPlainTextEdit *details_;
};
class modbusPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"modbus.rtu", QStringLiteral("Modbus RTU"), buildKey(), {"master", "slave"},
                {"serial"}, {"lifecycle", "read-coils", "read-discrete", "read-holding", "read-input", "write-single-coil", "write-single-register",
                               "write-multiple-coils", "write-multiple-registers"}, {}};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override {
        return std::make_unique<RtuSession>(std::move(services));
    }
    QWidget *createView(SessionAccess access, QWidget *parent) override {
        auto *tabs = new QTabWidget(parent);
        tabs->addTab(new RtuView(access, tabs), QStringLiteral("主站观测与设备写入"));
        tabs->addTab(createSlaveView(std::move(access), tabs), QStringLiteral("从站文件与模拟数据"));
        return tabs;
    }
};
#include "modbus.moc"
