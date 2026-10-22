#include "debugger/sdk.h"
#include "rtu_session.h"
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

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
        form->addRow(QStringLiteral("从站地址"), unit_);
        form->addRow(QStringLiteral("PDU 起始地址（从 0 开始）"), start_);
        form->addRow(QStringLiteral("保持寄存器数量"), count_);
        layout->addLayout(form);
        read_ = new QPushButton(QStringLiteral("读取保持寄存器（03）"), this);
        read_->setObjectName("readHolding");
        layout->addWidget(read_);
        result_ = new QLabel(QStringLiteral("启动会话后读取；本地发送不等于设备应答。应答超时 1000 ms，本项不自动重试。"), this);
        result_->setObjectName("readResult");
        result_->setWordWrap(true);
        layout->addWidget(result_);
        details_ = new QPlainTextEdit(this);
        details_->setObjectName("operationTrace");
        details_->setReadOnly(true);
        details_->setMinimumHeight(150);
        layout->addWidget(details_);
        QObject::connect(read_, &QPushButton::clicked, this, [this] {
            if (access_.submit) operationId_ = access_.submit("read-holding",
                {{"unit", unit_->value()}, {"start", start_->value()}, {"count", count_->value()}});
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
        const bool running = access_.running && access_.running();
        const auto operation = access_.operation ? access_.operation(operationId_) : OperationSnapshot{};
        read_->setEnabled(running && (!operationId_ || operation.finished));
        if (!operationId_) return;
        QStringList values;
        const auto registers = operation.update.result.value("values").toArray();
        const int start = operation.request.parameters.value("start").toInt();
        for (int i = 0; i < registers.size(); ++i)
            values << QStringLiteral("PDU %1 = %2").arg(start + i).arg(registers[i].toInt());
        result_->setText(QStringLiteral("操作 #%1 · %2\n%3\n%4")
            .arg(operationId_).arg(operation.update.state, operation.update.detail, values.join("；")));
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
    QSpinBox *unit_;
    QSpinBox *start_;
    QSpinBox *count_;
    QPushButton *read_;
    QLabel *result_;
    QPlainTextEdit *details_;
};
class modbusPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"modbus.rtu", QStringLiteral("Modbus RTU"), buildKey(), {"master"},
                {"serial"}, {"lifecycle", "read-holding"}, {}};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override {
        return std::make_unique<RtuSession>(std::move(services));
    }
    QWidget *createView(SessionAccess access, QWidget *parent) override {
        return new RtuView(std::move(access), parent);
    }
};
#include "modbus.moc"
