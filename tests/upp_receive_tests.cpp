#include "debugger/host.h"
#include "../plugins/can_endpoint.h"
#include <QCanBusDevice>
#include <QTest>
#include "upp_vectors.h"
#include "upp_edge_vectors.h"
#include <QTableWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QComboBox>
using namespace debugger;
namespace {
SessionConfig config(QString direction = "auto") { return {"upp", "can", "observer", {{"backend", "virtualcan"}, {"interface", "can0"}}, {{"direction", direction}}}; }
class Device final : public QCanBusDevice {
public:
    void inject(QByteArray bytes, quint32 id = 0x123, bool extended = false) { QCanBusFrame f(id, bytes); f.setExtendedFrameFormat(extended); enqueueReceivedFrames({f}); QTest::qWait(1); }
    bool writeFrame(const QCanBusFrame &) override { return false; }
    QString interpretErrorFrame(const QCanBusFrame &) override { return {}; }
protected:
    bool open() override { setState(ConnectedState); return true; }
    void close() override { setState(UnconnectedState); }
};
struct Input {
    QPointer<Device> device; EndpointEvents callbacks;
    DebugSession::EndpointFactory factory() { return [this](const QJsonObject &c, EndpointEvents e) { callbacks = e; return createCanEndpoint(c, std::move(e), [this](const QJsonObject &, QString *) { device = new Device; return device.data(); }); }; }
    void adu(const QByteArray &bytes, quint32 id = 0x123, bool extended = false) { for (int p = 0; p < bytes.size(); p += 8) device->inject(bytes.mid(p, 8), id, extended); }
};
}
class UppReceiveTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() { catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins"); }
    void handshakeIsDecodedThroughSession() {
        Input input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY2(session.start(config()), qPrintable(session.diagnostic())); QTRY_COMPARE(session.state(), SessionState::Running);
        input.adu(QByteArray::fromHex("05500701204889"));
        QTRY_VERIFY(!session.messages().isEmpty());
    }
    void completeHandshakeAndCorruptedCrcHaveExactSources() {
        Input input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY(session.start(config())); QTRY_COMPARE(session.state(), SessionState::Running);
        input.adu(QByteArray::fromHex("05500701204889")); QTRY_COMPARE(session.messages().size(), 1);
        auto message = session.messages().last(); QCOMPARE(message.status, QString("decoded"));
        QCOMPARE(message.fields.value("deviceId").toInt(), 5); QCOMPARE(message.fields.value("direction").toString(), QString("request"));
        QCOMPARE(message.fields.value("function").toInt(), 0x20); QCOMPARE(message.sources.size(), 1);
        auto damaged = QByteArray::fromHex("05500701204889"); damaged[6] ^= 1; input.adu(damaged);
        QTRY_COMPARE(session.messages().size(), 2); QCOMPARE(session.messages().last().status, QString("crc-error"));
        QCOMPARE(session.messages().last().sourceRanges[0].length, 7);
        QVERIFY(session.queryTrace({}).operations.isEmpty());
    }
    void fragmentsAndReplyAreDecodedWithAllSources() {
        Input input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY(session.start(config())); QTRY_COMPARE(session.state(), SessionState::Running);
        input.device->inject(QByteArray::fromHex("05500f0120323041")); QVERIFY(session.messages().isEmpty());
        input.device->inject(QByteArray::fromHex("41ffffffffb77a")); QTRY_COMPARE(session.messages().size(), 1);
        const auto m = session.messages().last(); QCOMPARE(m.status, QString("decoded")); QCOMPARE(m.sources.size(), 2);
        QCOMPARE(m.fields.value("direction").toString(), QString("response"));
        QCOMPARE(m.fields.value("pdu").toObject().value("protocolNumberHex").toString(), QString("32304141"));
        QCOMPARE(m.sourceRanges[0].length, 8); QCOMPARE(m.sourceRanges[1].length, 7);
    }
    void everyCanFunction_data() {
        QTest::addColumn<QString>("direction"); QTest::addColumn<QByteArray>("adu"); QTest::addColumn<int>("function");
        for (const auto &v : vectors) QTest::newRow(qPrintable(QString::number(v.function, 16) + '-' + v.direction)) << QString(v.direction) << QByteArray::fromHex(v.hex) << v.function;
    }
    void everyCanFunction() {
        QFETCH(QString, direction); QFETCH(QByteArray, adu); QFETCH(int, function);
        Input input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY(session.start(config(direction))); QTRY_COMPARE(session.state(), SessionState::Running);
        input.adu(adu, 0x123, true); QTRY_COMPARE(session.messages().size(), 1);
        const auto m = session.messages().last(); QCOMPARE(m.status, function & 0x80 ? QString("protocol-exception") : QString("decoded"));
        QCOMPARE(m.fields.value("function").toInt(), function); QCOMPARE(m.fields.value("direction").toString(), direction);
        QCOMPARE(m.sources.size(), (adu.size()+7)/8); QCOMPARE(m.fields.value("aduHex").toString(), QString::fromLatin1(adu.toHex()));
        if (function & 0x80) QCOMPARE(m.fields.value("pdu").toObject().value("exceptionCode").toInt(), 2);
        if (function == 0x22) QCOMPARE(m.fields.value("pdu").toObject().value("units").toArray().size(), 2);
        if (function == 0x23) QCOMPARE(m.fields.value("pdu").toObject().value("subsequence").toInt(), 0);
    }
    void limitsMalformedAndCrcAcrossFrameBoundary() {
        Input input; DebugSession s(catalog, nullptr, input.factory()); QVERIFY(s.start(config("response"))); QTRY_COMPARE(s.state(), SessionState::Running);
        for (const auto &name : {"eight", "nine", "maximum", "empty", "shortException"}) {
            const auto before = s.messages().size(); input.adu(edgeFrames[name]); QTRY_COMPARE(s.messages().size(), before+1);
            QVERIFY2((QStringList{"decoded", "protocol-exception"}.contains(s.messages().last().status)), qPrintable(s.messages().last().detail));
            QCOMPARE(s.messages().last().fields.value("length").toInt(), edgeFrames[name].size());
        }
        QCOMPARE(s.messages()[2].sources.size(), 32); QCOMPARE(s.messages()[1].sourceRanges.last().length, 1);
        for (const auto &name : {"malformed", "badException"}) { input.adu(edgeFrames[name]); QCOMPARE(s.messages().last().status, QString("pdu-error")); }
        input.adu(edgeFrames["unsupported"]); QCOMPARE(s.messages().last().status, QString("unsupported-function"));
    }
    void directionAndUniqueContextNeverInventAddresses() {
        Input input; DebugSession s(catalog, nullptr, input.factory());
        auto c = config(); c.protocol["requestCanIds"] = "123"; c.protocol["responseCanIds"] = "124";
        QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running);
        input.adu(edgeFrames["ambiguous"], 0x125); QCOMPARE(s.messages().last().status, QString("ambiguous-direction"));
        input.adu(edgeFrames["ambiguous"], 0x123); QCOMPARE(s.messages().last().fields.value("direction").toString(), QString("request"));
        input.adu(edgeFrames["rightResponse"], 0x124); QCOMPARE(s.messages().last().fields.value("requestContext").toString(), QString("matched"));
        QCOMPARE(s.messages().last().fields.value("requestPdu").toObject().value("offset").toInt(), 6);
        input.adu(edgeFrames["rightResponse"], 0x124); QCOMPARE(s.messages().last().fields.value("requestContext").toString(), QString("unavailable"));
        input.adu(edgeFrames["ambiguous"], 0x123); input.adu(edgeFrames["ambiguous"], 0x123);
        input.adu(edgeFrames["rightResponse"], 0x124); QCOMPARE(s.messages().last().fields.value("requestContext").toString(), QString("ambiguous"));
        input.adu(edgeFrames["ambiguous"], 0x123); input.adu(edgeFrames["wrongResponse"], 0x124);
        QCOMPARE(s.messages().last().status, QString("pdu-error"));
        input.adu(edgeFrames["ambiguous"], 0x123); input.adu(edgeFrames["ambiguous"], 0x125);
        input.adu(edgeFrames["rightResponse"], 0x124); QCOMPARE(s.messages().last().fields.value("requestContext").toString(), QString("ambiguous"));
        QVERIFY(s.queryTrace({}).operations.isEmpty());
        const auto operation = s.submit("write", {}); QCOMPARE(s.operation(operation).update.state, QString("rejected"));
    }
    void timeoutInterleavingStopAndOldCallbacks() {
        qint64 clock = 0; Input input; DebugSession s(catalog, nullptr, input.factory(), [&]{ return clock; });
        auto c = config("response"); QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running);
        const auto first = QByteArray::fromHex("05500f0120323041"); input.device->inject(first);
        clock = 100000; s.poll(); QCOMPARE(s.messages().last().status, QString("reassembly-timeout"));
        input.device->inject(first); input.adu(edgeFrames["empty"], 0x124); QCOMPARE(s.messages()[1].status, QString("interleaved-frame"));
        QCOMPARE(s.messages().last().status, QString("decoded"));
        input.device->inject(first); const auto old = input.callbacks; s.stop(); QCOMPARE(s.messages().last().status, QString("incomplete"));
        QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running); const auto count = s.messages().size();
        old.received(ReceiveChunk(first)); old.failed("old"); QTest::qWait(20); QCOMPARE(s.messages().size(), count);
        input.adu(edgeFrames["empty"]); QCOMPARE(s.messages().last().status, QString("decoded"));
        QVERIFY(s.snapshot().value("pendingBytes").toInt() == 0);
    }
    void dedicatedViewShowsMessagesFieldsAndSources() {
        Input input; DebugSession s(catalog, nullptr, input.factory()); QVERIFY(s.start(config())); QTRY_COMPARE(s.state(), SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("upp", nullptr, s.access())); QVERIFY(view);
        view->resize(1100, 800); view->show(); input.adu(QByteArray::fromHex("05500f012032304141ffffffffb77a"));
        auto *table = view->findChild<QTableWidget *>("uppMessages"); QVERIFY(table); QTRY_COMPARE(table->rowCount(), 1);
        table->selectRow(0); auto *fields = view->findChild<QTreeWidget *>("uppFields"); QVERIFY(fields); QTRY_VERIFY(fields->topLevelItemCount() > 0);
        auto *sources = view->findChild<QTableWidget *>("uppSources"); QVERIFY(sources); QTRY_COMPARE(sources->rowCount(), 2);
        QVERIFY(view->sessionControlsHost()); QCOMPARE(view->devices().size(), 1); QCOMPARE(view->devices().front().id, QString("5"));
    }
    void soeOnlyDecodesExplicitlyTypedModels() {
        Input input; DebugSession s(catalog, nullptr, input.factory()); auto c = config("response");
        QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running); input.adu(edgeFrames["soe"]);
        auto unit = s.messages().last().fields.value("pdu").toObject().value("units").toArray().first().toObject(); QVERIFY(!unit.contains("soe"));
        s.stop(); c.protocol["soeModels"] = "2"; QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running); input.adu(edgeFrames["soe"]);
        unit = s.messages().last().fields.value("pdu").toObject().value("units").toArray().first().toObject();
        const auto changes = unit.value("soe").toObject().value("changes").toArray(); QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].toObject().value("bit").toInt(), 0); QCOMPARE(changes[1].toObject().value("bit").toInt(), 1);
        QCOMPARE(changes[0].toObject().value("millisecondsWithinMinute").toInt(), 1000); QCOMPARE(changes[1].toObject().value("minute").toInt(), 6);
    }
    void independentVirtualCanReachesUppViewAndExport() {
        DebugSession s(catalog); auto c = config(); c.endpoint["interface"] = "can7";
        QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running);
        QProcess source; source.start(QCoreApplication::applicationDirPath() + "/can_sample_source.exe", {"--profile", "upp", "--interface", "can7", "--count", "3", "--can-id", "321"});
        QVERIFY(source.waitForStarted()); QTRY_COMPARE(s.messages().size(), 2); QTRY_COMPARE(source.state(), QProcess::NotRunning); QCOMPARE(source.exitCode(), 0);
        QCOMPARE(s.messages().last().status, QString("decoded")); QCOMPARE(s.messages().last().sources.size(), 2);
        QCOMPARE(s.messages().last().fields.value("deviceId").toInt(), 5); QCOMPARE(s.messages().last().fields.value("sourceKind").toString(), QString("virtual-can-test"));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("upp", nullptr, s.access())); QVERIFY(view); view->resize(1200, 850); view->show();
        auto *table = view->findChild<QTableWidget *>("uppMessages"); QTRY_COMPARE(table->rowCount(), 2); table->selectRow(1);
        QTRY_COMPARE(view->findChild<QTableWidget *>("uppSources")->rowCount(), 2);
        const auto dir = qEnvironmentVariable("DEBUGGER_UPP_EVIDENCE_DIR");
        if (!dir.isEmpty()) { QVERIFY(QDir().mkpath(dir)); QVERIFY(view->grab().save(dir + "/upp-virtual-view.png")); QFile log(dir + "/sample-source.jsonl"); QVERIFY(log.open(QIODevice::WriteOnly)); log.write(source.readAllStandardOutput()); }
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString exportPath = dir.isEmpty() ? temporary.filePath("upp-session.jsonl") : dir + "/upp-session.jsonl";
        auto result = s.exportTrace({}, exportPath, "jsonl"); const auto exported = result.get(); QVERIFY2(exported.ok, qPrintable(exported.error));
        QFile file(exportPath); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); QVERIFY(bytes.contains("32304141")); QVERIFY(bytes.contains("ruleProfile")); QVERIFY(bytes.contains("sourceRanges"));
        if (dir.isEmpty()) { file.close(); QVERIFY(QFile::remove(exportPath)); }
    }
    void boundedContextRecordsExpiryAndReconnect() {
        qint64 clock = 0; Input input; DebugSession s(catalog, nullptr, input.factory(), [&]{ return clock; });
        QVERIFY(s.configureTrace({8, 8, 8})); auto c = config("request"); QVERIFY(s.start(c)); QTRY_COMPARE(s.state(), SessionState::Running);
        input.adu(QByteArray::fromHex("05500701204889")); input.device->inject(QByteArray::fromHex("05500f0120323041")); clock = 100000; s.poll(); QCOMPARE(s.snapshot().value("requestContexts").toInt(), 0);
        for (int i = 0; i < 40; ++i) input.adu(QByteArray::fromHex("05500701204889"));
        QCOMPARE(s.messages().size(), 8); QVERIFY(s.expiredTraceCount() >= 32); QVERIFY(s.snapshot().value("requestContexts").toInt() <= 256);
        clock = 1100000; s.poll(); QCOMPARE(s.snapshot().value("requestContexts").toInt(), 0);
        input.device->inject(QByteArray::fromHex("05500f0120323041")); const auto old = input.callbacks; old.failed("disconnect"); QTRY_COMPARE(s.state(), SessionState::Error);
        QCOMPARE(s.messages().last().status, QString("incomplete")); QVERIFY(s.reconnect()); QTRY_COMPARE(s.state(), SessionState::Running);
        const auto count = s.messages().size(); old.received(ReceiveChunk(QByteArray::fromHex("41ffffffffb77a"))); QTest::qWait(20); QCOMPARE(s.messages().size(), count);
        input.adu(QByteArray::fromHex("05500701204889")); QCOMPARE(s.messages().last().status, QString("decoded"));
    }
    void malformedFragmentsStayRawAndDoNotCrossBoundaries() {
        Input input; DebugSession s(catalog, nullptr, input.factory()); QVERIFY(s.start(config())); QTRY_COMPARE(s.state(), SessionState::Running);
        input.device->inject(QByteArray::fromHex("05500f0120323041")); input.device->inject(QByteArray::fromHex("41ffffffffb77a00")); QCOMPARE(s.messages().last().status, QString("length-error"));
        input.device->inject(QByteArray::fromHex("05500f0120323041")); input.device->inject(QByteArray::fromHex("05500f0120323041")); QCOMPARE(s.messages().last().status, QString("length-error"));
        input.device->inject({}); QCOMPARE(s.messages().last().status, QString("invalid-header"));
        input.adu(QByteArray::fromHex("05500701204889")); QCOMPARE(s.messages().last().status, QString("decoded"));
        QCOMPARE(s.messages().last().sources.size(), 1);
    }
    void configurationWorkspaceAndOtherProtocolsRemainIsolated() {
        Input invalidInput; DebugSession invalid(catalog, nullptr, invalidInput.factory()); auto bad = config(); bad.protocol["requestCanIds"] = "123"; bad.protocol["responseCanIds"] = "123";
        QVERIFY(!invalid.start(bad)); QVERIFY(invalidInput.device.isNull());
        QTemporaryDir directory; QVERIFY(directory.isValid()); Input input; Workbench window(catalog, nullptr, input.factory());
        auto *protocol = window.sessionWindow(0)->findChild<QComboBox *>("protocol"); QVERIFY(protocol); protocol->setCurrentIndex(protocol->findData("upp"));
        QVERIFY(window.sessionWindow(0)->findChild<QTableWidget *>("uppMessages"));
        const auto path = directory.filePath("upp-workspace.json"); const auto save = window.saveWorkspace(path); QTRY_COMPARE(window.workspaceOperation(save).state, QString("success"));
        Input restoredInput; Workbench restored(catalog, nullptr, restoredInput.factory()); const auto load = restored.openWorkspace(path); QTRY_COMPARE(restored.workspaceOperation(load).state, QString("success"));
        QCOMPARE(restored.sessionWindow(0)->findChild<QComboBox *>("protocol")->currentData().toString(), QString("upp")); QCOMPARE(restored.session(0)->state(), SessionState::Stopped); QVERIFY(restoredInput.device.isNull());
        Input uppInput, canInput, rtuInput; DebugSession upp(catalog, nullptr, uppInput.factory()), can(catalog, nullptr, canInput.factory()), rtu(catalog, nullptr, rtuInput.factory());
        auto uppConfig = config(); uppConfig.endpoint["interface"] = "can2"; QVERIFY(upp.start(uppConfig)); QTRY_COMPARE(upp.state(), SessionState::Running);
        auto raw = config(); raw.protocolId = "can.raw"; raw.endpoint["interface"] = "can3"; QVERIFY(can.start(raw)); QTRY_COMPARE(can.state(), SessionState::Running);
        SessionConfig serial{"modbus.rtu", "serial", "master", {{"port", "COM210"}, {"baud", "9600"}, {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
        QVERIFY(rtu.start(serial)); QTRY_COMPARE(rtu.state(), SessionState::Running);
        uppInput.adu(QByteArray::fromHex("05500701204889")); QCOMPARE(upp.messages().size(), 1); QVERIFY(can.messages().isEmpty()); QVERIFY(rtu.messages().isEmpty()); upp.stop(); QCOMPARE(can.state(), SessionState::Running); QCOMPARE(rtu.state(), SessionState::Running);
    }
};
QTEST_MAIN(UppReceiveTests)
#include "upp_receive_tests.moc"
