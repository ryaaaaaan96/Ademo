#include "master_parameters.h"
#include "debugger/parameter_table.h"
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace debugger {
namespace {
const QStringList readActions{"read-coils", "read-discrete", "read-holding", "read-input"};
const QStringList areas{"coils", "discrete", "holding", "input"};
struct Row {
    int unit = 1;
    int address = 0;
    int area = 2;
    QString value = QStringLiteral("未读取");
    QString updated;
    quint64 operation = 0;
    QString id() const { return QString("%1/%2/%3").arg(unit).arg(areas[area]).arg(address); }
};
class MasterParameters final : public ParameterView {
public:
    MasterParameters(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        device_ = new QComboBox(this); device_->setAccessibleName(QStringLiteral("当前设备"));
        form->addRow(QStringLiteral("当前设备"), device_);
        auto *entry = new QWidget(this);
        auto *line = new QHBoxLayout(entry); line->setContentsMargins(0, 0, 0, 0);
        unit_ = new QSpinBox(entry); unit_->setRange(1, 247); unit_->setAccessibleName(QStringLiteral("设备地址"));
        area_ = new QComboBox(entry); area_->setAccessibleName(QStringLiteral("参数数据区"));
        area_->addItems({QStringLiteral("线圈"), QStringLiteral("离散输入"), QStringLiteral("保持寄存器"), QStringLiteral("输入寄存器")});
        area_->setCurrentIndex(2);
        address_ = new QSpinBox(entry); address_->setRange(0, 65535); address_->setAccessibleName(QStringLiteral("参数 PDU 地址"));
        add_ = new QPushButton(QStringLiteral("添加参数"), entry);
        line->addWidget(new QLabel(QStringLiteral("设备地址"), entry)); line->addWidget(unit_);
        line->addWidget(area_); line->addWidget(new QLabel(QStringLiteral("PDU 地址"), entry)); line->addWidget(address_); line->addWidget(add_);
        form->addRow(QStringLiteral("地址调试"), entry); layout->addLayout(form);
        table_ = new ParameterTable(this);
        table_->setColumns({{"name", QStringLiteral("名称")}, {"type", QStringLiteral("类型")},
            {"description", QStringLiteral("说明")}, {"value", QStringLiteral("值")}, {"unit", QStringLiteral("单位")},
            {"actions", QStringLiteral("操作")}, {"updated", QStringLiteral("更新时间")}});
        layout->addWidget(table_, 1);
        status_ = new QLabel(QStringLiteral("添加设备参数后启动会话，点击行内读取。"), this);
        status_->setWordWrap(true); layout->addWidget(status_);
        const auto config = access_.config ? access_.config().protocol : QJsonObject{};
        for (const auto &value : config.value("parameterRows").toArray()) {
            const auto item = value.toObject();
            const int unit = item.value("unit").toInt(-1), address = item.value("address").toInt(-1);
            const int area = areas.indexOf(item.value("area").toString());
            if (unit >= 1 && unit <= 247 && address >= 0 && address <= 65535 && area >= 0)
                append({unit, address, area});
        }
        connect(device_, &QComboBox::currentIndexChanged, this, [this] { showDevice(); });
        connect(add_, &QPushButton::clicked, this, [this] {
            const Row candidate{unit_->value(), address_->value(), area_->currentIndex()};
            for (const auto &row : rows_) if (row.id() == candidate.id()) { selectDevice(QString::number(row.unit)); return; }
            auto config = access_.config ? access_.config().protocol : QJsonObject{};
            auto stored = config.value("parameterRows").toArray();
            stored.append(QJsonObject{{"unit", candidate.unit}, {"area", areas[candidate.area]}, {"address", candidate.address}});
            config["parameterRows"] = stored;
            if (!access_.configure || !access_.configure(config)) {
                status_->setText(QStringLiteral("当前无法修改参数配置，请先停止会话并处理未保存内容。")); return;
            }
            append(candidate); selectDevice(QString::number(candidate.unit)); showDevice();
        });
        table_->onAction = [this](const QString &id, const QString &action) {
            if (action != "read" || !access_.submit) return;
            for (auto &row : rows_) if (row.id() == id) {
                row.operation = access_.submit(readActions[row.area], {{"unit", row.unit}, {"start", row.address}, {"count", 1}});
                if (!row.operation) status_->setText(QStringLiteral("操作容量已满，本次读取未提交。"));
                refresh(); return;
            }
        };
        auto *timer = new QTimer(this); timer->setInterval(50);
        connect(timer, &QTimer::timeout, this, [this] { refresh(); }); timer->start();
        showDevice(); refresh();
    }
    QList<ParameterDevice> devices() const override {
        QList<ParameterDevice> result;
        for (int i = 0; i < device_->count(); ++i) result.append({device_->itemData(i).toString(), device_->itemText(i)});
        return result;
    }
    QString selectedDevice() const override { return device_->currentData().toString(); }
    bool selectDevice(const QString &id) override {
        const int index = device_->findData(id); if (index < 0) return false;
        device_->setCurrentIndex(index); return true;
    }
private:
    void append(const Row &row) {
        for (const auto &existing : rows_) if (existing.id() == row.id()) return;
        rows_.append(row);
        const auto id = QString::number(row.unit);
        if (device_->findData(id) < 0) device_->addItem(QStringLiteral("设备 %1").arg(row.unit), id);
    }
    ParameterRow presentation(const Row &row) const {
        const auto op = access_.operation ? access_.operation(row.operation) : OperationSnapshot{};
        const bool running = access_.running && access_.running() && access_.config && access_.config().role == "master";
        return {row.id(), {{"name", QStringLiteral("PDU %1").arg(row.address)}, {"type", row.area < 2 ? "bool" : "uint16"},
            {"description", area_->itemText(row.area)}, {"value", row.value}, {"updated", row.updated}},
            {{"read", QStringLiteral("读取"), running && (!op.request.id || op.finished)}}};
    }
    void showDevice() {
        QList<ParameterRow> visible;
        for (const auto &row : rows_) if (QString::number(row.unit) == selectedDevice()) visible.append(presentation(row));
        table_->setRows(visible);
    }
    void refresh() {
        add_->setEnabled(!access_.running || !access_.running());
        if (access_.messages) for (const auto &message : access_.messages(0)) {
            if (message.id <= lastMessage_) continue;
            lastMessage_ = message.id;
            if (message.status != "success" || !message.fields.value("operationResult").toBool()
                || message.fields.value("freshnessUncertain").toBool()) continue;
            const auto values = message.fields.value("values").toArray();
            const int start = message.fields.value("start").toInt(-1);
            for (auto &row : rows_) if (row.unit == message.fields.value("unit").toInt()
                && areas[row.area] == message.fields.value("area").toString()
                && row.address >= start && row.address - start < values.size()) {
                const auto value = values[row.address - start];
                row.value = value.isBool() ? (value.toBool() ? "true" : "false") : QString::number(value.toInt());
                row.updated = message.observedUtc.toLocalTime().toString("HH:mm:ss.zzz");
            }
        }
        for (const auto &row : rows_) {
            table_->updateRow(presentation(row));
            if (!row.operation || !access_.operation) continue;
            const auto operation = access_.operation(row.operation);
            status_->setText(operation.request.id ? QStringLiteral("设备 %1 · PDU %2 · %3 · %4")
                .arg(row.unit).arg(row.address).arg(operation.update.state, operation.update.detail)
                : QStringLiteral("操作已过期；最近观测值保留，可重新读取。"));
        }
    }
    SessionAccess access_;
    QList<Row> rows_;
    quint64 lastMessage_ = 0;
    QComboBox *device_, *area_;
    QSpinBox *unit_, *address_;
    QPushButton *add_;
    ParameterTable *table_;
    QLabel *status_;
};
}
ParameterView *createMasterParameterView(SessionAccess access, QWidget *parent) {
    return new MasterParameters(std::move(access), parent);
}
}
