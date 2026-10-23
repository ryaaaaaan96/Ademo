#include "debugger/host.h"
#include "trace_codec.h"
#include "archive_format.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThreadPool>
#include <QUuid>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace debugger {
namespace {
constexpr qint64 manifestReserve = 131072;
constexpr int maxLineBytes = 131072;
std::atomic<int> recorderSlots{0};
QThreadPool &writerPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(4); return true; }();
    Q_UNUSED(configured); return pool;
}
QString utc(const QDateTime &value) { return value.toString(Qt::ISODateWithMs); }
QDateTime date(const QJsonValue &value) { return QDateTime::fromString(value.toString(), Qt::ISODateWithMs); }
quint64 number(const QJsonValue &value) { return value.toString().toULongLong(); }
void expand(QDateTime &first, QDateTime &last, const QDateTime &time) {
    if (!time.isValid()) return;
    if (!first.isValid() || time < first) first = time;
    if (!last.isValid() || time > last) last = time;
}
QJsonObject retentionJson(const RecordingStats &stats) {
    return {{"droppedEvents", QString::number(stats.droppedEvents)}, {"lossFromUtc", utc(stats.lossFromUtc)}, {"lossToUtc", utc(stats.lossToUtc)},
        {"rotatedEvents", QString::number(stats.rotatedEvents)}, {"rotationFromUtc", utc(stats.rotationFromUtc)},
        {"rotationToUtc", utc(stats.rotationToUtc)}};
}
bool safeSegment(const QString &name) {
    static const QRegularExpression expression("^segment-[0-9]{20}-[0-9a-f-]{36}\\.jsonl$");
    return expression.match(name).hasMatch();
}
}
struct DiskRecorder::State {
    RecordingOptions options;
    RecordingStats stats;
    std::function<void()> beforeBatch;
    mutable std::mutex mutex;
    std::condition_variable wake;
    struct Entry { QByteArray line; QJsonObject identity; };
    std::deque<Entry> queue;
    bool stopping = false;
    bool lossDirty = false;
    quint64 sequence = 0;
    QJsonArray losses; // 最多 64 个精确连续 trace ID 丢失区间。
    bool lossRangesTruncated = false;
    std::promise<RecordingStats> completion;
    std::shared_future<RecordingStats> future = completion.get_future().share();
    void lost(const QJsonObject &event) {
        std::lock_guard<std::mutex> guard(mutex);
        ++stats.droppedEvents;
        expand(stats.lossFromUtc, stats.lossToUtc, date(event.value("eventUtc")));
        if (event.value("type") == "trace") {
            const auto id = number(event.value("id"));
            bool merged = false;
            if (!losses.isEmpty()) {
                auto last = losses.last().toObject();
                if (last.value("sessionId") == event.value("sessionId") && number(last.value("lastId")) + 1 == id) {
                    last["lastId"] = QString::number(id); last["toUtc"] = event.value("eventUtc");
                    losses.replace(losses.size() - 1, last); merged = true;
                }
            }
            if (!merged) {
                if (losses.size() >= 64) { losses.removeFirst(); lossRangesTruncated = true; }
                losses.append(QJsonObject{{"sessionId", event.value("sessionId")}, {"firstId", QString::number(id)},
                    {"lastId", QString::number(id)}, {"fromUtc", event.value("eventUtc")}, {"toUtc", event.value("eventUtc")}});
            }
        }
        lossDirty = true; wake.notify_one();
    }
    void run();
};
QString DiskRecorder::validate(const RecordingOptions &options)
{
    if (options.directory.trimmed().isEmpty()) return QStringLiteral("请选择记录目录");
    if (options.queueItems < 1 || options.queueItems > 8192 || options.queueBytes < 1024 || options.queueBytes > 32 * 1024 * 1024)
        return QStringLiteral("写入队列为 1..8192 项、1024..33554432 字节");
    if (options.diskBytes < 262144 || options.diskBytes > 512LL * 1024 * 1024 || options.segmentBytes < 1024
        || options.segmentBytes > 8 * 1024 * 1024 || options.segmentBytes > (options.diskBytes - manifestReserve) / 2)
        return QStringLiteral("磁盘容量为 256 KiB..512 MiB，分段为 1 KiB..8 MiB 且不超过数据预算的一半");
    if (options.batchItems < 1 || options.batchItems > 256) return QStringLiteral("批量大小为 1..256 项");
    return {};
}
DiskRecorder::DiskRecorder(RecordingOptions options, QString sessionId, std::function<void()> beforeBatch)
    : state_(std::make_shared<State>())
{
    state_->options = std::move(options);
    state_->options.directory = QDir::cleanPath(QFileInfo(state_->options.directory).absoluteFilePath());
    state_->stats.sessionId = std::move(sessionId);
    state_->stats.recordingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state_->stats.state = "starting";
    state_->beforeBatch = std::move(beforeBatch);
    const auto validation = validate(state_->options);
    if (!validation.isEmpty()) {
        state_->stats.state = "error"; state_->stats.error = validation; state_->stopping = true;
        state_->completion.set_value(state_->stats); return;
    }
    if (recorderSlots.fetch_add(1) >= 4) {
        --recorderSlots; state_->stats.state = "error";
        state_->stats.error = QStringLiteral("最多同时运行 4 个磁盘记录器");
        state_->stopping = true; state_->completion.set_value(state_->stats); return;
    }
    const auto state = state_;
    writerPool().start([state] {
        try { state->run(); }
        catch (const std::exception &error) {
            std::lock_guard<std::mutex> guard(state->mutex);
            state->stats.state = "error"; state->stats.error = QString::fromUtf8(error.what()); state->stopping = true;
            state->completion.set_value(state->stats);
        }
        --recorderSlots;
    });
}
DiskRecorder::~DiskRecorder() { stop(); }
void DiskRecorder::append(QJsonObject event)
{
    event["sessionId"] = state_->stats.sessionId;
    event["recordingId"] = state_->stats.recordingId;
    event["eventUtc"] = utc(QDateTime::currentDateTimeUtc());
    event["schemaVersion"] = 1;
    {
        std::lock_guard<std::mutex> guard(state_->mutex);
        event["sequence"] = QString::number(++state_->sequence);
    }
    auto line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    const QJsonObject identity{{"type", event.value("type")}, {"id", event.value("id")},
        {"eventUtc", event.value("eventUtc")}, {"sessionId", event.value("sessionId")}};
    bool lost = false;
    {
        std::lock_guard<std::mutex> guard(state_->mutex);
        if (state_->stopping) return;
        lost = state_->stats.state == "error" || line.size() > maxLineBytes || line.size() > state_->options.segmentBytes
            || state_->queue.size() >= size_t(state_->options.queueItems)
            || state_->stats.queuedBytes + line.size() > state_->options.queueBytes;
        if (!lost) {
            state_->stats.queuedBytes += line.size();
            state_->queue.push_back({std::move(line), identity});
            state_->stats.queuedItems = int(state_->queue.size());
        }
    }
    if (lost) state_->lost(identity);
    state_->wake.notify_one();
}
RecordingStats DiskRecorder::stats() const { std::lock_guard<std::mutex> guard(state_->mutex); return state_->stats; }
std::shared_future<RecordingStats> DiskRecorder::stop()
{
    { std::lock_guard<std::mutex> guard(state_->mutex); state_->stopping = true;
      if (state_->stats.state != "error" && state_->stats.state != "stopped") state_->stats.state = "stopping"; }
    state_->wake.notify_one(); return state_->future;
}
void DiskRecorder::State::run()
{
    QDir directory(options.directory);
    QLockFile lock(directory.filePath("recording.lock"));
    QList<QJsonObject> segments;
    QJsonArray rotations;
    bool rotationRangesTruncated = false;
    bool archiveGap = false;
    quint64 nextSegment = 1;
    qint64 retained = 0;
    QFile current;
    int currentIndex = -1;
    auto fail = [this](const QString &error) {
        std::lock_guard<std::mutex> guard(mutex); stats.error = error; stats.state = "error";
    };
    auto manifest = [&] {
        RecordingStats captured; QJsonArray lossCopy; bool lossTruncated;
        { std::lock_guard<std::mutex> guard(mutex); captured = stats; lossCopy = losses; lossTruncated = lossRangesTruncated; lossDirty = false; }
        QJsonArray files; for (const auto &segment : segments) files.append(segment);
        QJsonObject object = retentionJson(captured);
        object["schemaVersion"] = 1; object["segments"] = files; object["lossRanges"] = lossCopy;
        object["lossRangesTruncated"] = lossTruncated; object["rotationRanges"] = rotations;
        object["rotationRangesTruncated"] = rotationRangesTruncated;
        object["archiveGap"] = archiveGap;
        object["recordingError"] = captured.error;
        object["diskBytes"] = QString::number(options.diskBytes); object["segmentBytes"] = QString::number(options.segmentBytes);
        object["queueItems"] = options.queueItems; object["queueBytes"] = QString::number(options.queueBytes); object["batchItems"] = options.batchItems;
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (bytes.size() > manifestReserve / 2 - 1024) { fail(QStringLiteral("归档索引超过保留预算")); return false; }
        QSaveFile file(directory.filePath("manifest.json"));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            fail(QStringLiteral("写入归档索引失败：") + file.errorString()); return false;
        }
        { std::lock_guard<std::mutex> guard(mutex); stats.retainedBytes = retained + bytes.size(); }
        return true;
    };
    auto rotateOldest = [&] {
        if (segments.isEmpty()) return false;
        const auto segment = segments.first();
        if (!QFile::remove(directory.filePath(segment.value("name").toString()))) {
            fail(QStringLiteral("无法轮换归档分段")); return false;
        }
        retained -= qint64(number(segment.value("size")));
        segments.removeFirst(); --currentIndex;
        {
            std::lock_guard<std::mutex> guard(mutex);
            stats.rotatedEvents += number(segment.value("events"));
            expand(stats.rotationFromUtc, stats.rotationToUtc, date(segment.value("firstUtc")));
            expand(stats.rotationFromUtc, stats.rotationToUtc, date(segment.value("lastUtc")));
        }
        if (!segment.value("orphan").toBool()) {
            if (rotations.size() >= 32) { rotations.removeFirst(); rotationRangesTruncated = true; }
            rotations.append(QJsonObject{{"sessionId", segment.value("sessionId")}, {"firstId", segment.value("firstTraceId")},
                {"lastId", segment.value("lastTraceId")}, {"fromUtc", segment.value("firstUtc")}, {"toUtc", segment.value("lastUtc")}});
        } else archiveGap = true;
        return true;
    };
    bool healthy = true;
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) { fail(QStringLiteral("无法创建记录目录")); healthy = false; }
    if (healthy && !lock.tryLock(0)) { fail(QStringLiteral("记录目录被其他记录器占用或不可写")); healthy = false; }
    if (healthy) {
        QFile old(directory.filePath("manifest.json"));
        if (old.exists()) {
            if (!old.open(QIODevice::ReadOnly) || old.size() > manifestReserve / 2) { fail(QStringLiteral("归档索引不可读或超过预算")); healthy = false; }
            else {
                QJsonParseError error; const auto object = QJsonDocument::fromJson(old.readAll(), &error).object();
                if (error.error != QJsonParseError::NoError || !validateArchiveIndex(object).isEmpty()) {
                    fail(QStringLiteral("归档索引无效，保留原文件")); healthy = false;
                } else {
                    {
                    std::lock_guard<std::mutex> guard(mutex);
                    stats.droppedEvents += number(object.value("droppedEvents"));
                    stats.rotatedEvents = number(object.value("rotatedEvents"));
                    expand(stats.lossFromUtc, stats.lossToUtc, date(object.value("lossFromUtc")));
                    expand(stats.lossFromUtc, stats.lossToUtc, date(object.value("lossToUtc")));
                    stats.rotationFromUtc = date(object.value("rotationFromUtc")); stats.rotationToUtc = date(object.value("rotationToUtc"));
                    auto oldLosses = object.value("lossRanges").toArray();
                    for (const auto &loss : losses) oldLosses.append(loss);
                    while (oldLosses.size() > 64) { oldLosses.removeFirst(); lossRangesTruncated = true; }
                    losses = oldLosses; lossRangesTruncated = lossRangesTruncated || object.value("lossRangesTruncated").toBool();
                    rotations = object.value("rotationRanges").toArray(); rotationRangesTruncated = object.value("rotationRangesTruncated").toBool();
                    archiveGap = object.value("archiveGap").toBool() || !object.value("recordingError").toString().isEmpty();
                    }
                    for (const auto &item : object.value("segments").toArray()) {
                        auto segment = item.toObject(); const auto name = segment.value("name").toString();
                        if (!safeSegment(name)) { fail(QStringLiteral("归档分段路径无效")); healthy = false; break; }
                        const QFileInfo info(directory.filePath(name));
                        if (!info.exists()) { archiveGap = true; continue; }
                        if (quint64(info.size()) != number(segment.value("size"))) archiveGap = true;
                        segment["size"] = QString::number(info.size()); segments.append(segment); retained += info.size();
                        nextSegment = qMax(nextSegment, name.mid(8, 20).toULongLong() + 1);
                    }
                }
            }
        }
    }
    if (healthy) {
        // 崩溃可能留下未进入索引的分段；保守纳入预算，不隐式当作完整历史。
        QSet<QString> known; for (const auto &segment : segments) known.insert(segment.value("name").toString());
        for (const auto &info : directory.entryInfoList({"segment-*.jsonl"}, QDir::Files, QDir::Name)) {
            if (!safeSegment(info.fileName()) || known.contains(info.fileName())) continue;
            segments.append({{"name", info.fileName()}, {"size", QString::number(info.size())}, {"events", "0"}, {"orphan", true}});
            archiveGap = true;
            retained += info.size(); nextSegment = qMax(nextSegment, info.fileName().mid(8, 20).toULongLong() + 1);
        }
        std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) { return a.value("name").toString() < b.value("name").toString(); });
        while ((retained > options.diskBytes - manifestReserve || segments.size() > 64) && healthy) healthy = rotateOldest();
        if (healthy) { std::lock_guard<std::mutex> guard(mutex); if (!stopping) stats.state = "running"; }
    }
    const bool initialized = healthy;
    while (true) {
        std::deque<Entry> batch;
        bool stop = false;
        {
            std::unique_lock<std::mutex> guard(mutex);
            wake.wait_for(guard, std::chrono::milliseconds(100), [this] { return stopping || !queue.empty() || lossDirty; });
            for (int i = 0; i < options.batchItems && !queue.empty(); ++i) {
                stats.queuedBytes -= queue.front().line.size(); batch.push_back(std::move(queue.front())); queue.pop_front();
            }
            stats.queuedItems = int(queue.size()); stop = stopping && queue.empty();
        }
        if (!batch.empty() && beforeBatch) beforeBatch(); // 注入慢存储的测试 seam，不持有队列锁。
        for (const auto &entry : batch) {
            if (!healthy) { lost(entry.identity); continue; }
            if (!current.isOpen() || current.size() + entry.line.size() > options.segmentBytes) {
                if (current.isOpen()) {
                    if (!current.flush()) { fail(current.errorString()); healthy = false; lost(entry.identity); continue; }
                    current.close();
                }
                const auto name = QStringLiteral("segment-%1-%2.jsonl").arg(nextSegment++, 20, 10, QChar('0')).arg(stats.recordingId);
                current.setFileName(directory.filePath(name));
                if (!current.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { fail(current.errorString()); healthy = false; lost(entry.identity); continue; }
                segments.append({{"name", name}, {"size", "0"}, {"events", "0"}, {"sessionId", stats.sessionId}});
                currentIndex = int(segments.size() - 1);
            }
            while ((retained + entry.line.size() > options.diskBytes - manifestReserve || segments.size() > 64) && healthy)
                healthy = currentIndex > 0 && rotateOldest();
            if (!healthy) { lost(entry.identity); continue; }
            if (current.write(entry.line) != entry.line.size()) { fail(current.errorString()); healthy = false; lost(entry.identity); continue; }
            retained += entry.line.size();
            auto &segment = segments[currentIndex]; segment["size"] = QString::number(current.size());
            segment["events"] = QString::number(number(segment.value("events")) + 1);
            if (!segment.contains("firstUtc")) segment["firstUtc"] = entry.identity.value("eventUtc");
            segment["lastUtc"] = entry.identity.value("eventUtc");
            if (entry.identity.value("type") == "trace") {
                if (!segment.contains("firstTraceId")) segment["firstTraceId"] = entry.identity.value("id");
                segment["lastTraceId"] = entry.identity.value("id");
            }
            { std::lock_guard<std::mutex> guard(mutex); ++stats.writtenEvents; }
        }
        if (healthy && current.isOpen() && !current.flush()) { fail(current.errorString()); healthy = false; }
        if (healthy) healthy = manifest();
        if (!healthy && initialized && lock.isLocked()) manifest(); // 尽力持久化失败标记；失败时界面仍保留错误。
        if (stop) break;
    }
    if (current.isOpen()) current.close();
    if (lock.isLocked()) lock.unlock();
    RecordingStats final;
    { std::lock_guard<std::mutex> guard(mutex); if (stats.state != "error") stats.state = "stopped"; final = stats; }
    completion.set_value(final);
}

