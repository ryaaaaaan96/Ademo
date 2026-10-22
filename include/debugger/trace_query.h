#pragma once
#include "sdk.h"
#include <future>
#include <limits>

namespace debugger {
struct TraceLimits {
    int records = 256;
    int messages = 256;
    int operations = 128;
};
struct TraceFilter {
    quint64 operationId = 0;
    quint64 messageId = 0;
    QDateTime fromUtc;
    QDateTime toUtc;
};
struct ResolvedSource {
    TraceRange range;
    QString availability; // available / expired / unavailable / invalid-range
    QByteArray bytes;
};
struct TraceSnapshot {
    QList<TraceRecord> records;
    QList<Message> messages;
    QList<OperationSnapshot> operations;
    QList<ResolvedSource> sources;
    quint64 expiredRecords = 0;
    quint64 expiredMessages = 0;
    quint64 expiredOperations = 0;
    quint64 omittedObservedRefs = 0;
    bool requestedOperationMissing = false;
    bool requestedMessageMissing = false;
};
struct ExportResult {
    bool ok = false;
    QString error;
    qsizetype records = 0;
    qsizetype missingSources = 0;
};
// 仅后台文件写入；所有输入均为脱离会话生命周期的有界值快照。
std::shared_future<ExportResult> exportSnapshot(std::function<TraceSnapshot()> snapshot, QString path, QString format);
class DebugSession;
class TracePanel : public QWidget {
public:
    explicit TracePanel(DebugSession &, QWidget *parent = nullptr);
};
}
