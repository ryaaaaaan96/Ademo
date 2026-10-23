#include "debugger/recording.h"
#include "trace_codec.h"
#include "archive_format.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QThreadPool>
#include <QUuid>
#include <atomic>

namespace debugger {
namespace {
std::atomic<int> querySlots{0};
QThreadPool &readerPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured); return pool;
}
quint64 id(const QJsonValue &value) { return value.toString().toULongLong(); }
ArchiveResult readArchive(const QString &path, const QString &sessionId, const TraceFilter &filter)
{
    ArchiveResult result;
    QDir directory(path); QFile manifest(directory.filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 65536) { result.error = QStringLiteral("归档索引不可读或超出 64 KiB"); return result; }
    QJsonParseError parse; auto index = QJsonDocument::fromJson(manifest.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError || index.value("schemaVersion").toInt() != 1) {
        result.error = QStringLiteral("不支持或损坏的归档索引"); return result;
    }
    const auto validation = validateArchiveIndex(index);
    if (!validation.isEmpty()) { result.error = validation; return result; }
    const auto segments = index.value("segments").toArray();
    if (segments.size() > 64) { result.error = QStringLiteral("归档分段超过 64 项预算"); return result; }
    static const QRegularExpression segmentName("^segment-[0-9]{20}-[0-9a-f-]{36}\\.jsonl$");
    for (const auto &value : segments) {
        const auto segment = value.toObject();
        if (!segmentName.match(segment.value("name").toString()).hasMatch() || id(segment.value("size")) > 8 * 1024 * 1024) {
            result.error = QStringLiteral("归档分段路径或容量无效"); return result;
        }
    }
    bool unavailableSegments = false; bool corrupt = false; bool limitReached = false;
    QSet<QString> sessions;
    QMap<quint64, Message> messages;
    QMap<quint64, OperationSnapshot> operations;
    QMap<quint64, qint64> messageCosts, operationCosts, recordCosts;
    qint64 messageBytes = 0, operationBytes = 0, recordBytes = 0;
    QMap<quint64, QPair<QString, quint64>> operationPositions;
    quint64 queryOmissions = 0;
    QJsonObject sessionConfiguration;
    auto inTime = [&filter](const QDateTime &time) {
        return (!filter.fromUtc.isValid() || time >= filter.fromUtc) && (!filter.toUtc.isValid() || time <= filter.toUtc);
    };
    auto scan = [&](const std::function<void(const QJsonObject &, qint64)> &consume) {
        qint64 scanned = 0;
        for (const auto &value : segments) {
            const auto segment = value.toObject(); QFile file(directory.filePath(segment.value("name").toString()));
            if (!file.open(QIODevice::ReadOnly)) { unavailableSegments = true; continue; }
            const auto budget = qint64(id(segment.value("size"))); qint64 read = 0; quint64 lines = 0;
            if (segment.value("orphan").toBool()) corrupt = true;
            while (read < budget) {
                const auto line = file.readLine(qMin(qint64(131073), budget - read + 1));
                if (line.isEmpty()) { unavailableSegments = true; break; }
                read += line.size(); scanned += line.size();
                if (scanned > 512LL * 1024 * 1024) { limitReached = true; return; }
                if (!line.endsWith('\n') || line.size() > 131072) { corrupt = true; break; }
                ++lines;
                QJsonParseError error; const auto object = QJsonDocument::fromJson(line, &error).object();
                if (error.error != QJsonParseError::NoError || object.value("schemaVersion").toInt() != 1
                    || QUuid(object.value("sessionId").toString()).isNull()) { corrupt = true; continue; }
                consume(object, line.size());
            }
            if (!segment.value("orphan").toBool() && lines != id(segment.value("events"))) corrupt = true;
        }
    };
    scan([&](const QJsonObject &event, qint64 cost) {
        const auto owner = event.value("sessionId").toString(); sessions.insert(owner);
        if (owner != sessionId || sessionId.isEmpty()) return;
        const auto type = event.value("type").toString();
        if (type == "session") sessionConfiguration = event;
        else if (type == "message") {
            const auto message = tracecodec::decodeMessage(event);
            if (!message.id || !message.observedUtc.isValid()) { corrupt = true; return; }
            if (filter.messageId && message.id != filter.messageId) return;
            if (!inTime(message.observedUtc)) return;
            messageBytes -= messageCosts.value(message.id); messageCosts[message.id] = cost; messageBytes += cost;
            messages.insert(message.id, message);
            while (messages.size() > 8192 || messageBytes > 8 * 1024 * 1024) {
                messageBytes -= messageCosts.take(messages.firstKey()); messages.erase(messages.begin()); ++queryOmissions;
            }
        } else if (type == "operation") {
            const auto operation = tracecodec::decodeOperation(event);
            if (!operation.request.id) { corrupt = true; return; }
            if (filter.operationId && operation.request.id != filter.operationId) return;
            operations.insert(operation.request.id, operation);
            operationBytes -= operationCosts.value(operation.request.id); operationCosts[operation.request.id] = cost; operationBytes += cost;
            operationPositions[operation.request.id] = {event.value("recordingId").toString(), id(event.value("sequence"))};
            while (operations.size() > 1024 || operationBytes > 8 * 1024 * 1024) {
                operationBytes -= operationCosts.take(operations.firstKey());
                operationPositions.remove(operations.firstKey()); operations.erase(operations.begin()); ++queryOmissions;
            }
        }
    });
    result.sessions = sessions.values(); result.sessions.sort();
    TraceSnapshot all; all.sessionId = sessionId; all.sessionConfiguration = sessionConfiguration;
    QSet<quint64> refs;
    for (const auto &message : messages) for (auto source : message.sources) refs.insert(source);
    for (const auto &operation : operations) for (auto source : operation.observedRefs) refs.insert(source);
    QMap<quint64, TraceRecord> records;
    scan([&](const QJsonObject &event, qint64 cost) {
        if (event.value("sessionId").toString() != sessionId || event.value("type") != "trace") return;
        const auto record = tracecodec::decodeTrace(event);
        const auto encoded = event.value("bytesHex").toString();
        static const QRegularExpression hex("^(?:[0-9a-f]{2})*$");
        if (!record.id || !record.observedUtc.isValid() || record.bytes.size() > 4096 || !hex.match(encoded).hasMatch()) { corrupt = true; return; }
        bool observed = false;
        for (const auto &value : event.value("observedOperationIds").toArray()) {
            const auto operationId = id(value); auto operation = operations.find(operationId);
            if (operation == operations.end()) continue;
            const auto position = operationPositions.value(operationId);
            if (position.first == event.value("recordingId").toString() && id(event.value("sequence")) > position.second
                && !operation->observedRefs.contains(record.id)) {
                operation->observedRefs.append(record.id);
                if (operation->observedRefs.size() > 512) { operation->observedRefs.removeFirst(); ++operation->omittedObservedRefCount; }
            }
            if (filter.operationId == operationId) observed = true;
        }
        if (filter.operationId || filter.messageId) {
            if (!refs.contains(record.id) && !observed) return;
        } else if (!inTime(record.observedUtc) && !refs.contains(record.id)) return;
        records.insert(record.id, record);
        recordBytes -= recordCosts.value(record.id); recordCosts[record.id] = cost; recordBytes += cost;
        while (records.size() > 8192 || recordBytes > 32 * 1024 * 1024) {
            recordBytes -= recordCosts.take(records.firstKey()); records.erase(records.begin()); ++queryOmissions;
        }
    });
    all.records = records.values(); all.messages = messages.values(); all.operations = operations.values();
    result.snapshot = filterSnapshot(all, filter);
    for (auto &source : result.snapshot.sources) if (source.availability != "available" && source.availability != "invalid-range") {
        auto inRange = [&](const QJsonValue &value) {
            const auto range = value.toObject();
            return range.value("sessionId").toString() == sessionId && source.range.recordId >= id(range.value("firstId"))
                && source.range.recordId <= id(range.value("lastId")) && id(range.value("firstId")) != 0;
        };
        for (const auto &range : index.value("rotationRanges").toArray()) if (inRange(range)) source.availability = "expired";
        for (const auto &range : index.value("lossRanges").toArray()) if (inRange(range)) source.availability = "lost";
        if (index.value("lossRangesTruncated").toBool() && source.availability == "expired") source.availability = "unavailable";
    }
    auto retention = index;
    retention.remove("segments");
    retention["scope"] = "archive-directory";
    retention["queryOmittedEntries"] = QString::number(queryOmissions);
    retention["segmentsUnavailableDuringQuery"] = unavailableSegments;
    retention["corruptOrIncompleteSegment"] = corrupt;
    retention["scanLimitReached"] = limitReached;
    bool missingSources = false;
    for (const auto &source : result.snapshot.sources) if (source.availability != "available") missingSources = true;
    bool unfinished = false; for (const auto &operation : result.snapshot.operations) if (!operation.finished) unfinished = true;
    retention["unfinishedOperationSnapshots"] = unfinished;
    retention["complete"] = !unavailableSegments && !corrupt && !limitReached && !queryOmissions && !id(index.value("droppedEvents"))
        && !id(index.value("rotatedEvents")) && !result.snapshot.requestedOperationMissing && !result.snapshot.requestedMessageMissing;
    if (missingSources || unfinished || index.value("archiveGap").toBool() || !index.value("recordingError").toString().isEmpty()) retention["complete"] = false;
    result.snapshot.retention = retention;
    result.ok = true;
    if (!sessionId.isEmpty() && !sessions.contains(sessionId)) result.error = QStringLiteral("该会话当前没有可用归档分段；保留范围与丢失信息仍可导出");
    return result;
}
}
std::shared_future<ArchiveResult> queryArchive(QString directory, QString sessionId, TraceFilter filter)
{
    auto promise = std::make_shared<std::promise<ArchiveResult>>(); const auto future = promise->get_future().share();
    if (querySlots.fetch_add(1) >= 2) {
        --querySlots; promise->set_value({false, QStringLiteral("最多并发两个归档查询"), {}, {}}); return future;
    }
    readerPool().start([promise, directory = std::move(directory), sessionId = std::move(sessionId), filter] {
        try { promise->set_value(readArchive(directory, sessionId, filter)); }
        catch (const std::exception &error) { promise->set_value({false, QString::fromUtf8(error.what()), {}, {}}); }
        --querySlots;
    });
    return future;
}
}
