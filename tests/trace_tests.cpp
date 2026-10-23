#include "debugger/host.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <thread>
using namespace debugger;
namespace {
struct Wire {
    EndpointEvents events;
    QList<QByteArray> writes;
    class Port final : public Endpoint {
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64, const QByteArray &bytes) override { wire_.writes.append(bytes); return true; }
    private: Wire &wire_;
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
    void receive(QByteArray bytes) { events.received(std::move(bytes)); QCoreApplication::processEvents(); }
};
SessionConfig config() {
    return {"trace.fixture", "serial", "master", {{"port", "COM9999"}, {"baud", "9600"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
}
QByteArray readFile(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}
class TraceTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/trace-fixtures");
        QVERIFY(catalog->protocol("trace.fixture")); QVERIFY(catalog->transport("serial"));
    }
    void manyToManyRangesMetadataAndAttempts() {
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        const auto op = session.submit("two-attempts", {});
        QCOMPARE(wire.writes, QList<QByteArray>({"attempt1", "attempt2"}));
        ReceiveChunk chunk("abcdef"); chunk.metadata = {{"actualFlag", true}, {"adapterId", "controlled"}};
        wire.events.received(chunk); QCoreApplication::processEvents(); wire.receive("gh");
        const auto snapshot = session.queryTrace({op});
        QCOMPARE(snapshot.messages.size(), 3); QCOMPARE(snapshot.operations.size(), 1);
        QCOMPARE(snapshot.operations.first().update.state, "success");
        int sends = 0; for (const auto &record : snapshot.records) if (record.kind == "send-request") ++sends;
        QCOMPARE(sends, 2);
        const auto first = session.queryTrace({0, snapshot.messages[0].id});
        QCOMPARE(first.messages.size(), 1); QCOMPARE(first.sources.first().bytes, "ab");
        const auto second = session.queryTrace({0, snapshot.messages[1].id});
        QCOMPARE(second.sources.first().bytes, "cd"); QCOMPARE(second.messages.first().status, "invalid");
        const auto third = session.queryTrace({0, snapshot.messages[2].id});
        QCOMPARE(third.messages.first().sourceRanges.size(), 2);
        QCOMPARE(third.sources[0].bytes + third.sources[1].bytes, "efgh");
        const auto record = session.trace(snapshot.messages.first().sources.first());
        QVERIFY(record); QCOMPARE(record->metadata.value("adapterId").toString(), "controlled");
        QCOMPARE(record->operationId, quint64(0));
    }
    void realModbusSplitResponseRanges() {
        auto production = std::make_shared<PluginCatalog>();
        production->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Wire wire; qint64 time = 0;
        DebugSession session(production, nullptr, wire.factory(), [&] { return time; });
        auto selected = config(); selected.protocolId = "modbus.rtu";
        QVERIFY(session.start(selected)); QCoreApplication::processEvents();
        const auto op = session.submit("read-holding", {{"unit", 1}, {"start", 0}, {"count", 2}});
        time += 5000; session.poll(); wire.events.sent(op, true, {}); QCoreApplication::processEvents();
        wire.receive(QByteArray::fromHex("01030400"));
        time += 500; wire.receive(QByteArray::fromHex("0a0014da3e"));
        time += 5000; session.poll();
        const auto messages = session.messages(op); QVERIFY(!messages.isEmpty());
        QCOMPARE(messages.last().status, "success");
        QCOMPARE(messages.last().sourceRanges.size(), 2);
        QCOMPARE(messages.last().sourceRanges[0].offset, 0);
        QCOMPARE(messages.last().sourceRanges[0].length, 4);
        QCOMPARE(messages.last().sourceRanges[1].length, 5);
    }
    void boundedCacheMarksExpiredAndPreservesOmittedCount() {
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        QString reason; QVERIFY(!session.configureTrace({0, 3, 2}, &reason)); QVERIFY(!reason.isEmpty());
        QVERIFY(session.configureTrace({3, 4, 2}));
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        const auto op = session.submit("two-attempts", {}); wire.receive("abcdef");
        const auto msg = session.messages().first().id;
        for (int i = 0; i < 520; ++i) wire.receive("x");
        QCOMPARE(session.traces().size(), 3); QVERIFY(session.expiredTraceCount() >= 520);
        const auto snapshot = session.queryTrace({0, msg});
        QVERIFY(!snapshot.sources.isEmpty()); QCOMPARE(snapshot.sources.first().availability, "expired");
        QVERIFY(snapshot.sources.first().bytes.isEmpty());
        const auto byOperation = session.queryTrace({op}); QVERIFY(byOperation.omittedObservedRefs > 0);
        QVERIFY(byOperation.operations.first().observedRefs.size() <= 512);
        QVERIFY(session.queryTrace({999999}).requestedOperationMissing);
    }
    void evictedOperationIgnoresLateSendResult_data() {
        QTest::addColumn<bool>("shrink");
        QTest::newRow("configure-shrink") << true;
        QTest::newRow("submit-eviction") << false;
    }
    void evictedOperationIgnoresLateSendResult() {
        QFETCH(bool, shrink);
        auto production = std::make_shared<PluginCatalog>();
        production->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Wire wire; qint64 time = 0;
        DebugSession session(production, nullptr, wire.factory(), [&] { return time; });
        QVERIFY(session.configureTrace({256, 256, shrink ? 2 : 1}));
        auto selected = config(); selected.protocolId = "modbus.rtu";
        QVERIFY(session.start(selected)); QCoreApplication::processEvents();
        const auto old = session.submit("read-holding", {{"unit", 1}, {"start", 0}, {"count", 1}});
        time += 5000; session.poll(); QCOMPARE(wire.writes.size(), 1);
        // 本地发送完成始终未到：协议按发送期限终结，但端点仍可迟到回调。
        time += 1000001; session.poll();
        QVERIFY(session.operation(old).finished);
        QCOMPARE(session.operation(old).update.state, "transport-error");
        if (shrink) {
            const auto retained = session.submit("unsupported", {});
            QVERIFY(session.operation(retained).finished);
            QVERIFY(session.configureTrace({256, 256, 1}));
        }
        const auto current = session.submit("configure-polling", {});
        QVERIFY(current); QVERIFY(session.queryTrace({old}).requestedOperationMissing);
        const auto before = session.operation(current);
        const auto recordsBefore = session.traces().size();
        wire.events.sent(old, true, {}); QCoreApplication::processEvents();
        QCOMPARE(session.traces().size(), recordsBefore + 1);
        const auto late = session.traces().last();
        QCOMPARE(late.kind, "stale-send-result"); QCOMPARE(late.operationId, quint64(0));
        QCOMPARE(session.operation(current).update.state, before.update.state);
        QCOMPARE(session.operation(current).localSent, before.localSent);
        QCOMPARE(session.operation(current).finished, before.finished);
        QCOMPARE(session.operation(current).update.result, before.update.result);
        for (const auto &record : session.traces(current)) QVERIFY(record.kind != "local-sent");
    }
    void timeFilterAndMissingIdentity() {
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
        TraceFilter future; future.fromUtc = QDateTime::currentDateTimeUtc().addDays(1);
        const auto empty = session.queryTrace(future); QVERIFY(empty.records.isEmpty()); QVERIFY(empty.messages.isEmpty());
        TraceFilter all; all.fromUtc = QDateTime::currentDateTimeUtc().addDays(-1); all.toUtc = QDateTime::currentDateTimeUtc().addDays(1);
        QCOMPARE(session.queryTrace(all).messages.size(), 3); QVERIFY(session.queryTrace({0, 999999}).requestedMessageMissing);
    }
    void exportSnapshotSurvivesEvictionAndSessionDestruction() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); std::shared_future<ExportResult> future;
        {
            Wire wire; DebugSession session(catalog, nullptr, wire.factory());
            QVERIFY(session.start(config())); QCoreApplication::processEvents();
            const auto op = session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
            future = session.exportTrace({op}, dir.filePath("trace.jsonl"), "jsonl");
            QVERIFY(session.configureTrace({1, 1, 1})); for (int i = 0; i < 20; ++i) wire.receive("noise");
        }
        QTRY_VERIFY_WITH_TIMEOUT(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
        QVERIFY(future.get().ok); const auto content = readFile(dir.filePath("trace.jsonl"));
        QVERIFY(content.contains("616263646566")); QVERIFY(content.contains("6768"));
        int messages = 0;
        for (const auto &line : content.split('\n')) if (!line.isEmpty()) {
            QJsonParseError error; const auto doc = QJsonDocument::fromJson(line, &error); QCOMPARE(error.error, QJsonParseError::NoError);
            if (doc.object().value("type") == "message") ++messages;
        }
        QCOMPARE(messages, 3);
    }
    void exportMissingSourcesAndCsvEscaping() {
        QTemporaryDir dir; Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        const auto op = session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
        QVERIFY(session.configureTrace({1, 256, 128})); wire.receive("noise");
        auto json = session.exportTrace({op}, dir.filePath("trace.jsonl"), "jsonl");
        auto csv = session.exportTrace({op}, dir.filePath("trace.csv"), "csv");
        QTRY_VERIFY(json.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        QTRY_VERIFY(csv.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        QVERIFY(json.get().ok); QVERIFY(json.get().missingSources > 0); QVERIFY(csv.get().ok);
        QVERIFY(readFile(dir.filePath("trace.jsonl")).contains("\"availability\":\"expired\""));
        QVERIFY(readFile(dir.filePath("trace.csv")).contains(QStringLiteral("错误消息，\"\"原始\"\"\n仍可回溯").toUtf8()));
        auto bad = session.exportTrace({}, dir.filePath("missing/trace.jsonl"), "jsonl");
        QTRY_VERIFY(bad.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready); QVERIFY(!bad.get().ok);
    }
    void exportSlotsRejectBeforeCopy() {
        QTemporaryDir dir; std::atomic<int> copies{0}; std::atomic<bool> release{false};
        std::vector<std::thread> callers; std::vector<std::shared_future<ExportResult>> futures(4);
        for (int i = 0; i < 4; ++i) callers.emplace_back([&, i] {
            futures[i] = exportSnapshot([&] { ++copies; while (!release.load()) std::this_thread::yield(); return TraceSnapshot{}; },
                dir.filePath(QString::number(i)), "jsonl");
        });
        QTRY_COMPARE(copies.load(), 4);
        const auto rejected = exportSnapshot([&] { ++copies; return TraceSnapshot{}; }, dir.filePath("rejected"), "jsonl");
        const bool immediate = rejected.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        const bool refused = immediate && !rejected.get().ok;
        release.store(true); for (auto &caller : callers) caller.join();
        QVERIFY(immediate); QVERIFY(refused); QCOMPARE(copies.load(), 4);
        for (const auto &future : futures) { future.wait(); QVERIFY(future.get().ok); }
    }
    void widgetQueryAndExport() {
        QTemporaryDir dir; Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        QVERIFY(session.start(config())); QCoreApplication::processEvents();
        const auto op = session.submit("two-attempts", {}); wire.receive("abcdef"); wire.receive("gh");
        TracePanel panel(session); panel.resize(900, 800); panel.show();
        QTest::mouseClick(panel.findChild<QPushButton *>("traceExpand"), Qt::LeftButton);
        panel.findChild<QLineEdit *>("traceOperation")->setText(QString::number(op));
        QTest::mouseClick(panel.findChild<QPushButton *>("traceQuery"), Qt::LeftButton);
        auto *output = panel.findChild<QPlainTextEdit *>("traceQueryResult");
        QVERIFY(output->toPlainText().contains("offset=4 length=2")); QVERIFY(output->toPlainText().contains("61 62 63 64 65 66"));
        panel.findChild<QLineEdit *>("traceExportPath")->setText(dir.filePath("widget.jsonl"));
        auto *button = panel.findChild<QPushButton *>("traceExport"); QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(!button->isEnabled()); wire.receive("continued"); QVERIFY(session.traces().last().bytes == "continued");
        QTRY_VERIFY(button->isEnabled()); QVERIFY(!readFile(dir.filePath("widget.jsonl")).isEmpty());
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(panel.grab().save(evidence + "/trace-query-export.png")); }
    }
};
QTEST_MAIN(TraceTests)
#include "trace_tests.moc"
