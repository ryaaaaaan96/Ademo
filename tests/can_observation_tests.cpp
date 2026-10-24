#include "debugger/host.h"
#include <QTest>
#include <QTableWidget>
#include "../plugins/can_endpoint.h"
#include <QCanBusDevice>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QDialogButtonBox>
using namespace debugger;
namespace {
SessionConfig configuration(QString channel = "can0") { return {"can.raw", "can", "observer", {{"backend", "virtualcan"}, {"interface", channel}}, {}}; }
QList<TraceRecord> received(const DebugSession &session) {
    QList<TraceRecord> out; for (const auto &r : session.traces()) if (r.kind == "rx") out << r; return out;
}
class ControlledCanDevice final : public QCanBusDevice {
public:
    void inject(const QCanBusFrame &frame) { enqueueReceivedFrames({frame}); }
    bool writeFrame(const QCanBusFrame &) override { return false; }
    QString interpretErrorFrame(const QCanBusFrame &) override { return {}; }
protected:
    bool open() override { setState(ConnectedState); return true; }
    void close() override { setState(UnconnectedState); }
};
struct CanInput {
    QPointer<ControlledCanDevice> device;
    EndpointEvents callbacks;
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &config, EndpointEvents events) {
            callbacks = events;
            return createCanEndpoint(config, std::move(events), [this](const QJsonObject &, QString *) { device = new ControlledCanDevice; return device.data(); });
        };
    }
};
}
class CanObservationTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() { catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins"); QCOMPARE(catalog->diagnostics(), QStringList{}); }
    void observerHasDedicatedFrameTable() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        DebugSession session(catalog);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("can.raw", nullptr, session.access()));
        QVERIFY(view);
        QVERIFY(view->findChild<QTableWidget *>("canFrames"));
        QVERIFY(view->devices().isEmpty());
        QVERIFY(view->sessionControlsHost());
    }
    void classicAndEmptyFramesUseSessionRecords() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        EndpointEvents input;
        DebugSession session(catalog, nullptr, [&input](const QJsonObject &, EndpointEvents events) {
            input = std::move(events);
            class Wire final : public Endpoint {
            public: void open() override {} void close() override {} bool send(quint64, const QByteArray &) override { return false; }
            };
            return std::make_unique<Wire>();
        });
        QVERIFY(session.start({"can.raw", "can", "observer", {{"backend", "virtualcan"}, {"interface", "can0"}}, {}}));
        input.opened(true, {}); QTRY_COMPARE(session.state(), SessionState::Running);
        ReceiveChunk first(QByteArray::fromHex("0102")); first.metadata = {{"frameType", "data"}, {"canId", 291}};
        input.received(first); input.received(ReceiveChunk{});
        QTRY_COMPARE(session.traces().last().kind, QString("rx"));
        int count = 0; for (const auto &r : session.traces()) if (r.kind == "rx") ++count;
        QCOMPARE(count, 2);
        QVERIFY(session.messages().isEmpty());
    }
    void backendPreservesRemoteErrorAndUnsupportedFrames() {
        CanInput input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY(session.start(configuration())); QTRY_COMPARE(session.state(), SessionState::Running);
        QCanBusFrame standard(0x123, QByteArray::fromHex("010203")); standard.setTimeStamp({7, 456});
        QCanBusFrame extended(0x123, {}); extended.setExtendedFrameFormat(true);
        QCanBusFrame remote(QCanBusFrame::RemoteRequestFrame); remote.setFrameId(0x321); remote.setPayload(QByteArray::fromHex("deadbeef"));
        QCanBusFrame error(QCanBusFrame::ErrorFrame); error.setError(QCanBusFrame::BusOffError);
        QCanBusFrame fd(0x456, QByteArray::fromHex("000102030405060708090a0b"));
        for (const auto &frame : {standard, extended, remote, error, fd}) input.device->inject(frame);
        QTRY_COMPARE(received(session).size(), 5);
        const auto records = received(session);
        QCOMPARE(records[0].bytes.toHex(), QByteArray("010203"));
        QCOMPARE(records[0].metadata.value("backendTimestampSeconds").toString(), QString("7"));
        QCOMPARE(records[0].metadata.value("backendTimestampMicroseconds").toString(), QString("456"));
        QVERIFY(records[1].metadata.value("extended").toBool()); QVERIFY(records[1].bytes.isEmpty());
        QVERIFY(records[2].bytes.isEmpty()); QCOMPARE(records[2].metadata.value("remoteExpectedLength").toInt(), 4);
        QCOMPARE(records[2].metadata.value("qtRemotePlaceholderHex").toString(), QString("deadbeef"));
        QVERIFY(records[2].metadata.value("declaredDlc").isNull());
        QCOMPARE(records[3].metadata.value("frameType").toString(), QString("error"));
        QCOMPARE(records[3].metadata.value("errorFlags").toInt(), 64); QVERIFY(!records[3].metadata.value("canIdAvailable").toBool());
        QVERIFY(!records[3].metadata.value("formatAvailable").toBool());
        QVERIFY(records[3].bytes.isEmpty()); QCOMPARE(records[4].bytes.size(), 12);
        QVERIFY(!records[4].metadata.value("classicSupported").toBool()); QVERIFY(!records[4].metadata.value("diagnostic").toString().isEmpty());
        QVERIFY(session.messages().isEmpty()); QVERIFY(session.queryTrace({}).operations.isEmpty());
        QCanBusFrame unknown(QCanBusFrame::UnknownFrame), invalid(QCanBusFrame::InvalidFrame);
        input.device->inject(unknown); input.device->inject(invalid); QTRY_COMPARE(received(session).size(), 7);
        QVERIFY(!received(session)[5].metadata.value("classicSupported").toBool());
        QCOMPARE(received(session)[5].metadata.value("qtFrameType").toInt(), 0);
        QCOMPARE(received(session)[6].metadata.value("qtFrameType").toInt(), 4);
    }
    void filteringAndExportKeepRawRecordsAndSessionSource() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        CanInput input; DebugSession session(catalog, nullptr, input.factory());
        QVERIFY(session.start(configuration())); QTRY_COMPARE(session.state(), SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("can.raw", nullptr, session.access())); view->show();
        auto *table = view->findChild<QTableWidget *>("canFrames"); auto *id = view->findChild<QLineEdit *>("canIdFilter");
        QCanBusFrame first(0x123, QByteArray::fromHex("0102")), second(0x123, QByteArray::fromHex("ff")); second.setExtendedFrameFormat(true);
        input.device->inject(first); input.device->inject(second); input.device->inject(QCanBusFrame(0x456, {}));
        QTRY_COMPARE(table->rowCount(), 3); id->setText("123");
        view->findChild<QComboBox *>("canFormatFilter")->setCurrentIndex(2); QTRY_COMPARE(table->rowCount(), 1);
        QCOMPARE(received(session).size(), 3);
        auto *path = view->findChild<QLineEdit *>("canExportPath"); auto *button = view->findChild<QPushButton *>("canExport");
        auto *result = view->findChild<QLabel *>("canExportResult"); const auto jsonPath = directory.filePath("frames.jsonl"); path->setText(jsonPath); QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(result->text().startsWith(QStringLiteral("已导出 1 帧")));
        QFile json(jsonPath); QVERIFY(json.open(QIODevice::ReadOnly)); const auto lines = json.readAll().trimmed().split('\n'); QCOMPARE(lines.size(), 2);
        const auto row = QJsonDocument::fromJson(lines[1]).object(); QCOMPARE(row.value("sessionId").toString(), session.sessionId()); QCOMPARE(row.value("bytesHex").toString(), QString("ff"));
        QVERIFY(row.value("metadata").toObject().value("extended").toBool());
        view->findChild<QComboBox *>("canExportScope")->setCurrentIndex(1); view->findChild<QComboBox *>("canExportFormat")->setCurrentText("CSV");
        const auto csvPath = directory.filePath("frames.csv"); path->setText(csvPath); QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY(result->text().startsWith(QStringLiteral("已导出 3 帧")));
        QFile csv(csvPath); QVERIFY(csv.open(QIODevice::ReadOnly)); const auto text = csv.readAll(); QVERIFY(text.contains("bytesHex")); QVERIFY(text.contains("0102")); QVERIFY(text.contains("extended")); QVERIFY(text.contains("expiredRecords"));
        id->setText("xyz"); QTRY_COMPARE(table->rowCount(), 0); QCOMPARE(received(session).size(), 3);
    }
    void boundedFramesStopAndOldCallbacksCannotContaminateRestart() {
        CanInput input; DebugSession session(catalog, nullptr, input.factory()); QVERIFY(session.configureTrace({8, 8, 8}));
        QVERIFY(session.start(configuration())); QTRY_COMPARE(session.state(), SessionState::Running);
        for (int i = 0; i < 40; ++i) input.device->inject(QCanBusFrame(i, {}));
        QTRY_COMPARE(received(session).size(), 8); QVERIFY(session.expiredTraceCount() >= 32);
        const auto old = input.callbacks; session.stop(); QVERIFY(input.device.isNull());
        QVERIFY(session.start(configuration())); QTRY_COMPARE(session.state(), SessionState::Running);
        const auto count = received(session).size(); old.received(ReceiveChunk(QByteArray("old"))); old.failed("old failure");
        QTest::qWait(30); QCOMPARE(received(session).size(), count); QCOMPARE(session.state(), SessionState::Running);
        QVERIFY(session.access().recordStatus().value("expiredRecords").toString().toULongLong() >= 32);
    }
    void independentVirtualSampleUsesFormalBackend() {
        DebugSession session(catalog); QVERIFY(session.start(configuration("can8"))); QTRY_COMPARE(session.state(), SessionState::Running);
        QProcess source; source.start(QCoreApplication::applicationDirPath() + "/can_sample_source.exe", {"--interface", "can8", "--count", "4", "--interval", "20"});
        QVERIFY(source.waitForStarted()); QTRY_COMPARE(received(session).size(), 4); QTRY_COMPARE(source.state(), QProcess::NotRunning); QCOMPARE(source.exitCode(), 0);
        const auto records = received(session); QCOMPARE(records[0].metadata.value("canId").toInt(), 0x123); QVERIFY(records[1].metadata.value("extended").toBool());
        QVERIFY(records[2].bytes.isEmpty()); QCOMPARE(records[3].metadata.value("frameType").toString(), QString("remote")); QVERIFY(records[3].bytes.isEmpty());
        QCOMPARE(records[3].metadata.value("remoteExpectedLength").toInt(), 4);
        for (const auto &r : records) QCOMPARE(r.metadata.value("sourceKind").toString(), QString("virtual-can-test"));
        qInfo().noquote() << "实际独立样例源输出：" << source.readAllStandardOutput();
        session.stop();
        DebugSession replacement(catalog); QVERIFY(replacement.start(configuration("can8"))); QTRY_COMPARE(replacement.state(), SessionState::Running); replacement.stop();
    }
    void compatibleDefaultsWorkspaceRestoreAndParallelSessions() {
        QTemporaryDir directory; CanInput input;
        Workbench window(catalog, nullptr, input.factory()); window.show();
        auto *protocol = window.sessionWindow(0)->findChild<QComboBox *>("protocol");
        auto *transport = window.sessionWindow(0)->findChild<QComboBox *>("transport");
        QCOMPARE(protocol->currentData().toString(), QString("modbus.rtu")); QCOMPARE(transport->currentData().toString(), QString("serial"));
        protocol->setCurrentIndex(protocol->findData("can.raw")); QTRY_COMPARE(transport->currentData().toString(), QString("can"));
        QVERIFY(window.sessionWindow(0)->findChild<QTableWidget *>("canFrames"));
        const auto path = directory.filePath("can-workspace.json"); const auto save = window.saveWorkspace(path); QTRY_COMPARE(window.workspaceOperation(save).state, QString("success"));
        CanInput restoredInput; Workbench restored(catalog, nullptr, restoredInput.factory()); const auto load = restored.openWorkspace(path);
        QTRY_COMPARE(restored.workspaceOperation(load).state, QString("success")); QCOMPARE(restored.session(0)->state(), SessionState::Stopped); QVERIFY(restoredInput.device.isNull());
        CanInput canInput, serialInput; DebugSession canSession(catalog, nullptr, canInput.factory()), serialSession(catalog, nullptr, serialInput.factory());
        QVERIFY(canSession.start(configuration("can2"))); QTRY_COMPARE(canSession.state(), SessionState::Running);
        const SessionConfig rtu{"modbus.rtu", "serial", "master", {{"port", "COM210"}, {"baud", "9600"}, {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
        QVERIFY(serialSession.start(rtu)); QTRY_COMPARE(serialSession.state(), SessionState::Running);
        canInput.device->inject(QCanBusFrame(0x123, QByteArray::fromHex("abcd"))); QTRY_COMPARE(received(canSession).size(), 1); QVERIFY(received(serialSession).isEmpty());
        const auto previous = canInput.callbacks; previous.failed("controlled disconnect"); QTRY_COMPARE(canSession.state(), SessionState::Error);
        QVERIFY(canSession.reconnect()); QTRY_COMPARE(canSession.state(), SessionState::Running);
        previous.received(ReceiveChunk(QByteArray("late"))); QTest::qWait(30); QCOMPARE(received(canSession).size(), 1);
        canSession.stop(); QCOMPARE(serialSession.state(), SessionState::Running);
    }
    void switchingProtocolEnablesStartAfterCompatibleRoleSelection() {
        CanInput input; Workbench window(catalog, nullptr, input.factory()); window.show();
        auto *pane = window.sessionWindow(0);
        QPushButton *configure = nullptr;
        for (auto *button : pane->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("连接配置…")) configure = button;
        QVERIFY(configure); QTest::mouseClick(configure, Qt::LeftButton);
        auto *protocol = pane->findChild<QComboBox *>("protocol");
        auto *transport = pane->findChild<QComboBox *>("transport");
        auto *role = pane->findChild<QComboBox *>("role");
        auto *start = pane->findChild<QPushButton *>("start");
        auto *status = pane->findChild<QLabel *>("status");
        QVERIFY(protocol->isVisible());
        protocol->setCurrentIndex(protocol->findData("can.raw"));
        QCOMPARE(transport->currentData().toString(), QString("can"));
        QCOMPARE(role->currentData().toString(), QString("observer"));
        QVERIFY2(start->isEnabled(), "切换到兼容CAN/observer组合后，启动入口必须立即刷新");
        QVERIFY(!status->text().contains(QStringLiteral("不可用")));
        auto *buttons = protocol->window()->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QTest::mouseClick(buttons->button(QDialogButtonBox::Close), Qt::LeftButton);
        QTest::mouseClick(start, Qt::LeftButton); QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        QTest::mouseClick(pane->findChild<QPushButton *>("stop"), Qt::LeftButton);
        QTRY_COMPARE(window.session(0)->state(), SessionState::Stopped);
        protocol->setCurrentIndex(protocol->findData("modbus.rtu"));
        QCOMPARE(transport->currentData().toString(), QString("serial"));
        QCOMPARE(role->currentData().toString(), QString("master")); QVERIFY(start->isEnabled());
    }
    void exportSurvivesViewSessionAndCatalogDestruction() {
        QTemporaryDir directory; const auto path = directory.filePath("detached.jsonl");
        const auto pluginPath = QCoreApplication::applicationDirPath() + "/can-lifetime-fixtures";
        auto ownCatalog = std::make_shared<PluginCatalog>(); ownCatalog->discover(pluginPath);
        const std::weak_ptr<PluginCatalog> lease = ownCatalog;
        CanInput input; auto session = std::make_unique<DebugSession>(ownCatalog, nullptr, input.factory());
        QVERIFY(session->start(configuration("can3"))); QTRY_COMPARE(session->state(), SessionState::Running);
        input.device->inject(QCanBusFrame(0x7ff, QByteArray::fromHex("1234"))); QTRY_COMPARE(received(*session).size(), 1);
        std::unique_ptr<ParameterView> view(ownCatalog->createParameterView("can.raw", nullptr, session->access()));
        view->findChild<QLineEdit *>("canExportPath")->setText(path);
        view->findChild<QPushButton *>("canExport")->click();
        view.reset(); session.reset(); ownCatalog.reset();
        QTRY_VERIFY(QFile::exists(path)); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(file.readAll().contains("1234"));
        QTRY_VERIFY(lease.expired());
    }
};
QTEST_MAIN(CanObservationTests)
#include "can_observation_tests.moc"
