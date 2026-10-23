#include "debugger/host.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace debugger {
SessionRecords::SessionRecords(DebugSession &session, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto *header = new QHBoxLayout;
    auto *toggle = new QPushButton(QStringLiteral("通信记录"), this);
    toggle->setCheckable(true); toggle->setChecked(true);
    toggle->setToolTip(QStringLiteral("展开或折叠本会话记录，不停止通信"));
    auto *owner = new QLabel(QStringLiteral("记录所属会话 %1").arg(session.sessionId()), this);
    owner->setWordWrap(true); owner->setAccessibleName(QStringLiteral("记录所属会话"));
    header->addWidget(toggle); header->addWidget(owner, 1); layout->addLayout(header);
    auto *tabs = new QTabWidget(this); tabs->setMinimumHeight(100); tabs->setMaximumHeight(150);
    tabs->setAccessibleName(QStringLiteral("通信记录页面"));
    layout->addWidget(tabs);
    auto *summary = new QPlainTextEdit(tabs);
    summary->setAccessibleName(QStringLiteral("本会话通信记录"));
    summary->setReadOnly(true); summary->setMaximumBlockCount(100);
    summary->setStyleSheet("QPlainTextEdit { background: white; color: #223344; }");
    tabs->addTab(summary, QStringLiteral("通信概要"));
    auto addPage = [tabs](QWidget *panel, const QString &title) {
        auto *scroll = new QScrollArea(tabs); scroll->setWidgetResizable(true); scroll->setWidget(panel);
        tabs->addTab(scroll, title);
    };
    addPage(new TracePanel(session, tabs, false), QStringLiteral("回溯与导出"));
    addPage(new RecordingPanel(session, tabs, false), QStringLiteral("磁盘与历史"));
    connect(toggle, &QPushButton::toggled, tabs, &QWidget::setVisible);
    connect(tabs, &QTabWidget::currentChanged, tabs, [tabs](int index) { tabs->setMaximumHeight(index == 0 ? 150 : 420); });
    auto refresh = [&session, summary] {
        const auto records = session.traces();
        QStringList lines;
        lines << QStringLiteral("近期原始 %1 / %2；正常过期 %3；详情及缺失来源请打开回溯页")
            .arg(records.size()).arg(session.traceLimits().records).arg(session.expiredTraceCount());
        const auto start = qMax(qsizetype(0), records.size() - 80);
        for (auto i = start; i < records.size(); ++i) {
            const auto &record = records[i];
            lines << QStringLiteral("%1 · #%2 · %3 · %4%5 · %6")
                .arg(record.observedUtc.toLocalTime().toString("HH:mm:ss.zzz")).arg(record.id)
                .arg(record.kind, QString::fromLatin1(record.bytes.left(128).toHex(' ')),
                     record.bytes.size() > 128 ? QStringLiteral(" …（完整字节见导出）") : QString(), record.detail.left(256));
        }
        const auto text = lines.join('\n');
        if (text != summary->toPlainText()) summary->setPlainText(text);
    };
    connect(&session, &DebugSession::changed, this, refresh); refresh();
}
}
