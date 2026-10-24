#include "upp_session.h"
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
using namespace debugger;
namespace {
void addJson(QTreeWidgetItem *parent, const QString &name, const QJsonValue &value) {
    auto *item = new QTreeWidgetItem(parent, {name});
    if (value.isObject()) { const auto object = value.toObject(); for (auto it = object.begin(); it != object.end(); ++it) addJson(item, it.key(), it.value()); }
    else if (value.isArray()) { const auto array = value.toArray(); for (int i = 0; i < array.size(); ++i) addJson(item, QString::number(i), array[i]); }
    else item->setText(1, value.isString() ? value.toString() : value.isBool() ? (value.toBool() ? "true" : "false") : value.isDouble() ? QString::number(value.toDouble(), 'g', 16) : "null");
}
class UppView final : public ParameterView {
    SessionAccess access_;
    QWidget *controls_;
    QTableWidget *messages_, *sources_;
    QTreeWidget *fields_;
    QLineEdit *device_;
    QLabel *summary_;
    QList<Message> displayed_;
    quint64 selected_ = 0;
    QString expiredRecords_;
    void details() {
        const int row = messages_->currentRow(); fields_->clear(); sources_->setRowCount(0);
        if (row < 0 || row >= displayed_.size()) { selected_ = 0; return; }
        const auto &message = displayed_[row]; selected_ = message.id;
        auto *root = new QTreeWidgetItem(fields_, {QStringLiteral("解析字段"), message.detail});
        for (auto it = message.fields.begin(); it != message.fields.end(); ++it) addJson(root, it.key(), it.value());
        fields_->expandToDepth(1);
        QMap<quint64, TraceRecord> records;
        if (access_.traces) for (const auto &r : access_.traces(0)) records.insert(r.id, r);
        for (const auto &range : message.sourceRanges) {
            const int index = sources_->rowCount(); sources_->insertRow(index);
            QStringList cells{QString::number(range.recordId), QString::number(range.offset), QString::number(range.length)};
            const auto it = records.constFind(range.recordId);
            if (it == records.cend()) cells << QStringLiteral("来源已过期/不可用") << "" << "";
            else cells << QString::fromLatin1(it->bytes.toHex(' ')) << QString::number(it->metadata.value("canId").toInt(), 16).toUpper() << QString::fromUtf8(QJsonDocument(it->metadata).toJson(QJsonDocument::Compact));
            for (int c = 0; c < cells.size(); ++c) sources_->setItem(index, c, new QTableWidgetItem(cells[c]));
        }
    }
    void refresh() {
        const auto messages = access_.messages ? access_.messages(0) : QList<Message>{};
        QList<Message> filtered;
        for (const auto &m : messages) if (device_->text().isEmpty() || QString::number(m.fields.value("deviceId").toInt(-1)) == device_->text()) filtered.append(m);
        const auto status = access_.recordStatus ? access_.recordStatus() : QJsonObject{};
        summary_->setText(QStringLiteral("当前 %1 条消息 · 原始记录过期 %2 · 暂定规则，未做设备互通验证").arg(filtered.size()).arg(status.value("expiredRecords").toString("0")));
        const bool same = filtered.size() == displayed_.size() && (filtered.isEmpty() || (filtered.first().id == displayed_.first().id && filtered.last().id == displayed_.last().id));
        const auto expired = status.value("expiredRecords").toString();
        if (same) { if (expired != expiredRecords_) { expiredRecords_ = expired; details(); } return; }
        expiredRecords_ = expired;
        const auto selected = selected_;
        displayed_ = filtered;
        QSignalBlocker block(messages_); messages_->setRowCount(displayed_.size());
        int selectedRow = -1;
        for (int row = 0; row < displayed_.size(); ++row) {
            const auto &m = displayed_[row]; const auto f = m.fields;
            const QStringList cells{m.observedUtc.toLocalTime().toString("HH:mm:ss.zzz"), f.contains("deviceId") ? QString::number(f.value("deviceId").toInt()) : "—", f.contains("sequence") ? QString::number(f.value("sequence").toInt()) : "—", f.contains("function") ? "0x" + QString::number(f.value("function").toInt(), 16).toUpper() : "—", f.value("direction").toString("—"), m.status, QString::number(m.sources.size())};
            for (int c = 0; c < cells.size(); ++c) messages_->setItem(row, c, new QTableWidgetItem(cells[c]));
            if (m.id == selected) selectedRow = row;
        }
        if (selectedRow < 0 && !displayed_.isEmpty()) selectedRow = 0;
        messages_->selectRow(selectedRow); details();
    }
public:
    UppView(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        auto *layout = new QHBoxLayout(this); auto *side = new QWidget(this); side->setFixedWidth(220); auto *left = new QVBoxLayout(side);
        auto *title = new QLabel(QStringLiteral("UPP CAN 接收解析"), side); left->addWidget(title);
        auto *note = new QLabel(QStringLiteral("暂定规则：0=256字节；异常码2字节；末片无填充。仅观察，不发送。未知类型保留原始字节。方向/超时在连接配置中设置。"), side); note->setWordWrap(true); left->addWidget(note);
        left->addWidget(new QLabel(QStringLiteral("设备地址筛选（十进制，空为全部）"), side)); device_ = new QLineEdit(side); device_->setObjectName("uppDeviceFilter"); left->addWidget(device_);
        controls_ = new QWidget(side); controls_->setLayout(new QVBoxLayout); left->addWidget(controls_); left->addStretch(); layout->addWidget(side);
        auto *right = new QVBoxLayout; layout->addLayout(right); summary_ = new QLabel(this); summary_->setObjectName("uppSummary"); right->addWidget(summary_);
        auto *split = new QSplitter(Qt::Vertical, this); right->addWidget(split);
        messages_ = new QTableWidget(0, 7, split); messages_->setObjectName("uppMessages"); messages_->setHorizontalHeaderLabels({QStringLiteral("观测时间"), QStringLiteral("设备"), QStringLiteral("序号"), QStringLiteral("功能"), QStringLiteral("方向"), QStringLiteral("状态"), QStringLiteral("来源帧")});
        messages_->setSelectionBehavior(QAbstractItemView::SelectRows); messages_->setSelectionMode(QAbstractItemView::SingleSelection); messages_->setEditTriggers(QAbstractItemView::NoEditTriggers); messages_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); messages_->horizontalHeader()->setStretchLastSection(true);
        fields_ = new QTreeWidget(split); fields_->setObjectName("uppFields"); fields_->setHeaderLabels({QStringLiteral("字段"), QStringLiteral("值")}); fields_->setColumnWidth(0, 230);
        sources_ = new QTableWidget(0, 6, split); sources_->setObjectName("uppSources"); sources_->setHorizontalHeaderLabels({QStringLiteral("记录ID"), QStringLiteral("偏移"), QStringLiteral("长度"), QStringLiteral("原始字节/状态"), QStringLiteral("CAN ID"), QStringLiteral("后端元数据")}); sources_->setEditTriggers(QAbstractItemView::NoEditTriggers); sources_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        split->setSizes({260, 300, 180});
        connect(messages_, &QTableWidget::itemSelectionChanged, this, [this]{ details(); }); connect(device_, &QLineEdit::textChanged, this, [this]{ refresh(); });
        auto *timer = new QTimer(this); timer->setInterval(100); connect(timer, &QTimer::timeout, this, [this]{ refresh(); }); timer->start(); refresh();
    }
    QList<ParameterDevice> devices() const override {
        QSet<int> found; QList<ParameterDevice> result;
        if (access_.messages) for (const auto &m : access_.messages(0)) if (m.fields.contains("deviceId")) found.insert(m.fields.value("deviceId").toInt());
        auto ids = found.values(); std::sort(ids.begin(), ids.end()); for (int id : ids) result.append({QString::number(id), QStringLiteral("设备 %1").arg(id)}); return result;
    }
    QString selectedDevice() const override { return device_->text(); }
    bool selectDevice(const QString &id) override { if (!id.isEmpty()) { bool exists = false; for (const auto &d : devices()) exists |= d.id == id; if (!exists) return false; } device_->setText(id); return true; }
    QWidget *sessionControlsHost() const override { return controls_; }
};
}
ParameterView *createUppView(SessionAccess access, QWidget *parent) { return new UppView(std::move(access), parent); }
