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
RecordingPanel::RecordingPanel(DebugSession &session, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *expand = new QPushButton(QStringLiteral("磁盘记录与历史归档 / 展开或收起"), this); expand->setObjectName("recordingExpand");
    auto *status = new QLabel(this); status->setWordWrap(true); status->setObjectName("recordingStatus");
    auto *group = new QGroupBox(QStringLiteral("用户主动开启；队列丢失与正常轮换分别统计"), this); group->hide();
    layout->addWidget(expand); layout->addWidget(status); layout->addWidget(group);
    auto *form = new QFormLayout(group);
    auto *directory = new QLineEdit(group); directory->setObjectName("recordingDirectory");
    auto *choose = new QPushButton(QStringLiteral("选择记录/归档目录"), group);
    form->addRow(QStringLiteral("目录"), directory); form->addRow(choose);
    auto spin = [group, form](const QString &name, const QString &label, int minimum, int maximum, int value) {
        auto *editor = new QSpinBox(group); editor->setObjectName(name); editor->setRange(minimum, maximum); editor->setValue(value);
        form->addRow(label, editor); return editor;
    };
    auto *items = spin("recordingQueueItems", QStringLiteral("写入队列项数"), 1, 8192, 1024);
    auto *queue = spin("recordingQueueKiB", QStringLiteral("写入队列 KiB"), 1, 32768, 8192);
    auto *disk = spin("recordingDiskKiB", QStringLiteral("磁盘保留 KiB（含索引预留）"), 256, 524288, 65536);
    auto *segment = spin("recordingSegmentKiB", QStringLiteral("每分段 KiB"), 1, 8192, 1024);
    auto *start = new QPushButton(QStringLiteral("开启后台磁盘记录"), group); start->setObjectName("recordingStart");
    auto *stop = new QPushButton(QStringLiteral("停止记录并后台排空"), group); stop->setObjectName("recordingStop");
    form->addRow(start); form->addRow(stop);
    auto *sessions = new QComboBox(group); sessions->setEditable(true); sessions->setObjectName("archiveSession");
    auto *list = new QPushButton(QStringLiteral("读取可用历史会话列表"), group); list->setObjectName("archiveList");
    auto *operation = new QLineEdit(group); operation->setObjectName("archiveOperation");
    auto *message = new QLineEdit(group); message->setObjectName("archiveMessage");
    auto *from = new QLineEdit(group); from->setObjectName("archiveFromUtc"); from->setPlaceholderText("ISO UTC / Z");
    auto *to = new QLineEdit(group); to->setObjectName("archiveToUtc"); to->setPlaceholderText("ISO UTC / Z");
    form->addRow(list); form->addRow(QStringLiteral("会话 UUID（必须明确选择）"), sessions);
    form->addRow(QStringLiteral("操作 ID（留空为全部）"), operation); form->addRow(QStringLiteral("消息 ID（留空为全部）"), message);
    form->addRow(QStringLiteral("起始 UTC（含）"), from); form->addRow(QStringLiteral("结束 UTC（含）"), to);
    auto *query = new QPushButton(QStringLiteral("后台查询历史"), group); query->setObjectName("archiveQuery"); form->addRow(query);
    auto *output = new QPlainTextEdit(group); output->setReadOnly(true); output->setMaximumBlockCount(2000); output->setObjectName("archiveResult");
    form->addRow(output);
    auto *path = new QLineEdit(group); path->setObjectName("archiveExportPath");
    auto *chooseExport = new QPushButton(QStringLiteral("选择历史导出文件"), group);
    auto *format = new QComboBox(group); format->addItems({"jsonl", "csv"}); format->setObjectName("archiveExportFormat");
    auto *exportButton = new QPushButton(QStringLiteral("后台导出已查询的历史快照"), group); exportButton->setObjectName("archiveExport"); exportButton->setEnabled(false);
    form->addRow(QStringLiteral("导出路径"), path); form->addRow(chooseExport); form->addRow(QStringLiteral("格式"), format); form->addRow(exportButton);
    connect(expand, &QPushButton::clicked, group, [group] { group->setVisible(group->isHidden()); });
    connect(choose, &QPushButton::clicked, this, [this, directory] {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择记录/归档目录"), directory->text());
        if (!path.isEmpty()) directory->setText(path);
    });
    connect(start, &QPushButton::clicked, this, [&session, directory, items, queue, disk, segment, output] {
        RecordingOptions options; options.directory = directory->text(); options.queueItems = items->value();
        options.queueBytes = qint64(queue->value()) * 1024; options.diskBytes = qint64(disk->value()) * 1024;
        options.segmentBytes = qint64(segment->value()) * 1024;
        QString error; if (!session.startRecording(options, &error)) output->setPlainText(error);
    });
    connect(stop, &QPushButton::clicked, this, [&session] { session.stopRecording(); });
    auto result = std::make_shared<std::shared_ptr<ArchiveResult>>();
    auto future = std::make_shared<std::shared_future<ArchiveResult>>();
    auto exporting = std::make_shared<std::shared_future<ExportResult>>();
    auto listOnly = std::make_shared<bool>(false);
    auto launch = [directory, sessions, operation, message, from, to, output, list, query, future, listOnly](bool listing) {
        TraceFilter filter; bool ok = true;
        if (!operation->text().isEmpty()) { filter.operationId = operation->text().toULongLong(&ok); if (!ok || !filter.operationId) { output->setPlainText(QStringLiteral("操作 ID 无效")); return; } }
        if (!message->text().isEmpty()) { filter.messageId = message->text().toULongLong(&ok); if (!ok || !filter.messageId) { output->setPlainText(QStringLiteral("消息 ID 无效")); return; } }
        if (!from->text().isEmpty()) filter.fromUtc = QDateTime::fromString(from->text(), Qt::ISODateWithMs);
        if (!to->text().isEmpty()) filter.toUtc = QDateTime::fromString(to->text(), Qt::ISODateWithMs);
        if ((!from->text().isEmpty() && (!filter.fromUtc.isValid() || !from->text().endsWith('Z')))
            || (!to->text().isEmpty() && (!filter.toUtc.isValid() || !to->text().endsWith('Z')))
            || (filter.fromUtc.isValid() && filter.toUtc.isValid() && filter.fromUtc > filter.toUtc)) {
            output->setPlainText(QStringLiteral("时间必须为带 Z 的 ISO UTC，结束不早于开始")); return;
        }
        if (!listing && sessions->currentText().isEmpty()) { output->setPlainText(QStringLiteral("请选择历史会话 UUID")); return; }
        *listOnly = listing; *future = queryArchive(directory->text(), listing ? QString() : sessions->currentText(), filter);
        list->setEnabled(false); query->setEnabled(false); output->setPlainText(QStringLiteral("后台读取归档，通信继续"));
    };
    connect(list, &QPushButton::clicked, this, [launch] { launch(true); });
    connect(query, &QPushButton::clicked, this, [launch] { launch(false); });
    connect(chooseExport, &QPushButton::clicked, this, [this, path] {
        const auto selected = QFileDialog::getSaveFileName(this, QStringLiteral("导出历史"), path->text(), "JSONL/CSV (*.jsonl *.csv)");
        if (!selected.isEmpty()) path->setText(selected);
    });
    connect(exportButton, &QPushButton::clicked, this, [result, exporting, exportButton, path, format, output] {
        if (!*result || path->text().isEmpty()) { output->appendPlainText(QStringLiteral("请先查询并填写导出路径")); return; }
        const auto captured = *result;
        *exporting = exportSnapshot([captured] { return captured->snapshot; }, path->text(), format->currentText());
        exportButton->setEnabled(false);
    });
    auto *timer = new QTimer(this); timer->setInterval(200);
    connect(timer, &QTimer::timeout, this, [&session, status, start, stop, sessions, result, future, exporting, listOnly, output, list, query, exportButton] {
        const auto stats = session.recordingStats();
        status->setText(QStringLiteral("磁盘 %1；队列 %2 项 / %3 字节；过载丢失 %4（%5 — %6）；正常轮换 %7（%8 — %9）%10")
            .arg(stats.state).arg(stats.queuedItems).arg(stats.queuedBytes).arg(stats.droppedEvents)
            .arg(stats.lossFromUtc.toString(Qt::ISODateWithMs), stats.lossToUtc.toString(Qt::ISODateWithMs)).arg(stats.rotatedEvents)
            .arg(stats.rotationFromUtc.toString(Qt::ISODateWithMs), stats.rotationToUtc.toString(Qt::ISODateWithMs), stats.error));
        const bool active = stats.state == "starting" || stats.state == "running" || stats.state == "stopping";
        start->setEnabled(!active); stop->setEnabled(active && stats.state != "stopping");
        if (future->valid() && future->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const auto value = future->get(); *future = {}; list->setEnabled(true); query->setEnabled(true);
            if (!value.ok) { output->setPlainText(value.error); return; }
            if (*listOnly) {
                const auto selected = sessions->currentText(); sessions->clear(); sessions->addItems(value.sessions);
                if (!selected.isEmpty() && value.sessions.contains(selected)) sessions->setCurrentText(selected);
                output->setPlainText(QStringLiteral("可用历史会话 %1 个；请选择 UUID 后查询。\n").arg(value.sessions.size()) + value.error);
            } else {
                *result = std::make_shared<ArchiveResult>(value); exportButton->setEnabled(true);
                const auto retention = value.snapshot.retention;
                QString text = QStringLiteral("历史会话 %1：原始 %2 / 消息 %3 / 操作 %4\n")
                    .arg(value.snapshot.sessionId).arg(value.snapshot.records.size()).arg(value.snapshot.messages.size()).arg(value.snapshot.operations.size());
                text += retention.value("complete").toBool() ? QStringLiteral("当前归档范围完整\n") : QStringLiteral("历史证据存在缺失或未完成项，请查看来源状态；完整性摘要会随导出保留。\n");
                text += QStringLiteral("过载丢失 %1（%2 — %3）；正常轮换 %4（%5 — %6）\n查询省略 %7 项\n")
                    .arg(retention.value("droppedEvents").toString(), retention.value("lossFromUtc").toString(), retention.value("lossToUtc").toString(),
                         retention.value("rotatedEvents").toString(), retention.value("rotationFromUtc").toString(), retention.value("rotationToUtc").toString(),
                         retention.value("queryOmittedEntries").toString());
                if (retention.value("segmentsUnavailableDuringQuery").toBool()) text += QStringLiteral("查询期间部分分段已不可用。\n");
                if (retention.value("corruptOrIncompleteSegment").toBool() || retention.value("archiveGap").toBool()) text += QStringLiteral("存在损坏、未完整提交或无法证明的历史分段。\n");
                if (!retention.value("recordingError").toString().isEmpty()) text += QStringLiteral("历史写入错误：") + retention.value("recordingError").toString() + '\n';
                for (const auto &operation : value.snapshot.operations.mid(0, 100)) text += QStringLiteral("操作 #%1 %2 %3\n").arg(operation.request.id).arg(operation.update.state, operation.update.detail);
                for (const auto &message : value.snapshot.messages.mid(0, 100)) text += QStringLiteral("消息 #%1 操作 #%2 %3\n").arg(message.id).arg(message.operationId).arg(message.status);
                for (const auto &source : value.snapshot.sources.mid(0, 100)) text += QStringLiteral("来源 #%1 %2 offset=%3 length=%4 %5\n")
                    .arg(source.range.recordId).arg(source.availability).arg(source.range.offset).arg(source.range.length).arg(QString::fromLatin1(source.bytes.left(64).toHex(' ')));
                if (!value.error.isEmpty()) text += value.error;
                output->setPlainText(text);
            }
        }
        if (exporting->valid() && exporting->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const auto value = exporting->get(); *exporting = {}; exportButton->setEnabled(bool(*result));
            output->appendPlainText(value.ok ? QStringLiteral("历史导出完成，缺失来源 %1").arg(value.missingSources) : QStringLiteral("导出失败：") + value.error);
        }
    });
    timer->start(); status->setText(QStringLiteral("磁盘记录未开启"));
}
}
