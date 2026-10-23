#include "master_parameters.h"
#include "parameter_definition.h"
#include "parameter_file_editor.h"
#include "debugger/parameter_table.h"
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QMessageBox>
#include <QTabWidget>

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
    quint64 writeOperation = 0, editRevision = 0, submittedRevision = 0;
    std::optional<QString> pending;
    QString writeResult, inputError, submittedText;
    QString name, type, displayUnit, description, group;
    QString id() const { return QString("%1/%2/%3").arg(unit).arg(areas[area]).arg(address); }
};
class MasterParameters final : public ParameterView {
public:
    MasterParameters(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        auto *outer = new QVBoxLayout(this);
        auto *pages = new QTabWidget(this); pages->setAccessibleName(QStringLiteral("参数页面"));
        auto *operations = new QWidget(pages);
        auto *layout = new QVBoxLayout(operations);
        pages->addTab(operations, QStringLiteral("参数操作")); outer->addWidget(pages);
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
        count_ = new QSpinBox(entry); count_->setRange(1, maximumDeviceParameters); count_->setAccessibleName(QStringLiteral("参数数量"));
        add_ = new QPushButton(QStringLiteral("添加参数"), entry);
        line->addWidget(new QLabel(QStringLiteral("设备地址"), entry)); line->addWidget(unit_);
        line->addWidget(area_); line->addWidget(new QLabel(QStringLiteral("PDU 地址"), entry)); line->addWidget(address_);
        line->addWidget(new QLabel(QStringLiteral("数量"), entry)); line->addWidget(count_); line->addWidget(add_);
        form->addRow(QStringLiteral("地址调试"), entry);
        auto *fileEntry = new QWidget(this); auto *fileLine = new QHBoxLayout(fileEntry);
        fileLine->setContentsMargins(0, 0, 0, 0);
        path_ = new QLineEdit(fileEntry); path_->setAccessibleName(QStringLiteral("参数文件"));
        path_->setPlaceholderText(QStringLiteral("参数定义 JSON 路径；应用目标为上方设备地址"));
        auto *browse = new QPushButton(QStringLiteral("选择参数文件…"), fileEntry);
        load_ = new QPushButton(QStringLiteral("加载并应用到设备"), fileEntry);
        fileLine->addWidget(path_, 1); fileLine->addWidget(browse); fileLine->addWidget(load_);
        form->addRow(QStringLiteral("参数文件"), fileEntry);
        group_ = new QComboBox(this); group_->setAccessibleName(QStringLiteral("参数分组"));
        form->addRow(QStringLiteral("参数分组"), group_); layout->addLayout(form);
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择参数定义（选择不应用）"), path_->text(), "JSON (*.json)");
            if (!path.isEmpty()) path_->setText(path);
        });
        connect(load_, &QPushButton::clicked, this, [this] { loadFile(); });
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
            {
                Row row{unit, address, area};
                row.name = item.value("name").toString(); row.type = item.value("type").toString();
                row.displayUnit = item.value("displayUnit").toString(); row.description = item.value("description").toString();
                row.group = item.value("group").toString(); append(row);
            }
        }
        const auto sources = config.value("parameterFiles").toObject();
        for (auto it = sources.begin(); it != sources.end(); ++it) {
            bool valid = false; const int unit = it.key().toInt(&valid);
            if (valid && unit >= 1 && unit <= 247 && device_->findData(QString::number(unit)) < 0)
                device_->addItem(QStringLiteral("设备 %1").arg(unit), QString::number(unit));
        }
        selectedDevice_ = selectedDevice();
        connect(device_, &QComboBox::currentIndexChanged, this, [this] {
            const auto next = selectedDevice();
            QPointer<MasterParameters> alive(this);
            const bool resolved = next == selectedDevice_ || (resolvePending(selectedDevice_) && bindEditor(next));
            if (!alive) return;
            if (!resolved) {
                const QSignalBlocker blocker(device_); device_->setCurrentIndex(device_->findData(selectedDevice_)); return;
            }
            selectedDevice_ = next; rebuildGroups();
        });
        connect(group_, &QComboBox::currentIndexChanged, this, [this] { showDevice(); });
        connect(add_, &QPushButton::clicked, this, [this] {
            if (address_->value() + count_->value() > 65536) {
                status_->setText(QStringLiteral("范围超出 PDU 0..65535，未添加任何参数。")); return;
            }
            QList<Row> additions;
            int existingCount = 0;
            QSet<QString> existingIds;
            for (const auto &row : rows_) if (row.unit == unit_->value()) { ++existingCount; existingIds.insert(row.id()); }
            for (int offset = 0; offset < count_->value(); ++offset) {
                const Row row{unit_->value(), address_->value() + offset, area_->currentIndex()};
                if (!existingIds.contains(row.id())) additions.append(row);
            }
            if (existingCount + additions.size() > maximumDeviceParameters) {
                status_->setText(QStringLiteral("每个设备最多 4096 个参数，原内容保持不变。")); return;
            }
            auto config = access_.config ? access_.config().protocol : QJsonObject{};
            auto stored = config.value("parameterRows").toArray();
            for (const auto &row : additions) stored.append(storedRow(row));
            config["parameterRows"] = stored;
            QPointer<MasterParameters> alive(this);
            const bool configured = access_.configure && access_.configure(config);
            if (!alive) return;
            if (!configured) {
                status_->setText(QStringLiteral("当前无法修改参数配置，请先停止会话并处理未保存内容。")); return;
            }
            for (const auto &row : additions) append(row);
            selectDevice(QString::number(unit_->value())); rebuildGroups();
            const int defaultGroup = group_->findData(QString()); if (defaultGroup >= 0) group_->setCurrentIndex(defaultGroup);
            status_->setText(QStringLiteral("已添加 %1 个地址参数，未发送设备命令").arg(additions.size()));
        });
        table_->onEdit = [this](const QString &id, const QString &column, const QString &text) {
            if (column != "value") return;
            for (auto &row : rows_) if (row.id() == id && (row.area == 0 || row.area == 2)) {
                row.pending = text; row.inputError.clear(); ++row.editRevision; ++pendingRevision_; refresh(); return;
            }
        };
        table_->onAction = [this](const QString &id, const QString &action) {
            if (!access_.submit) return;
            for (auto &row : rows_) if (row.id() == id) {
                if (action == "set") { write(row); refresh(); return; }
                if (action != "read") return;
                row.operation = access_.submit(readActions[row.area], {{"unit", row.unit}, {"start", row.address}, {"count", 1}});
                if (!row.operation) status_->setText(QStringLiteral("操作容量已满，本次读取未提交。"));
                refresh(); return;
            }
        };
        auto *timer = new QTimer(this); timer->setInterval(50);
        connect(timer, &QTimer::timeout, this, [this] { refresh(); }); timer->start();
        if (access_.registerViewState) access_.registerViewState(this,
            [this] { return LocalState{false, hasPending(), loading_, {}}; }, [this] { return resolvePending(); });
        fileEditor_ = new FileEditorPage(access_, createParameterFileEditor(nullptr),
            [this](const QString &target, const QJsonObject &document, const QString &path) {
                const auto result = parseParameterDefinition(document);
                if (!result.error.isEmpty()) return result.error;
                return applyDefinition(target.toInt(), path, result.parameters) ? QString()
                    : QStringLiteral("未应用：请先停止会话并处理未保存内容；原参数保留。");
            }, pages);
        pages->addTab(fileEditor_, QStringLiteral("文件编辑")); bindEditor(selectedDevice());
        rebuildGroups(); refresh();
    }
    QList<ParameterDevice> devices() const override {
        QList<ParameterDevice> result;
        for (int i = 0; i < device_->count(); ++i) result.append({device_->itemData(i).toString(), device_->itemText(i)});
        return result;
    }
    QString selectedDevice() const override { return device_->currentData().toString(); }
    bool selectDevice(const QString &id) override {
        const int index = device_->findData(id); if (index < 0) return false;
        QPointer<MasterParameters> alive(this);
        device_->setCurrentIndex(index); return alive && selectedDevice() == id;
    }
