#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <cstdio>
using namespace debugger;
namespace {
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    std::function<void(quint64, const QByteArray &)> written;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override {
            wire_.writes.append({id, bytes}); if (wire_.written) wire_.written(id, bytes); return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) { this->events = std::move(events); return std::make_unique<Port>(*this); };
    }
    void receive(const QByteArray &bytes) { events.received(bytes); QCoreApplication::processEvents(); }
};
SessionConfig config(const QString &protocol = "trace.fixture") {
    return {protocol, "serial", "master", {{"port", "COM9999"}, {"baud", "115200"}, {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
}
std::shared_ptr<PluginCatalog> catalog(bool production = false) {
    auto result = std::make_shared<PluginCatalog>();
    result->discover(QCoreApplication::applicationDirPath() + (production ? "/plugins" : "/trace-fixtures")); return result;
}
RecordingOptions options(const QString &directory) { RecordingOptions result; result.directory = directory; return result; }
QByteArray fileBytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
template <class T> bool ready(const std::shared_future<T> &future) {
    return future.valid() && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}
int archiveChild(const QString &directory, bool printIdentity = false) {
    Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
    if (!session.start(config())) return 1; QCoreApplication::processEvents();
    if (!session.startRecording(options(directory))) return 2;
    session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
    auto future = session.stopRecording(); QElapsedTimer timer; timer.start();
    while (!ready(future) && timer.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    if (!ready(future) || future.get().state != "stopped" || future.get().droppedEvents) return 3;
    if (printIdentity) std::printf("%s", session.sessionId().toUtf8().constData()); return 0;
}
}
class RecordingTests : public QObject {
    Q_OBJECT
private slots:
    void crossProcessRestartKeepsIdentitiesAndRelations() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); QStringList owners;
        for (int i = 0; i < 2; ++i) {
            QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--archive-child", directory.path()});
            QVERIFY(child.waitForStarted()); QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 15000);
            QCOMPARE(child.exitCode(), 0); owners.append(QString::fromUtf8(child.readAllStandardOutput()).trimmed());
        }
        QVERIFY(!owners[0].isEmpty()); QVERIFY(owners[0] != owners[1]);
        auto listing = queryArchive(directory.path(), {}); QTRY_VERIFY(ready(listing)); QVERIFY(listing.get().ok);
        QCOMPARE(listing.get().sessions.size(), 2);
        for (const auto &owner : owners) {
            auto query = queryArchive(directory.path(), owner, {1}); QTRY_VERIFY(ready(query));
            const auto result = query.get(); QVERIFY(result.ok); QCOMPARE(result.snapshot.sessionId, owner);
            QCOMPARE(result.snapshot.messages.size(), 3); QCOMPARE(result.snapshot.operations.size(), 1);
            QCOMPARE(result.snapshot.operations.first().request.id, quint64(1));
            QCOMPARE(result.snapshot.operations.first().update.state, "success");
            QCOMPARE(result.snapshot.messages.last().sourceRanges.size(), 2);
            QVERIFY(result.snapshot.retention.value("complete").toBool());
            auto exported = exportSnapshot([result] { return result.snapshot; }, directory.filePath(owner + ".jsonl"), "jsonl");
            QTRY_VERIFY(ready(exported)); QVERIFY(exported.get().ok);
            const auto data = fileBytes(directory.filePath(owner + ".jsonl"));
            QVERIFY(data.contains(owner.toUtf8())); QVERIFY(data.contains("616263646566")); QVERIFY(data.contains("\"sourceRanges\""));
        }
    }
    void fullQueueKeepsIndependentLossAndLateSources() {
        QTemporaryDir directory; Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        auto selected = options(directory.path()); selected.queueItems = 16; selected.queueBytes = 16384; selected.batchItems = 1;
        auto pause = std::make_shared<QSemaphore>(); auto entered = std::make_shared<QSemaphore>();
        auto once = std::make_shared<std::atomic<bool>>(true);
        QVERIFY(session.startRecording(selected, nullptr, [pause, entered, once] {
            if (once->exchange(false)) { entered->release(); pause->acquire(); }
        }));
        QTRY_VERIFY(entered->available() == 1);
        const auto op = session.submit("two-attempts", {});
        for (int i = 0; i < 1000; ++i) wire.receive("noise");
        wire.receive("abcdef");
        const auto blocked = session.recordingStats();
        QVERIFY(blocked.droppedEvents > 900); QVERIFY(blocked.lossFromUtc.isValid()); QVERIFY(blocked.lossToUtc.isValid());
        QVERIFY(blocked.queuedItems <= selected.queueItems); QVERIFY(blocked.queuedBytes <= selected.queueBytes);
        QVERIFY(session.configureTrace({16, 16, 4})); QCOMPARE(session.traces().size(), 16);
        pause->release(); QTRY_COMPARE(session.recordingStats().queuedItems, 0);
        wire.receive("gh"); QCOMPARE(session.operation(op).update.state, "success");
        auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped)); QCOMPARE(stopped.get().state, "stopped");
        auto query = queryArchive(directory.path(), session.sessionId(), {op}); QTRY_VERIFY(ready(query));
        const auto result = query.get(); QVERIFY(result.ok);
        QVERIFY(result.snapshot.retention.value("droppedEvents").toString().toULongLong() > 900);
        bool missing = false; for (const auto &source : result.snapshot.sources) if (source.availability == "lost") missing = true;
        QVERIFY(missing); QVERIFY(!result.snapshot.retention.value("complete").toBool());
        auto exported = exportSnapshot([result] { return result.snapshot; }, directory.filePath("loss.jsonl"), "jsonl");
        QTRY_VERIFY(ready(exported)); QVERIFY(exported.get().ok);
        QVERIFY(fileBytes(directory.filePath("loss.jsonl")).contains("\"availability\":\"lost\""));
    }
    void rotationBoundsDiskAndMarksExpiredSources() {
        QTemporaryDir directory; Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        auto selected = options(directory.path()); selected.diskBytes = 262144; selected.segmentBytes = 8192; selected.queueItems = 8192;
        QVERIFY(session.startRecording(selected)); const auto op = session.submit("two-attempts", {}); wire.receive("abcdef");
        for (int i = 0; i < 1200; ++i) wire.receive(QByteArray(100, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(session.recordingStats().queuedItems, 0, 15000); wire.receive("gh");
        auto stopped = session.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(ready(stopped), 15000);
        QCOMPARE(stopped.get().state, "stopped"); QCOMPARE(stopped.get().droppedEvents, quint64(0)); QVERIFY(stopped.get().rotatedEvents > 0);
        qint64 bytes = 0; for (const auto &file : QDir(directory.path()).entryInfoList(QDir::Files)) bytes += file.size();
        QVERIFY(bytes <= selected.diskBytes);
        auto query = queryArchive(directory.path(), session.sessionId(), {op}); QTRY_VERIFY(ready(query)); const auto result = query.get();
        QVERIFY(result.ok); QVERIFY(result.snapshot.retention.value("rotatedEvents").toString().toULongLong() > 0);
        QVERIFY(result.snapshot.retention.value("rotationFromUtc").toString().size());
        bool expired = false; for (const auto &source : result.snapshot.sources) if (source.availability == "expired") expired = true;
        QVERIFY(expired); QVERIFY(!result.snapshot.retention.value("complete").toBool());
    }
    void byteQuotaRejectsWithoutBlockingProtocolInput() {
        QTemporaryDir directory; Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        auto selected = options(directory.path()); selected.queueItems = 8192; selected.queueBytes = 1024;
        auto gate = std::make_shared<QSemaphore>(); auto entered = std::make_shared<QSemaphore>(); auto once = std::make_shared<std::atomic<bool>>(true);
        QVERIFY(session.startRecording(selected, nullptr, [gate, entered, once] { if (once->exchange(false)) { entered->release(); gate->acquire(); } }));
        QTRY_VERIFY(entered->available() == 1); wire.receive(QByteArray(4096, 'x'));
        const auto stats = session.recordingStats(); QVERIFY(stats.droppedEvents >= 1); QVERIFY(stats.queuedBytes <= 1024);
        QCOMPARE(session.traces().last().bytes.size(), 4096); QCOMPARE(session.state(), SessionState::Running);
        gate->release(); auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped)); QCOMPARE(stopped.get().state, "stopped");
    }
    void pollingAndExportContinueWithBlockedDisk() {
        QTemporaryDir directory; Wire wire; qint64 time = 0;
        DebugSession session(catalog(true), nullptr, wire.factory(), [&] { return time; });
        auto selectedConfig = config("modbus.rtu");
        selectedConfig.protocol = {{"extraReadRetries", 0}, {"polls", QJsonArray{QJsonObject{{"unit", 1}, {"action", "read-holding"},
            {"start", 0}, {"count", 1}, {"intervalMs", 10}}}}};
        wire.written = [&](quint64 id, const QByteArray &) {
            wire.events.sent(id, true, {}); wire.events.received(QByteArray::fromHex("010302000a3843"));
        };
        QVERIFY(session.start(selectedConfig)); QCoreApplication::processEvents(); QVERIFY(session.configureTrace({32, 32, 16}));
        auto settings = options(directory.path()); settings.queueItems = 8; settings.queueBytes = 8192;
        auto gate = std::make_shared<QSemaphore>(); auto entered = std::make_shared<QSemaphore>(); auto once = std::make_shared<std::atomic<bool>>(true);
        QVERIFY(session.startRecording(settings, nullptr, [gate, entered, once] { if (once->exchange(false)) { entered->release(); gate->acquire(); } }));
        QTRY_VERIFY(entered->available() == 1);
        QElapsedTimer elapsed; elapsed.start();
        for (int i = 0; i < 1000; ++i) { time += 5000; session.poll(); QCoreApplication::processEvents(); }
        const auto duration = elapsed.elapsed(); const auto writes = wire.writes.size();
        QVERIFY(writes >= 100); QCOMPARE(session.state(), SessionState::Running);
        QVERIFY(session.traces().size() <= 32); QVERIFY(session.messages().size() <= 32);
        QVERIFY(session.recordingStats().droppedEvents > 0); QVERIFY(session.recordingStats().queuedItems <= 8);
        auto exported = session.exportTrace({}, directory.filePath("live.jsonl"), "jsonl");
        for (int i = 0; i < 20; ++i) { time += 2000; session.poll(); QCoreApplication::processEvents(); }
        QVERIFY(wire.writes.size() > writes); QTRY_VERIFY(ready(exported)); QVERIFY(exported.get().ok);
        gate->release(); auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped));
        qInfo() << "controlled-poll" << writes << "sends in" << duration << "ms; dropped" << stopped.get().droppedEvents;
    }
    void metadataPersistedAfterCompletionAndStop() {
        QTemporaryDir directory; Wire wire; qint64 time = 0;
        DebugSession session(catalog(true), nullptr, wire.factory(), [&] { return time; });
        QVERIFY(session.start(config("modbus.rtu"))); QCoreApplication::processEvents(); QVERIFY(session.startRecording(options(directory.path())));
        const auto op = session.submit("read-holding", {{"unit", 1}, {"start", 0}, {"count", 1}});
        time = 2000; session.poll(); wire.events.sent(op, true, {}); QCoreApplication::processEvents();
        ReceiveChunk chunk(QByteArray::fromHex("010302000a3843"), 0); chunk.metadata = {{"actualAdapterFlag", 7}};
        wire.events.received(chunk); QCoreApplication::processEvents(); time += 2000; session.poll();
        QCOMPARE(session.operation(op).update.state, "success"); session.stop();
        auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped)); auto query = queryArchive(directory.path(), session.sessionId(), {op});
        QTRY_VERIFY(ready(query)); const auto result = query.get(); QVERIFY(result.ok);
        bool metadata = false; bool attempt = false;
        for (const auto &record : result.snapshot.records) {
            if (record.kind == "rx") { QCOMPARE(record.idleBeforeUs, std::optional<qint64>(0)); metadata = record.metadata.value("actualAdapterFlag").toInt() == 7; }
            if (record.kind == "send-request") attempt = record.metadata.value("attempt").toInt() == 1;
        }
        QVERIFY(metadata); QVERIFY(attempt); QCOMPARE(result.snapshot.operations.first().update.state, "success");
    }
    void lockedOrInvalidDirectoryOnlyDisablesRecording() {
        QTemporaryDir directory; Wire firstWire, secondWire;
        DebugSession first(catalog(), nullptr, firstWire.factory()), second(catalog(), nullptr, secondWire.factory());
        auto bad = options(directory.path()); bad.queueItems = 0; QString error; QVERIFY(!first.startRecording(bad, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(first.startRecording(options(directory.path()))); QTRY_COMPARE(first.recordingStats().state, "running");
        QVERIFY(second.startRecording(options(directory.path()))); QTRY_COMPARE(second.recordingStats().state, "error");
        QVERIFY(second.start(config())); QCoreApplication::processEvents(); const auto op = second.submit("two-attempts", {});
        secondWire.receive("abcdef"); secondWire.receive("gh"); QCOMPARE(second.operation(op).update.state, "success");
        auto one = first.stopRecording(), two = second.stopRecording(); QTRY_VERIFY(ready(one)); QTRY_VERIFY(ready(two));
        auto broken = queryArchive(directory.filePath("absent"), "unknown"); QTRY_VERIFY(ready(broken)); QVERIFY(!broken.get().ok);
    }
    void initializationFailuresReleaseBackgroundCapacity() {
        QTemporaryDir directory; auto plugins = catalog(); DebugSession owner(plugins);
        QVERIFY(owner.startRecording(options(directory.path()))); QTRY_COMPARE(owner.recordingStats().state, "running");
        std::vector<std::unique_ptr<DebugSession>> failed;
        for (int i = 0; i < 12; ++i) {
            auto session = std::make_unique<DebugSession>(plugins);
            QVERIFY(session->startRecording(options(directory.path())));
            QTRY_COMPARE(session->recordingStats().state, "error");
            QVERIFY(session->recordingStats().error.contains(QStringLiteral("占用")));
            failed.push_back(std::move(session));
            QTest::qWait(10); // 失败对象继续存活，不调用 stop，后台容量应自行释放。
        }
        DebugSession other(plugins); QVERIFY(other.startRecording(options(directory.filePath("other"))));
        QTRY_COMPARE(other.recordingStats().state, "running");
        auto first = owner.stopRecording(), second = other.stopRecording(); QTRY_VERIFY(ready(first)); QTRY_VERIFY(ready(second));
    }
    void queuedOperationsKeepSameLiveAndArchivedMessages() {
        QTemporaryDir directory; Wire wire; qint64 time = 0;
        DebugSession session(catalog(true), nullptr, wire.factory(), [&] { return time; });
        auto selected = config("modbus.rtu"); selected.protocol = {{"extraReadRetries", 0}};
        QVERIFY(session.start(selected)); QCoreApplication::processEvents(); QVERIFY(session.startRecording(options(directory.path())));
        const auto first = session.submit("read-holding", {{"unit", 1}, {"start", 0}, {"count", 1}});
        const auto second = session.submit("read-holding", {{"unit", 2}, {"start", 0}, {"count", 1}});
        time += 1750; session.poll(); wire.events.sent(wire.writes.last().first, true, {}); QCoreApplication::processEvents();
        wire.receive(QByteArray::fromHex("010302000a3843")); time += 1750; session.poll(); QCoreApplication::processEvents();
        QCOMPARE(session.operation(first).update.state, "success");
        const auto observed = session.queryTrace({second}); QVERIFY(!observed.messages.isEmpty());
        QCOMPARE(observed.messages.first().operationId, first);
        session.stop(); const auto live = session.queryTrace({second}); // 同一截止点，包含停止产生的取消消息。
        auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped));
        auto query = queryArchive(directory.path(), session.sessionId(), {second}); QTRY_VERIFY(ready(query)); QVERIFY(query.get().ok);
        const auto archived = query.get().snapshot; QCOMPARE(archived.messages.size(), live.messages.size());
        for (qsizetype i = 0; i < live.messages.size(); ++i) {
            QCOMPARE(archived.messages[i].id, live.messages[i].id);
            QCOMPARE(archived.messages[i].operationId, live.messages[i].operationId);
        }
    }
    void disconnectedWriteAndRecoveryPolicySurviveArchive() {
        QTemporaryDir directory; Wire wire; qint64 time = 0;
        DebugSession session(catalog(true), nullptr, wire.factory(), [&] { return time; });
        QVERIFY(session.startRecording(options(directory.path()))); // 先记录，再启动，配置不得一直为空。
        auto selected = config("modbus.rtu"); selected.protocol = {{"extraReadRetries", 0}};
        selected.recovery = {{"autoReconnect", true}, {"restorePolling", false}, {"intervalMs", 10}};
        QVERIFY(session.start(selected)); QCoreApplication::processEvents();
        const auto op = session.submit("write-single-register", {{"unit", 1}, {"start", 0}, {"value", 7}});
        time += 1750; session.poll(); QCOMPARE(wire.writes.size(), 1);
        wire.events.sent(wire.writes.last().first, true, {}); QCoreApplication::processEvents();
        wire.events.failed(QStringLiteral("受控断线：未确认写入")); QCoreApplication::processEvents();
        QCOMPARE(session.operation(op).update.state, "transport-error"); QVERIFY(session.operation(op).update.result.value("uncertain").toBool());
        time += 10000; session.poll(); QCoreApplication::processEvents(); QCOMPARE(session.state(), SessionState::Running);
        QCOMPARE(wire.writes.size(), 1); // 重连不补发未确认写入。
        const QJsonObject policy{{"autoReconnect", true}, {"restorePolling", true}, {"intervalMs", 20}};
        QVERIFY(session.configureRecovery(policy)); session.stop(); auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped));
        auto query = queryArchive(directory.path(), session.sessionId(), {op}); QTRY_VERIFY(ready(query)); const auto result = query.get(); QVERIFY(result.ok);
        QCOMPARE(result.snapshot.operations.size(), 1); QCOMPARE(result.snapshot.operations.first().update.state, "transport-error");
        QVERIFY(result.snapshot.operations.first().update.result.value("uncertain").toBool());
        QCOMPARE(result.snapshot.sessionConfiguration.value("recovery").toObject(), policy);
        QCOMPARE(result.snapshot.sessionConfiguration.value("protocolId").toString(), "modbus.rtu");
        auto exported = exportSnapshot([result] { return result.snapshot; }, directory.filePath("recovery.jsonl"), "jsonl");
        QTRY_VERIFY(ready(exported)); QVERIFY(exported.get().ok); const auto bytes = fileBytes(directory.filePath("recovery.jsonl"));
        QVERIFY(bytes.contains("\"state\":\"transport-error\"")); QVERIFY(bytes.contains("\"uncertain\":true"));
        QVERIFY(bytes.contains("\"recovery\":{\"autoReconnect\":true,\"intervalMs\":20,\"restorePolling\":true}"));
    }
    void deletedAndCorruptSegmentsAreExplicit() {
        QTemporaryDir directory; QCOMPARE(archiveChild(directory.path()), 0);
        const auto index = QJsonDocument::fromJson(fileBytes(directory.filePath("manifest.json"))).object();
        const auto segment = index.value("segments").toArray().first().toObject().value("name").toString();
        auto listing = queryArchive(directory.path(), {}); QTRY_VERIFY(ready(listing)); const auto owner = listing.get().sessions.first();
        QVERIFY(QFile::remove(directory.filePath(segment)));
        auto query = queryArchive(directory.path(), owner); QTRY_VERIFY(ready(query)); QVERIFY(query.get().ok);
        QVERIFY(query.get().snapshot.retention.value("segmentsUnavailableDuringQuery").toBool());
        QFile corrupted(directory.filePath(segment)); QVERIFY(corrupted.open(QIODevice::WriteOnly)); corrupted.write("not-json\n"); corrupted.close();
        auto corrupt = queryArchive(directory.path(), owner); QTRY_VERIFY(ready(corrupt)); QVERIFY(corrupt.get().ok);
        QVERIFY(corrupt.get().snapshot.retention.value("corruptOrIncompleteSegment").toBool());
    }
    void invalidIndexNumbersNeverLookComplete_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
        QTest::newRow("segment-garbage") << QString("size") << QJsonValue("garbage");
        QTest::newRow("segment-negative") << QString("size") << QJsonValue("-1");
        QTest::newRow("segment-missing") << QString("size") << QJsonValue(QJsonValue::Null);
        QTest::newRow("loss-garbage") << QString("droppedEvents") << QJsonValue("garbage");
        QTest::newRow("loss-negative") << QString("droppedEvents") << QJsonValue("-1");
        QTest::newRow("rotation-overflow") << QString("rotatedEvents") << QJsonValue("18446744073709551616");
        QTest::newRow("rotation-number-type") << QString("rotatedEvents") << QJsonValue(1);
    }
    void invalidIndexNumbersNeverLookComplete() {
        QFETCH(QString, field); QFETCH(QJsonValue, value);
        QTemporaryDir directory; QCOMPARE(archiveChild(directory.path()), 0);
        auto index = QJsonDocument::fromJson(fileBytes(directory.filePath("manifest.json"))).object();
        if (field == "size") {
            auto segments = index.value("segments").toArray(); auto segment = segments[0].toObject(); segment[field] = value;
            segments.replace(0, segment); index["segments"] = segments;
        } else index[field] = value;
        const auto corruptedBytes = QJsonDocument(index).toJson(QJsonDocument::Compact);
        QFile file(directory.filePath("manifest.json")); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(corruptedBytes), corruptedBytes.size()); file.close();
        auto query = queryArchive(directory.path(), {}); QTRY_VERIFY(ready(query)); QVERIFY(!query.get().ok); QVERIFY(!query.get().error.isEmpty());
        Wire wire; DebugSession session(catalog(), nullptr, wire.factory()); QVERIFY(session.startRecording(options(directory.path())));
        QTRY_COMPARE(session.recordingStats().state, "error"); auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped));
        QCOMPARE(fileBytes(directory.filePath("manifest.json")), corruptedBytes);
    }
    void orphanRotationKeepsIndexReadable() {
        QTemporaryDir directory; QCOMPARE(archiveChild(directory.path()), 0);
        const auto orphanName = QStringLiteral("segment-00000000000000000999-11111111-1111-1111-1111-111111111111.jsonl");
        QFile orphan(directory.filePath(orphanName)); QVERIFY(orphan.open(QIODevice::WriteOnly));
        QCOMPARE(orphan.write(QByteArray(200000, 'x')), qint64(200000)); orphan.close();
        QFile sentinel(directory.filePath("keep.txt")); QVERIFY(sentinel.open(QIODevice::WriteOnly)); sentinel.write("keep"); sentinel.close();
        Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        auto selected = options(directory.path()); selected.diskBytes = 262144; selected.segmentBytes = 8192;
        QVERIFY(session.startRecording(selected)); QTRY_COMPARE(session.recordingStats().state, "running");
        auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped)); QCOMPARE(stopped.get().state, "stopped");
        QVERIFY(!QFile::exists(directory.filePath(orphanName))); QCOMPARE(fileBytes(directory.filePath("keep.txt")), "keep");
        auto query = queryArchive(directory.path(), session.sessionId()); QTRY_VERIFY(ready(query)); QVERIFY(query.get().ok);
        QVERIFY(query.get().snapshot.retention.value("archiveGap").toBool());
        QVERIFY(!query.get().snapshot.retention.value("complete").toBool());
    }
    void localOnlyFlagSurvivesArchive() {
        QTemporaryDir directory; Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents(); QVERIFY(session.startRecording(options(directory.path())));
        const auto op = session.submit("local-example", {}); QVERIFY(session.operation(op).localOnly); session.stop();
        auto stopped = session.stopRecording(); QTRY_VERIFY(ready(stopped));
        auto query = queryArchive(directory.path(), session.sessionId(), {op}); QTRY_VERIFY(ready(query)); QVERIFY(query.get().ok);
        QCOMPARE(query.get().snapshot.operations.size(), 1); QVERIFY(query.get().snapshot.operations.first().localOnly);
        QCOMPARE(query.get().snapshot.operations.first().update.state, "success");
    }
    void widgetRecordingAndArchiveExport() {
        QTemporaryDir directory; Wire wire; DebugSession session(catalog(), nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents(); RecordingPanel panel(session); panel.resize(980, 1000); panel.show();
        QTest::mouseClick(panel.findChild<QPushButton *>("recordingExpand"), Qt::LeftButton);
        panel.findChild<QLineEdit *>("recordingDirectory")->setText(directory.path());
        QTest::mouseClick(panel.findChild<QPushButton *>("recordingStart"), Qt::LeftButton);
        QTRY_COMPARE(session.recordingStats().state, "running"); session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
        QTest::mouseClick(panel.findChild<QPushButton *>("recordingStop"), Qt::LeftButton); QTRY_COMPARE(session.recordingStats().state, "stopped");
        auto *list = panel.findChild<QPushButton *>("archiveList"); QTest::mouseClick(list, Qt::LeftButton); QTRY_VERIFY(list->isEnabled());
        auto *sessions = panel.findChild<QComboBox *>("archiveSession"); QCOMPARE(sessions->count(), 1);
        auto *query = panel.findChild<QPushButton *>("archiveQuery"); QTest::mouseClick(query, Qt::LeftButton); QTRY_VERIFY(query->isEnabled());
        auto *output = panel.findChild<QPlainTextEdit *>("archiveResult"); QVERIFY(output->toPlainText().contains(session.sessionId()));
        panel.findChild<QLineEdit *>("archiveExportPath")->setText(directory.filePath("widget.jsonl"));
        auto *button = panel.findChild<QPushButton *>("archiveExport"); QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(button->isEnabled());
        QVERIFY(fileBytes(directory.filePath("widget.jsonl")).contains("\"retention\""));
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(panel.grab().save(evidence + "/recording-archive-controlled.png")); }
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--archive-child") return archiveChild(QString::fromLocal8Bit(argv[2]), true);
    RecordingTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "recording_tests.moc"
