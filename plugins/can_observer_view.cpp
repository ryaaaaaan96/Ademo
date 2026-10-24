#include "can_observer_view.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
using namespace debugger;
namespace {
QString idText(const TraceRecord &r) {
    return r.metadata.value("canIdAvailable").toBool() ? QString("0x%1").arg(r.metadata.value("canId").toInt(), r.metadata.value("extended").toBool() ? 8 : 3, 16, QChar('0')).toUpper() : QStringLiteral("不可用");
}
QString typeText(const QString &type) {
    if (type == "data") return QStringLiteral("数据帧");
    if (type == "remote") return QStringLiteral("远程帧");
    if (type == "error") return QStringLiteral("错误帧");
    return QStringLiteral("未知帧");
}
QString formatText(const QJsonObject &metadata) {
    return !metadata.value("formatAvailable").toBool() ? QStringLiteral("不可用") : metadata.value("extended").toBool() ? QStringLiteral("扩展") : QStringLiteral("标准");
}
QString csv(QString value) { value.replace('"', "\"\""); return '"' + value + '"'; }
QJsonObject exportFrames(const QList<TraceRecord> &records, const QJsonObject &status, const QString &scope, const QString &path, const QString &format) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {{"error", file.errorString()}};
    auto write = [&file](const QByteArray &bytes) { return file.write(bytes) == bytes.size(); };
    auto summary = status; summary["kind"] = "can-export-summary"; summary["scope"] = scope; summary["frameCount"] = records.size();
    bool ok = true;
    if (format == "JSONL") {
        ok = write(QJsonDocument(summary).toJson(QJsonDocument::Compact) + '\n');
        for (const auto &r : records) {
            QJsonObject row{{"kind", "can-frame"}, {"recordId", QString::number(r.id)}, {"generation", QString::number(r.generation)},
                {"sessionId", status.value("sessionId")}, {"direction", r.kind}, {"observedUtc", r.observedUtc.toString(Qt::ISODateWithMs)},
                {"monotonicUs", QString::number(r.monotonicUs)}, {"bytesHex", QString::fromLatin1(r.bytes.toHex())}, {"metadata", r.metadata}};
            ok = ok && write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
        }
    } else {
        ok = write("kind,sessionId,recordId,generation,observedUtc,direction,canId,format,type,dataLength,bytesHex,expectedResponseLength,declaredDlc,classicSupported,metadataJson\r\n");
        QStringList summaryRow{"summary", status.value("sessionId").toString()};
        while (summaryRow.size() < 14) summaryRow << QString();
        summaryRow << QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact));
        for (auto &cell : summaryRow) cell = csv(cell);
        ok = ok && write(summaryRow.join(',').toUtf8() + "\r\n");
        for (const auto &r : records) {
            const auto m = r.metadata;
            QStringList cells{"frame", status.value("sessionId").toString(), QString::number(r.id), QString::number(r.generation),
                r.observedUtc.toString(Qt::ISODateWithMs), r.kind, idText(r), !m.value("formatAvailable").toBool() ? "unavailable" : m.value("extended").toBool() ? "extended" : "standard",
                m.value("frameType").toString(), QString::number(r.bytes.size()), QString::fromLatin1(r.bytes.toHex()),
                m.value("remoteExpectedLength").isDouble() ? QString::number(m.value("remoteExpectedLength").toInt()) : "unavailable",
                m.value("declaredDlc").isDouble() ? QString::number(m.value("declaredDlc").toInt()) : "unavailable",
                m.value("classicSupported").toBool() ? "true" : "false", QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact))};
            for (auto &cell : cells) cell = csv(cell);
            ok = ok && write(cells.join(',').toUtf8() + "\r\n");
        }
    }
    if (!ok || !file.commit()) return {{"error", file.errorString()}};
    return {{"ok", true}, {"count", records.size()}, {"path", path}};
}
class CanObserverView final : public ParameterView {
public:
    CanObserverView(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        setObjectName("canObserver");
        auto *outer = new QHBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
        auto *side = new QWidget(this); side->setMaximumWidth(245); side->setMinimumWidth(190);
        auto *left = new QVBoxLayout(side);
        auto *title = new QLabel(QStringLiteral("CAN 原始帧观察"), side); title->setStyleSheet("font-weight: bold; font-size: 16px"); left->addWidget(title);
        controls_ = new QWidget(side); auto *controlsLayout = new QVBoxLayout(controls_); controlsLayout->setContentsMargins(0, 0, 0, 0); left->addWidget(controls_);
        auto *note = new QLabel(QStringLiteral("仅观察经典 CAN；不解析 UPP。\n虚拟 CAN 为软件测试来源。\n观察不等同于硬件静默模式。"), side); note->setWordWrap(true); left->addWidget(note);
        auto *form = new QFormLayout;
        id_ = new QLineEdit(side); id_->setObjectName("canIdFilter"); id_->setPlaceholderText(QStringLiteral("十六进制，留空全部"));
        format_ = new QComboBox(side); format_->setObjectName("canFormatFilter"); format_->addItem(QStringLiteral("全部"), ""); format_->addItem(QStringLiteral("标准"), "standard"); format_->addItem(QStringLiteral("扩展"), "extended");
        type_ = new QComboBox(side); type_->setObjectName("canTypeFilter"); type_->addItem(QStringLiteral("全部"), "");
        for (const auto &type : {"data", "remote", "error", "unknown"}) type_->addItem(typeText(type), type);
        form->addRow(QStringLiteral("CAN ID"), id_); form->addRow(QStringLiteral("格式"), format_); form->addRow(QStringLiteral("帧类型"), type_); left->addLayout(form);
        scope_ = new QComboBox(side); scope_->setObjectName("canExportScope"); scope_->addItem(QStringLiteral("当前筛选的保留帧"), "filtered"); scope_->addItem(QStringLiteral("全部保留帧"), "all"); left->addWidget(scope_);
        exportFormat_ = new QComboBox(side); exportFormat_->setObjectName("canExportFormat"); exportFormat_->addItems({"JSONL", "CSV"}); left->addWidget(exportFormat_);
        path_ = new QLineEdit(side); path_->setObjectName("canExportPath"); path_->setPlaceholderText(QStringLiteral("导出文件路径")); left->addWidget(path_);
        auto *browse = new QPushButton(QStringLiteral("选择导出文件…"), side); left->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto lease = access_.lifetime; QPointer<CanObserverView> alive(this);
            choosing_ = true;
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("选择 CAN 记录导出文件"), path_->text());
            if (!alive) return;
            choosing_ = false; if (!path.isEmpty()) path_->setText(path);
        });
        export_ = new QPushButton(QStringLiteral("导出快照"), side); export_->setObjectName("canExport"); left->addWidget(export_);
        result_ = new QLabel(side); result_->setObjectName("canExportResult"); result_->setWordWrap(true); left->addWidget(result_); left->addStretch();
        outer->addWidget(side);
        auto *main = new QVBoxLayout; outer->addLayout(main, 1);
        status_ = new QLabel(this); status_->setObjectName("canRecordStatus"); status_->setWordWrap(true); main->addWidget(status_);
        table_ = new QTableWidget(this); table_->setObjectName("canFrames"); table_->setAccessibleName(QStringLiteral("CAN 原始帧列表"));
        table_->setColumnCount(9); table_->setHorizontalHeaderLabels({QStringLiteral("观测时间"), QStringLiteral("方向"), "CAN ID", QStringLiteral("格式"), QStringLiteral("类型"), QStringLiteral("数据长度"), QStringLiteral("原始数据"), QStringLiteral("期望长度"), QStringLiteral("说明")});
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table_->horizontalHeader()->setStretchLastSection(true); main->addWidget(table_, 1);
        auto *timer = new QTimer(this); timer->setInterval(100); connect(timer, &QTimer::timeout, this, [this] { refresh(); }); timer->start();
        connect(id_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(format_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(type_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(export_, &QPushButton::clicked, this, [this] { exportCurrent(); });
        if (access_.registerViewState) access_.registerViewState(this, [this] { return LocalState{false, false, choosing_, {}}; }, [this] { return !choosing_; });
        refresh();
    }
    QList<ParameterDevice> devices() const override { return {}; }
    QString selectedDevice() const override { return {}; }
    bool selectDevice(const QString &) override { return false; }
    QWidget *sessionControlsHost() const override { return controls_; }
private:
    QList<TraceRecord> selected(bool all, bool *valid) const {
        QList<TraceRecord> result; const auto text = id_->text().trimmed();
        bool idOk = true; const auto id = text.isEmpty() ? 0 : text.toUInt(&idOk, 16); idOk = idOk && id <= 0x1fffffff;
        *valid = all || idOk; if (!*valid) return result;
        for (const auto &r : access_.traces ? access_.traces(0) : QList<TraceRecord>{}) {
            if (r.kind != "rx" || r.metadata.value("unitKind") != "can-frame") continue;
            const auto m = r.metadata;
            if (!all && ((!text.isEmpty() && (!m.value("canIdAvailable").toBool() || quint32(m.value("canId").toInt()) != id))
                || (!format_->currentData().toString().isEmpty() && (!m.value("formatAvailable").toBool() || m.value("extended").toBool() != (format_->currentData() == "extended")))
                || (!type_->currentData().toString().isEmpty() && m.value("frameType") != type_->currentData().toString()))) continue;
            result << r;
        }
        return result;
    }
    void refresh() {
        bool valid; const auto rows = selected(false, &valid); const auto status = access_.recordStatus ? access_.recordStatus() : QJsonObject{};
        status_->setText(QStringLiteral("会话 %1 · 当前 %2 帧 · 容量 %3 · 正常过期 %4 · 磁盘丢失 %5 / 轮换 %6%7")
            .arg(status.value("sessionId").toString()).arg(rows.size()).arg(status.value("recordCapacity").toInt()).arg(status.value("expiredRecords").toString(), status.value("diskDroppedEvents").toString(), status.value("diskRotatedEvents").toString(), valid ? QString() : QStringLiteral(" · CAN ID 无效")));
        QByteArray signature = QJsonDocument(status).toJson(QJsonDocument::Compact) + id_->text().toUtf8() + format_->currentData().toByteArray() + type_->currentData().toByteArray();
        for (const auto &r : rows) signature += QByteArray::number(r.id) + ',';
        if (signature == signature_) return; signature_ = signature;
        table_->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row) {
            const auto &r = rows[row]; const auto m = r.metadata;
            QStringList cells{r.observedUtc.toLocalTime().toString("HH:mm:ss.zzz"), "RX", idText(r), formatText(m),
                typeText(m.value("frameType").toString()), QString::number(r.bytes.size()), QString::fromLatin1(r.bytes.toHex(' ')),
                m.value("remoteExpectedLength").isDouble() ? QString::number(m.value("remoteExpectedLength").toInt()) : QStringLiteral("不可用"),
                m.value("diagnostic").toString().isEmpty() ? m.value("sourceKind").toString() : m.value("diagnostic").toString()};
            for (int col = 0; col < cells.size(); ++col) { auto *item = new QTableWidgetItem(cells[col]); item->setToolTip(QStringLiteral("原始记录 #%1\n%2").arg(r.id).arg(QString::fromUtf8(QJsonDocument(m).toJson()))); table_->setItem(row, col, item); }
        }
    }
    void exportCurrent() {
        if (busy_) return;
        bool valid; const auto rows = selected(scope_->currentData() == "all", &valid);
        if (!valid || path_->text().trimmed().isEmpty()) { result_->setText(QStringLiteral("请填写有效筛选和导出路径")); return; }
        const auto status = access_.recordStatus ? access_.recordStatus() : QJsonObject{};
        const auto path = path_->text(); const auto format = exportFormat_->currentText(); const auto scope = scope_->currentData().toString();
        busy_ = true; export_->setEnabled(false); result_->setText(QStringLiteral("正在导出固定快照…"));
        QPointer<CanObserverView> alive(this);
        const bool accepted = access_.background && access_.background([rows, status, scope, path, format] { return exportFrames(rows, status, scope, path, format); }, [alive](QJsonObject result) {
            if (!alive) return;
            alive->busy_ = false; alive->export_->setEnabled(true);
            alive->result_->setText(result.value("ok").toBool() ? QStringLiteral("已导出 %1 帧：%2").arg(result.value("count").toInt()).arg(result.value("path").toString()) : QStringLiteral("导出失败：%1").arg(result.value("error").toString()));
        });
        if (!accepted) { busy_ = false; export_->setEnabled(true); result_->setText(QStringLiteral("后台任务繁忙，请稍后重试")); }
    }
    SessionAccess access_; QWidget *controls_; QLineEdit *id_, *path_; QComboBox *format_, *type_, *scope_, *exportFormat_;
    QTableWidget *table_; QLabel *status_, *result_; QPushButton *export_; bool busy_ = false, choosing_ = false; QByteArray signature_;
};
}
ParameterView *createCanObserverView(SessionAccess access, QWidget *parent) { return new CanObserverView(std::move(access), parent); }