private:
    bool bindEditor(const QString &id) {
        if (!fileEditor_) return true;
        QJsonArray parameters;
        for (const auto &row : rows_) if (QString::number(row.unit) == id)
            parameters.append(parameterDefinitionJson({areas[row.area], row.address, row.name,
                row.type.isEmpty() ? (row.area < 2 ? "bool" : "uint16") : row.type, row.displayUnit, row.description, row.group}));
        const auto config = access_.config ? access_.config().protocol : QJsonObject{};
        return fileEditor_->bindTarget(id, QStringLiteral("设备 %1").arg(id),
            {{"kind", "modbus-parameter-definition"}, {"version", 1}, {"parameters", parameters}},
            config.value("parameterFiles").toObject().value(id).toString());
    }
    bool hasPending(const QString &device = {}) const {
        for (const auto &row : rows_) if (row.pending && (device.isEmpty() || QString::number(row.unit) == device)) return true;
        return false;
    }
    bool resolvePending(const QString &device = {}) {
        if (!hasPending(device)) return true;
        const auto revision = pendingRevision_;
        QPointer<MasterParameters> alive(this);
        const auto decision = QMessageBox::warning(this, QStringLiteral("待提交设备值"),
            QStringLiteral("存在尚未确认的待提交值。放弃只清除界面草稿，不撤销已发送的写入，也不删除通信结果。"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (!alive || decision != QMessageBox::Discard || revision != pendingRevision_) return false;
        for (auto &row : rows_) if (device.isEmpty() || QString::number(row.unit) == device) row.pending.reset();
        refresh(); return true;
    }
    void write(Row &row) {
        if (!row.pending || (row.area != 0 && row.area != 2) || !access_.running || !access_.running()) return;
        const auto previous = access_.operation ? access_.operation(row.writeOperation) : OperationSnapshot{};
        if (previous.request.id && !previous.finished) return;
        const auto text = row.pending->trimmed();
        row.inputError.clear();
        QJsonValue value;
        if (row.area == 0) {
            if (text != "true" && text != "false" && text != "0" && text != "1") {
                row.inputError = QStringLiteral("无效输入：线圈只接受 true/false 或 0/1，未发送"); return;
            }
            value = text == "true" || text == "1";
        } else {
            bool valid = false; const int number = text.toInt(&valid);
            if (!valid || !QRegularExpression("^[0-9]+$").match(text).hasMatch() || number < 0 || number > 65535) {
                row.inputError = QStringLiteral("无效输入：寄存器只接受 0..65535 整数，未发送"); return;
            }
            value = number;
        }
        row.writeOperation = access_.submit(row.area == 0 ? "write-single-coil" : "write-single-register",
            {{"unit", row.unit}, {"start", row.address}, {"value", value}});
        row.submittedRevision = row.editRevision;
        row.submittedText = text;
        row.writeResult = row.writeOperation ? QStringLiteral("写入已提交，等待设备确认") : QStringLiteral("写入未提交：操作容量已满");
    }
    QJsonObject storedRow(const Row &row) const {
        return {{"unit", row.unit}, {"area", areas[row.area]}, {"address", row.address}, {"name", row.name},
            {"type", row.type}, {"displayUnit", row.displayUnit}, {"description", row.description}, {"group", row.group}};
    }
    void loadFile() {
        if (loading_) return;
        const auto path = QFileInfo(path_->text()).absoluteFilePath();
        const int unit = unit_->value();
        loading_ = true; refresh();
        status_->setText(QStringLiteral("正在校验设备 %1 的参数文件；当前参数保持可用").arg(unit));
        QPointer<MasterParameters> self(this);
        const bool accepted = access_.background && access_.background([path] {
            const auto result = readParameterDefinition(path);
            if (!result.error.isEmpty()) return QJsonObject{{"error", result.error}};
            QJsonArray parameters;
            for (const auto &parameter : result.parameters) parameters.append(parameterDefinitionJson(parameter));
            return QJsonObject{{"kind", "modbus-parameter-definition"}, {"version", 1}, {"parameters", parameters}};
        }, [self, path, unit](QJsonObject value) {
            if (!self) return;
            self->loading_ = false;
            const auto result = value.contains("error") ? ParameterDefinitionResult{{}, value.value("error").toString()}
                                                        : parseParameterDefinition(value);
            if (!result.error.isEmpty()) self->status_->setText(result.error);
            else self->applyDefinition(unit, path, result.parameters);
            self->refresh();
        });
        if (!accepted) { loading_ = false; refresh(); status_->setText(QStringLiteral("后台文件任务繁忙，请稍后重试；原参数保留。")); }
    }
    bool applyDefinition(int unit, const QString &path, const QList<ParameterDefinition> &parameters) {
        QList<Row> replacement;
        for (const auto &row : rows_) if (row.unit != unit) replacement.append(row);
        for (const auto &p : parameters) {
            Row row{unit, p.address, static_cast<int>(areas.indexOf(p.area))};
            row.name = p.name; row.type = p.type; row.displayUnit = p.unit; row.description = p.description; row.group = p.group;
            replacement.append(row);
        }
        auto config = access_.config ? access_.config().protocol : QJsonObject{};
        QJsonArray stored; for (const auto &row : replacement) stored.append(storedRow(row));
        config["parameterRows"] = stored;
        auto sources = config.value("parameterFiles").toObject(); sources[QString::number(unit)] = path; config["parameterFiles"] = sources;
        QPointer<MasterParameters> alive(this);
        const bool configured = access_.configure && access_.configure(config);
        if (!alive) return false;
        if (!configured) {
            status_->setText(QStringLiteral("未应用：请先停止会话并处理未保存内容；原参数保留。")); return false;
        }
        // configure 可能经过模态修改保护；其他设备须取确认后的实时状态，不能复活旧草稿或覆盖新观测。
        QList<Row> current;
        for (const auto &row : rows_) if (row.unit != unit) current.append(row);
        for (const auto &row : replacement) if (row.unit == unit) current.append(row);
        replacement = std::move(current);
        rows_.clear(); operationStatuses_.clear(); const QSignalBlocker blocker(device_); device_->clear();
        for (const auto &row : replacement) append(row);
        if (device_->findData(QString::number(unit)) < 0) device_->addItem(QStringLiteral("设备 %1").arg(unit), QString::number(unit));
        selectDevice(QString::number(unit)); selectedDevice_ = selectedDevice(); rebuildGroups();
        bindEditor(selectedDevice_);
        status_->setText(QStringLiteral("已应用 %1 个参数到设备 %2；未发送设备命令").arg(parameters.size()).arg(unit));
        return true;
    }
    void rebuildGroups() {
        const auto previous = group_->currentData().toString();
        const QSignalBlocker blocker(group_); group_->clear();
        for (const auto &row : rows_) if (QString::number(row.unit) == selectedDevice() && group_->findData(row.group) < 0)
            group_->addItem(row.group.isEmpty() ? QStringLiteral("默认组") : row.group, row.group);
        const int index = group_->findData(previous); if (index >= 0) group_->setCurrentIndex(index);
        showDevice();
    }
    void append(const Row &row) {
        for (const auto &existing : rows_) if (existing.id() == row.id()) return;
        rows_.append(row);
        const auto id = QString::number(row.unit);
        if (device_->findData(id) < 0) device_->addItem(QStringLiteral("设备 %1").arg(row.unit), id);
    }
    ParameterRow presentation(const Row &row) const {
        const auto op = access_.operation ? access_.operation(row.operation) : OperationSnapshot{};
        const bool running = access_.running && access_.running() && access_.config && access_.config().role == "master";
        ParameterRow result{row.id(), {{"name", row.name.isEmpty() ? QStringLiteral("PDU %1").arg(row.address) : row.name},
            {"type", row.type.isEmpty() ? (row.area < 2 ? "bool" : "uint16") : row.type},
            {"description", row.description.isEmpty() ? area_->itemText(row.area) : row.description}, {"value", row.pending.value_or(row.value)},
            {"unit", row.displayUnit}, {"updated", row.updated}},
            {{"read", QStringLiteral("读取"), running && (!op.request.id || op.finished)}}};
        if (row.area == 0 || row.area == 2) {
            result.editableCells.insert("value");
            const auto write = access_.operation ? access_.operation(row.writeOperation) : OperationSnapshot{};
            result.actions.append({"set", QStringLiteral("设置"), running && row.pending.has_value() && (!write.request.id || write.finished)});
        }
        if (row.pending) result.pendingCells.insert("value");
        result.tooltips["value"] = (row.pending ? QStringLiteral("待提交；") : QString())
            + QStringLiteral("最近读回：%1；%2；%3").arg(row.value, row.writeResult, row.inputError);
        return result;
    }
    void showDevice() {
        QList<ParameterRow> visible;
        for (const auto &row : rows_) if (QString::number(row.unit) == selectedDevice() && row.group == group_->currentData().toString()) visible.append(presentation(row));
        table_->setRows(visible);
    }
    void refresh() {
        const bool configurable = !loading_ && (!access_.running || !access_.running());
        add_->setEnabled(configurable); load_->setEnabled(configurable);
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
        for (auto &row : rows_) {
            if (row.writeOperation && access_.operation) {
                const auto write = access_.operation(row.writeOperation);
                row.writeResult = write.request.id ? QStringLiteral("提交值 %1 · %2 · %3").arg(row.submittedText, write.update.state, write.update.detail)
                                                  : QStringLiteral("写入记录已过期；请核对设备后决定是否再次设置");
                if (write.update.result.value("uncertain").toBool()) row.writeResult.prepend(QStringLiteral("结果不确定 · "));
                if (write.finished && write.update.state == "success" && row.submittedRevision == row.editRevision) row.pending.reset();
            }
            const auto writeStatus = row.inputError.isEmpty() ? row.writeResult : row.inputError;
            if (!writeStatus.isEmpty() && operationStatuses_.value("write:" + row.id()) != writeStatus) {
                operationStatuses_["write:" + row.id()] = writeStatus;
                if (QString::number(row.unit) == selectedDevice())
                    status_->setText(QStringLiteral("设备 %1 · PDU %2 · %3").arg(row.unit).arg(row.address).arg(writeStatus));
            }
            table_->updateRow(presentation(row));
            if (!row.operation || !access_.operation) continue;
            const auto operation = access_.operation(row.operation);
            const auto operationStatus = operation.request.id ? QStringLiteral("设备 %1 · PDU %2 · %3 · %4")
                .arg(row.unit).arg(row.address).arg(operation.update.state, operation.update.detail)
                : QStringLiteral("操作已过期；最近观测值保留，可重新读取。");
            if (operationStatuses_.value(row.id()) != operationStatus) {
                operationStatuses_[row.id()] = operationStatus;
                if (QString::number(row.unit) == selectedDevice()) status_->setText(operationStatus);
            }
        }
    }
    SessionAccess access_;
    QList<Row> rows_;
    QMap<QString, QString> operationStatuses_;
    quint64 lastMessage_ = 0;
    quint64 pendingRevision_ = 0;
    QString selectedDevice_;
    bool loading_ = false;
    QComboBox *device_, *area_, *group_;
    QLineEdit *path_;
    QSpinBox *unit_, *address_, *count_;
    QPushButton *add_, *load_;
    ParameterTable *table_;
    QLabel *status_;
    FileEditorPage *fileEditor_ = nullptr;
};
}
ParameterView *createMasterParameterView(SessionAccess access, QWidget *parent) {
    return new MasterParameters(std::move(access), parent);
}
}
