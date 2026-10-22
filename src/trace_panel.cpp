#include "debugger/host.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace debugger {
TracePanel::TracePanel(DebugSession &session, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *expand = new QPushButton(QStringLiteral("展开回溯 / 收起"), this);
    expand->setObjectName("traceExpand");
    auto *summary = new QLabel(this);
    summary->setObjectName("traceSummary");
    layout->addWidget(expand);
    layout->addWidget(summary);
    auto *details = new QGroupBox(QStringLiteral("操作、消息与原始证据"), this);
    details->hide();
    layout->addWidget(details);
    auto *form = new QFormLayout(details);
    auto *operation = new QLineEdit(details);
    auto *message = new QLineEdit(details);
    auto *from = new QLineEdit(details);
    auto *to = new QLineEdit(details);
    operation->setObjectName("traceOperation"); message->setObjectName("traceMessage");
    from->setObjectName("traceFromUtc"); to->setObjectName("traceToUtc");
    operation->setPlaceholderText(QStringLiteral("留空表示全部"));
    message->setPlaceholderText(QStringLiteral("留空表示全部"));
    from->setPlaceholderText("2026-10-22T00:00:00.000Z"); to->setPlaceholderText(from->placeholderText());
    form->addRow(QStringLiteral("操作 ID"), operation); form->addRow(QStringLiteral("消息 ID"), message);
    form->addRow(QStringLiteral("起始 UTC（含）"), from); form->addRow(QStringLiteral("结束 UTC（含）"), to);
    auto *capacity = new QSpinBox(details); capacity->setRange(1, 8192); capacity->setValue(session.traceLimits().records);
    auto *messageCapacity = new QSpinBox(details); messageCapacity->setRange(1, 8192); messageCapacity->setValue(session.traceLimits().messages);
    auto *operationCapacity = new QSpinBox(details); operationCapacity->setRange(1, 1024); operationCapacity->setValue(session.traceLimits().operations);
    capacity->setObjectName("traceCapacity"); messageCapacity->setObjectName("messageCapacity"); operationCapacity->setObjectName("operationCapacity");
    form->addRow(QStringLiteral("近期原始记录上限"), capacity);
    form->addRow(QStringLiteral("近期消息上限"), messageCapacity);
    form->addRow(QStringLiteral("近期操作上限"), operationCapacity);
    auto *apply = new QPushButton(QStringLiteral("应用容量"), details);
    auto *query = new QPushButton(QStringLiteral("查询证据"), details);
    apply->setObjectName("traceApplyCapacity"); query->setObjectName("traceQuery");
    form->addRow(apply); form->addRow(query);
    auto *output = new QPlainTextEdit(details); output->setReadOnly(true); output->setObjectName("traceQueryResult");
    output->setMaximumBlockCount(2000); form->addRow(output);
    auto *path = new QLineEdit(details); path->setObjectName("traceExportPath");
    auto *choose = new QPushButton(QStringLiteral("选择导出文件"), details);
    auto *format = new QComboBox(details); format->addItems({"jsonl", "csv"}); format->setObjectName("traceExportFormat");
    auto *exportButton = new QPushButton(QStringLiteral("后台导出当前筛选"), details); exportButton->setObjectName("traceExport");
    auto *exportStatus = new QLabel(details); exportStatus->setObjectName("traceExportStatus"); exportStatus->setWordWrap(true);
    form->addRow(QStringLiteral("导出路径"), path); form->addRow(choose); form->addRow(QStringLiteral("格式"), format);
    form->addRow(exportButton); form->addRow(exportStatus);
    connect(expand, &QPushButton::clicked, details, [details] { details->setVisible(details->isHidden()); });
    auto filter = [operation, message, from, to](TraceFilter &value) {
        bool ok = true;
        if (!operation->text().trimmed().isEmpty()) { value.operationId = operation->text().toULongLong(&ok); if (!ok || !value.operationId) return false; }
        if (!message->text().trimmed().isEmpty()) { value.messageId = message->text().toULongLong(&ok); if (!ok || !value.messageId) return false; }
        if (!from->text().isEmpty()) { value.fromUtc = QDateTime::fromString(from->text(), Qt::ISODateWithMs); if (!value.fromUtc.isValid() || !from->text().endsWith('Z')) return false; }
        if (!to->text().isEmpty()) { value.toUtc = QDateTime::fromString(to->text(), Qt::ISODateWithMs); if (!value.toUtc.isValid() || !to->text().endsWith('Z')) return false; }
        return !value.fromUtc.isValid() || !value.toUtc.isValid() || value.fromUtc <= value.toUtc;
    };
    connect(apply, &QPushButton::clicked, this, [&session, capacity, messageCapacity, operationCapacity, exportStatus] {
        QString error;
        exportStatus->setText(session.configureTrace({capacity->value(), messageCapacity->value(), operationCapacity->value()}, &error)
            ? QStringLiteral("容量已应用，淘汰计数保留") : error);
    });
    connect(query, &QPushButton::clicked, this, [&session, filter, output] {
        TraceFilter selection;
        if (!filter(selection)) { output->setPlainText(QStringLiteral("筛选无效：ID 为正整数，时间为带 Z 的 ISO UTC，结束不早于开始")); return; }
        const auto snapshot = session.queryTrace(selection);
        QString text = QStringLiteral("原始 %1 / 消息 %2 / 操作 %3；正常过期 %4 / %5 / %6；省略观察引用 %7\n")
            .arg(snapshot.records.size()).arg(snapshot.messages.size()).arg(snapshot.operations.size())
            .arg(snapshot.expiredRecords).arg(snapshot.expiredMessages).arg(snapshot.expiredOperations).arg(snapshot.omittedObservedRefs);
        if (snapshot.requestedMessageMissing || snapshot.requestedOperationMissing) text += QStringLiteral("指定操作或消息已过期或不存在\n");
        text += QStringLiteral("面板最多显示 100 项各类概要、每条原始 128 字节；完整内容通过后台导出。\n");
        for (const auto &operation : snapshot.operations.mid(0, 100)) text += QStringLiteral("操作 #%1 %2 %3：%4\n").arg(operation.request.id).arg(operation.request.action, operation.update.state, operation.update.detail);
        for (const auto &message : snapshot.messages.mid(0, 100)) {
            text += QStringLiteral("消息 #%1 操作 #%2 %3：%4\n").arg(message.id).arg(message.operationId).arg(message.status, message.detail);
            for (const auto &range : message.sourceRanges) text += QStringLiteral("  来源 #%1 offset=%2 length=%3\n").arg(range.recordId).arg(range.offset).arg(range.length);
        }
        for (const auto &source : snapshot.sources.mid(0, 100)) if (source.availability != "available")
            text += QStringLiteral("来源 #%1 %2：缺少原始字节\n").arg(source.range.recordId).arg(source.availability);
        for (const auto &record : snapshot.records.mid(0, 100)) text += QStringLiteral("原始 #%1 run=%2 %3 %4 %5\n").arg(record.id).arg(record.generation)
            .arg(record.observedUtc.toString(Qt::ISODateWithMs), record.kind, QString::fromLatin1(record.bytes.left(128).toHex(' ')));
        output->setPlainText(text);
    });
    connect(choose, &QPushButton::clicked, this, [this, path] {
        const auto selected = QFileDialog::getSaveFileName(this, QStringLiteral("导出回溯"), path->text(), "JSONL/CSV (*.jsonl *.csv)");
        if (!selected.isEmpty()) path->setText(selected);
    });
    auto future = std::make_shared<std::shared_future<ExportResult>>();
    auto *timer = new QTimer(this); timer->setInterval(50);
    connect(exportButton, &QPushButton::clicked, this, [&session, filter, path, format, future, timer, exportButton, exportStatus] {
        TraceFilter selection;
        if (!filter(selection) || path->text().isEmpty()) { exportStatus->setText(QStringLiteral("请填写有效筛选与导出路径")); return; }
        *future = session.exportTrace(selection, path->text(), format->currentText());
        exportButton->setEnabled(false); exportStatus->setText(QStringLiteral("后台写入快照，通信继续")); timer->start();
    });
    connect(timer, &QTimer::timeout, this, [future, timer, exportButton, exportStatus] {
        if (!future->valid() || future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        const auto result = future->get(); timer->stop(); exportButton->setEnabled(true);
        exportStatus->setText(result.ok ? QStringLiteral("导出完成：%1 条原始记录，%2 个缺失来源").arg(result.records).arg(result.missingSources)
                                        : QStringLiteral("导出失败：") + result.error);
    });
    auto refresh = [&session, summary] {
        summary->setText(QStringLiteral("近期原始记录 %1 / %2；正常过期 %3；按需展开查询与导出")
            .arg(session.traces().size()).arg(session.traceLimits().records).arg(session.expiredTraceCount()));
    };
    connect(&session, &DebugSession::changed, this, refresh); refresh();
}
}