bool DebugSession::startRecording(RecordingOptions options, QString *error, std::function<void()> beforeBatch)
{
    const auto validation = DiskRecorder::validate(options);
    if (!validation.isEmpty()) { if (error) *error = validation; return false; }
    if (recorder_ && recorder_->stats().state != "stopped" && recorder_->stats().state != "error") {
        if (error) *error = QStringLiteral("请先停止当前磁盘记录并等待排空"); return false;
    }
    if (recorder_ && recorder_->stop().wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (error) *error = QStringLiteral("上一记录器仍在清理文件，请稍后重试"); return false;
    }
    recorder_ = std::make_unique<DiskRecorder>(std::move(options), sessionId_, std::move(beforeBatch));
    recorder_->append({{"type", "session"}, {"protocolId", config_.protocolId}, {"transportId", config_.transportId},
        {"role", config_.role}, {"endpoint", config_.endpoint}, {"protocol", config_.protocol}});
    for (const auto &operation : operations_) recordOperation(operation);
    emit changed(); return true;
}
RecordingStats DebugSession::recordingStats() const { return recorder_ ? recorder_->stats() : RecordingStats{}; }
std::shared_future<RecordingStats> DebugSession::stopRecording()
{
    if (recorder_) return recorder_->stop();
    std::promise<RecordingStats> promise; promise.set_value({}); return promise.get_future().share();
}
void DebugSession::recordTrace(const TraceRecord &record)
{
    if (!recorder_) return;
    auto object = tracecodec::encode(record); QJsonArray observed;
    for (const auto &operation : operations_) if (!operation.observedRefs.isEmpty() && operation.observedRefs.last() == record.id)
        observed.append(QString::number(operation.request.id));
    object["observedOperationIds"] = observed; recorder_->append(std::move(object));
}
void DebugSession::recordOperation(const OperationSnapshot &operation)
{
    if (recorder_) recorder_->append(tracecodec::encode(operation));
}
}
