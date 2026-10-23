#include "debugger/host.h"
#include "trace_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <thread>
#include <atomic>

namespace debugger {
bool DebugSession::configureTrace(TraceLimits limits, QString *error)
{
    if (limits.records < 1 || limits.records > 8192 || limits.messages < 1 || limits.messages > 8192
        || limits.operations < 1 || limits.operations > 1024) {
        if (error) *error = QStringLiteral("原始/消息容量为 1..8192，操作容量为 1..1024");
        return false;
    }
    int pending = 0;
    for (const auto &operation : operations_) if (!operation.finished) ++pending;
    if (pending > limits.operations) {
        if (error) *error = QStringLiteral("操作容量不能小于未完成操作数量");
        return false;
    }
    traceLimits_ = limits;
    while (traces_.size() > limits.records) { traces_.removeFirst(); ++expiredTraces_; }
    while (messages_.size() > limits.messages) { messages_.removeFirst(); ++expiredMessages_; }
    for (qsizetype i = 0; operations_.size() > limits.operations && i < operations_.size();) {
        if (operations_[i].finished) { operations_.removeAt(i); ++expiredOperations_; }
        else ++i;
    }
    if (!notificationTimer_.isActive()) notificationTimer_.start();
    return true;
}
TraceSnapshot DebugSession::queryTrace(const TraceFilter &filter) const
{
    TraceSnapshot all;
    all.sessionId = sessionId_; all.records = traces_; all.messages = messages_; all.operations = operations_;
    all.sessionConfiguration = {{"protocolId", config_.protocolId}, {"transportId", config_.transportId}, {"role", config_.role},
        {"endpoint", config_.endpoint}, {"protocol", config_.protocol}, {"recovery", config_.recovery}};
    all.expiredRecords = expiredTraces_; all.expiredMessages = expiredMessages_; all.expiredOperations = expiredOperations_;
    return filterSnapshot(all, filter);
}
TraceSnapshot filterSnapshot(const TraceSnapshot &all, const TraceFilter &filter)
{
    TraceSnapshot result = all;
    result.records.clear(); result.messages.clear(); result.operations.clear(); result.sources.clear(); result.omittedObservedRefs = 0;
    const auto &traces_ = all.records; const auto &messages_ = all.messages; const auto &operations_ = all.operations;
    auto operation = [&operations_](quint64 id) {
        for (const auto &operation : operations_) if (operation.request.id == id) return operation;
        return OperationSnapshot{};
    };
    quint64 largestId = 0; for (const auto &record : traces_) largestId = qMax(largestId, record.id);
    const auto inTime = [&filter](QDateTime time) {
        return (!filter.fromUtc.isValid() || time >= filter.fromUtc)
            && (!filter.toUtc.isValid() || time <= filter.toUtc);
    };
    QSet<quint64> refs;
    QList<TraceRange> ranges;
    QHash<quint64, TraceRecord> recordsById;
    for (const auto &record : traces_) recordsById.insert(record.id, record);
    const auto selectedOperation = operation(filter.operationId);
    if (filter.operationId) {
        result.requestedOperationMissing = selectedOperation.request.id == 0;
        for (auto id : selectedOperation.observedRefs) refs.insert(id);
    }
    const auto observedRefs = refs;
    bool foundMessage = false;
    for (const auto &message : messages_) {
        if (filter.messageId && message.id != filter.messageId) continue;
        if (filter.operationId && message.operationId != filter.operationId) {
            bool observed = false;
            for (auto id : message.sources) if (observedRefs.contains(id)) { observed = true; break; }
            if (!observed) continue;
        }
        if (!inTime(message.observedUtc)) continue;
        foundMessage = true;
        result.messages.append(message);
        for (const auto &range : message.sourceRanges) {
            ranges.append(range);
            refs.insert(range.recordId);
        }
    }
    result.requestedMessageMissing = filter.messageId && !foundMessage;
    QSet<quint64> included;
    for (const auto &record : traces_) {
        const bool related = refs.contains(record.id);
        if ((filter.operationId || filter.messageId) ? !related : !inTime(record.observedUtc)) continue;
        result.records.append(record);
        included.insert(record.id);
    }
    // 时间范围选中消息后补充完整来源；不能因块观测时间略早而丢掉消息前半段。
    for (auto id : refs) if (!included.contains(id) && recordsById.contains(id)) {
        result.records.append(recordsById.value(id));
        included.insert(id);
    }
    QSet<quint64> rangedIds;
    for (const auto &range : ranges) rangedIds.insert(range.recordId);
    for (const auto &operation : operations_) {
        bool selected = !filter.operationId && !filter.messageId && !filter.fromUtc.isValid() && !filter.toUtc.isValid();
        if (filter.operationId) selected = operation.request.id == filter.operationId;
        else {
            for (const auto &message : result.messages) if (message.operationId == operation.request.id) selected = true;
            for (auto id : operation.observedRefs) if (included.contains(id)) selected = true;
        }
        if (!selected) continue;
        result.operations.append(operation);
        result.omittedObservedRefs += operation.omittedObservedRefCount;
        for (auto id : operation.observedRefs) {
            if (!included.contains(id) && recordsById.contains(id)) {
                result.records.append(recordsById.value(id));
                included.insert(id);
            }
            if (!rangedIds.contains(id)) {
                rangedIds.insert(id);
                ranges.append({id, 0, recordsById.contains(id) ? int(recordsById.value(id).bytes.size()) : -1});
            }
        }
    }
    for (const auto &range : ranges) {
        ResolvedSource source{range, {}, {}};
        const auto record = recordsById.constFind(range.recordId);
        if (record == recordsById.cend()) source.availability = range.recordId && range.recordId <= largestId && all.expiredRecords ? "expired" : "unavailable";
        else if (range.offset < 0 || range.length < 0 || range.offset > record->bytes.size()
                 || range.length > record->bytes.size() - range.offset) source.availability = "invalid-range";
        else { source.availability = "available"; source.bytes = record->bytes.mid(range.offset, range.length); }
        result.sources.append(std::move(source));
    }
    return result;
}
std::shared_future<ExportResult> DebugSession::exportTrace(const TraceFilter &filter, const QString &path, const QString &format) const
{
    return exportSnapshot([this, filter] { return queryTrace(filter); }, path, format);
}
namespace {
QString idString(quint64 id) { return QString::number(id); }
QJsonArray ids(const QList<quint64> &values) {
    QJsonArray result;
    for (auto id : values) result.append(idString(id));
    return result;
}
QString csv(QString value) { value.replace('"', "\"\""); return '"' + value + '"'; }
ExportResult writeSnapshot(const TraceSnapshot &snapshot, const QString &path, const QString &format)
{
    ExportResult result;
    result.records = snapshot.records.size();
    for (const auto &source : snapshot.sources) if (source.availability != "available") ++result.missingSources;
    if (format != "jsonl" && format != "csv") { result.error = QStringLiteral("导出格式必须为 csv 或 jsonl"); return result; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { result.error = file.errorString(); return result; }
    bool good = true;
    auto json = [&file, &good](const QJsonObject &object) {
        const auto line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(line) != line.size()) good = false;
    };
    auto row = [&file, &good](const QStringList &fields) {
        QStringList escaped;
        for (const auto &field : fields) escaped.append(csv(field));
        const auto line = (escaped.join(',') + "\r\n").toUtf8();
        if (file.write(line) != line.size()) good = false;
    };
    if (format == "csv") row({"type", "id", "operationId", "utc", "state", "detail", "sourceRefs", "availability"});
    QJsonObject summary{{"type", "manifest"}, {"schemaVersion", 1}, {"snapshot", true},
        {"sessionId", snapshot.sessionId}, {"retention", snapshot.retention},
        {"sessionConfiguration", snapshot.sessionConfiguration},
        {"expiredRecords", idString(snapshot.expiredRecords)}, {"expiredMessages", idString(snapshot.expiredMessages)},
        {"expiredOperations", idString(snapshot.expiredOperations)}, {"omittedObservedRefs", idString(snapshot.omittedObservedRefs)},
        {"requestedOperationMissing", snapshot.requestedOperationMissing}, {"requestedMessageMissing", snapshot.requestedMessageMissing}};
    if (format == "jsonl") json(summary);
    else row({"manifest", {}, {}, {}, {}, QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact)), {}, {}});
    for (const auto &record : snapshot.records) {
        if (format == "jsonl") {
            json(tracecodec::encode(record));
        } else row({"trace", idString(record.id), idString(record.operationId), record.observedUtc.toString(Qt::ISODateWithMs),
                    record.kind, record.detail, {}, "available"});
    }
    for (const auto &message : snapshot.messages) {
        if (format == "jsonl") json(tracecodec::encode(message));
        else row({"message", idString(message.id), idString(message.operationId), message.observedUtc.toString(Qt::ISODateWithMs),
            message.status, message.detail, QString::fromUtf8(QJsonDocument(ids(message.sources)).toJson(QJsonDocument::Compact)), {}});
    }
    for (const auto &operation : snapshot.operations) {
        if (format == "jsonl") json(tracecodec::encode(operation));
        else row({"operation", idString(operation.request.id), idString(operation.request.id), {}, operation.update.state,
            operation.update.detail, QString::fromUtf8(QJsonDocument(ids(operation.observedRefs)).toJson(QJsonDocument::Compact)), {}});
    }
    for (const auto &source : snapshot.sources) {
        if (format == "jsonl") json({{"type", "source"}, {"recordId", idString(source.range.recordId)},
            {"offset", source.range.offset}, {"length", source.range.length}, {"availability", source.availability},
            {"bytesHex", QString::fromLatin1(source.bytes.toHex())}});
        else row({"source", idString(source.range.recordId), {}, {}, {},
            QStringLiteral("offset=%1 length=%2").arg(source.range.offset).arg(source.range.length), {}, source.availability});
    }
    if (!good || !file.commit()) { result.error = file.errorString(); return result; }
    result.ok = true;
    return result;
}
}
std::shared_future<ExportResult> exportSnapshot(std::function<TraceSnapshot()> makeSnapshot, QString path, QString format)
{
    static std::atomic<int> active{0};
    auto promise = std::make_shared<std::promise<ExportResult>>();
    auto future = promise->get_future().share();
    if (active.fetch_add(1) >= 4) {
        active.fetch_sub(1);
        promise->set_value({false, QStringLiteral("后台导出已达 4 项上限，请等待完成"), 0, 0});
        return future;
    }
    try {
        auto snapshot = makeSnapshot(); // 在调用者上下文捕获，预留槽位后才复制。
        std::thread([promise, snapshot = std::move(snapshot), path = std::move(path), format = std::move(format)] {
            try { promise->set_value(writeSnapshot(snapshot, path, format)); }
            catch (const std::exception &error) { promise->set_value({false, QString::fromUtf8(error.what()), 0, 0}); }
            active.fetch_sub(1);
        }).detach();
    } catch (const std::exception &error) {
        active.fetch_sub(1);
        promise->set_value({false, QString::fromUtf8(error.what()), 0, 0});
    }
    return future;
}
}
