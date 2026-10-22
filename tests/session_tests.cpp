#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QTest>
#include <QDir>
#include <QFile>

using namespace debugger;
struct ControlledWire {
    QList<EndpointEvents> runs;
    int live = 0;
};
class ControlledEndpoint final : public Endpoint {
public:
    explicit ControlledEndpoint(ControlledWire &wire) : wire_(wire) { ++wire_.live; }
    ~ControlledEndpoint() override { --wire_.live; }
    void open() override {}
    void close() override {}
    bool send(quint64, const QByteArray &) override { return false; }
private:
    ControlledWire &wire_;
};
class SessionTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
    SessionConfig config() const {
        return {"modbus.rtu", "serial", "master",
                {{"port", "COM9999"}, {"baud", "9600"}, {"dataBits", "8"},
                 {"parity", "even"}, {"stopBits", "1"}}, {}};
    }
    DebugSession::EndpointFactory controlled(ControlledWire &wire) {
        return [&wire](const QJsonObject &, EndpointEvents events) {
            wire.runs.append(std::move(events));
            return std::make_unique<ControlledEndpoint>(wire);
        };
    }
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QCOMPARE(catalog->diagnostics(), QStringList{});
        QCOMPARE(catalog->protocols().size(), 1);
        QCOMPARE(catalog->transports().size(), 1);
    }
    void startStopAndRelease() {
        ControlledWire wire;
        DebugSession first(catalog, nullptr, controlled(wire));
        DebugSession second(catalog, nullptr, controlled(wire));
        QVERIFY(first.start(config()));
        QCOMPARE(first.state(), SessionState::Starting);
        wire.runs.last().opened(true, {});
        QTRY_COMPARE(first.state(), SessionState::Running);
        QVERIFY(!first.start(config()));
        auto alias = config();
        alias.endpoint["port"] = "\\\\.\\com9999";
        QVERIFY(!second.start(alias));
        QVERIFY(second.diagnostic().contains(QStringLiteral("占用")));
        first.stop();
        QCOMPARE(wire.live, 0);
        QVERIFY(second.start(alias));
        wire.runs.last().opened(true, {});
        QTRY_COMPARE(second.state(), SessionState::Running);
        second.stop();
        QCOMPARE(second.state(), SessionState::Stopped);
    }
    void staleCallbacksAndDestruction() {
        ControlledWire wire;
        auto session = std::make_unique<DebugSession>(catalog, nullptr, controlled(wire));
        QVERIFY(session->start(config()));
        const auto old = wire.runs.last();
        old.opened(true, {}); // 停止前排队的成功回调也必须无效。
        session->stop();
        QVERIFY(session->start(config()));
        const auto run = session->generation();
        old.failed("late failure");
        QCoreApplication::processEvents();
        QCOMPARE(session->state(), SessionState::Starting);
        wire.runs.last().opened(true, {});
        QTRY_COMPARE(session->state(), SessionState::Running);
        old.opened(false, "late open");
        QCoreApplication::processEvents();
        QCOMPARE(session->state(), SessionState::Running);
        QCOMPARE(session->generation(), run);
        session.reset();
        QCOMPARE(wire.live, 0);
        wire.runs.last().failed("after destruction");
        QCoreApplication::processEvents();
        DebugSession reused(catalog, nullptr, controlled(wire));
        QVERIFY(reused.start(config()));
    }
    void stopOrDestroyFromStartingNotification() {
        ControlledWire wire;
        DebugSession session(catalog, nullptr, controlled(wire));
        connect(&session, &DebugSession::changed, &session, [&session] {
            if (session.state() == SessionState::Starting) session.stop();
        });
        QVERIFY(session.start(config()));
        QCOMPARE(session.state(), SessionState::Stopped);
        QCOMPARE(wire.live, 0);
        QPointer<DebugSession> disposable = new DebugSession(catalog, nullptr, controlled(wire));
        connect(disposable, &DebugSession::changed, disposable, [disposable] {
            if (disposable->state() == SessionState::Starting) delete disposable;
        });
        QVERIFY(disposable->start(config()));
        QVERIFY(disposable.isNull());
        QCOMPARE(wire.live, 0);
        DebugSession reused(catalog, nullptr, controlled(wire));
        QVERIFY(reused.start(config()));
    }
    void failureReleasesAndRetry() {
        ControlledWire wire;
        DebugSession session(catalog, nullptr, controlled(wire));
        QVERIFY(session.start(config()));
        wire.runs.last().opened(false, QStringLiteral("访问被拒绝"));
        QTRY_COMPARE(session.state(), SessionState::Error);
        QVERIFY(session.diagnostic().contains(QStringLiteral("访问被拒绝")));
        QCOMPARE(wire.live, 0);
        QVERIFY(session.start(config()));
        wire.runs.last().opened(true, {});
        QTRY_COMPARE(session.state(), SessionState::Running);
        wire.runs.last().failed(QStringLiteral("设备已断开"));
        QTRY_COMPARE(session.state(), SessionState::Error);
        QCOMPARE(wire.live, 0);
        DebugSession reused(catalog, nullptr, controlled(wire));
        QVERIFY(reused.start(config()));
    }
    void rejectsUnsupportedAndInvalid() {
        ControlledWire wire;
        DebugSession session(catalog, nullptr, controlled(wire));
        auto bad = config();
        bad.role = "unavailable-role";
        QVERIFY(!session.start(bad));
        QVERIFY(session.diagnostic().contains(QStringLiteral("不支持")));
        bad = config();
        bad.transportId = "test-adapter";
        QVERIFY(!session.start(bad));
        QVERIFY(session.diagnostic().contains(QStringLiteral("不可用")));
        bad = config();
        bad.endpoint["baud"] = "0";
        QVERIFY(!session.start(bad));
        bad = config();
        bad.endpoint["port"] = "";
        QVERIFY(!session.start(bad));
        QCOMPARE(wire.runs.size(), 0);
        QCOMPARE(session.config().endpoint, bad.endpoint);
    }
    void realSerialOpenError() {
        // 经过真实串口 DLL 与 QSerialPort 的失败路径，不宣称硬件成功打开。
        DebugSession session(catalog);
        QVERIFY(session.start(config()));
        QTRY_COMPARE(session.state(), SessionState::Error);
        QVERIFY(session.diagnostic().contains("COM9999"));
        QVERIFY(session.diagnostic().contains(QStringLiteral("无法打开串口")));
        QVERIFY(session.start(config()));
        QTRY_COMPARE(session.state(), SessionState::Error);
        qInfo() << "Available serial ports:" << QSerialPortInfo::availablePorts().size();
        qInfo() << "COM9999 open failed with a readable diagnostic (expected nonexistent port)";
    }
    void pluginViewLifetime() {
        auto registry = std::make_shared<PluginCatalog>();
        registry->discover(QCoreApplication::applicationDirPath() + "/plugins");
        std::weak_ptr<PluginCatalog> lifetime = registry;
        QPointer<QWidget> view = registry->createView("modbus.rtu", nullptr);
        QVERIFY(view);
        registry.reset();
        QVERIFY(!lifetime.expired());
        delete view;
        QVERIFY(view.isNull());
        QVERIFY(lifetime.expired());
    }
    void brokenPluginDoesNotHideWorkingOnes() {
        const QDir source(QCoreApplication::applicationDirPath() + "/plugins");
        QFile broken(source.filePath("broken.dll"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a DLL");
        broken.close();
        auto registry = std::make_shared<PluginCatalog>();
        registry->discover(source.path());
        QVERIFY(!registry->diagnostics().isEmpty());
        QVERIFY(registry->protocol("modbus.rtu"));
        QVERIFY(registry->transport("serial"));
        registry.reset();
        QVERIFY(broken.remove());
    }
    void incompatibleAndDuplicatePlugins() {
        const QDir fixtures(QCoreApplication::applicationDirPath() + "/fixtures");
        auto registry = std::make_shared<PluginCatalog>();
        registry->discover(fixtures.path());
        QCOMPARE(registry->protocols().size(), 1);
        QVERIFY(!registry->protocol("incompatible"));
        QCOMPARE(registry->diagnostics().size(), 2);
    }
    void widgetFlow() {
        Workbench window(catalog);
        window.show();
        QVERIFY(window.findChild<QWidget *>("protocolView"));
        auto *port = window.findChild<QLineEdit *>("port");
        auto *start = window.findChild<QPushButton *>("start");
        auto *status = window.findChild<QLabel *>("status");
        QVERIFY(port && start && status);
        port->setText("COM9999");
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_VERIFY(status->text().contains(QStringLiteral("无法打开串口")));
        QVERIFY(start->isEnabled());
        QVERIFY(port->isEnabled());
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            QVERIFY(window.grab().save(output + "/01-open-error.png"));
        }
    }
    void widgetStartStopWithControlledTransport() {
        ControlledWire wire;
        Workbench window(catalog, nullptr, controlled(wire));
        window.show();
        auto *start = window.findChild<QPushButton *>("start");
        auto *stop = window.findChild<QPushButton *>("stop");
        auto *port = window.findChild<QLineEdit *>("port");
        auto *status = window.findChild<QLabel *>("status");
        QVERIFY(start && stop && port && status);
        port->setText("COM9999");
        QTest::mouseClick(start, Qt::LeftButton);
        QVERIFY(!start->isEnabled());
        QVERIFY(!port->isEnabled());
        QVERIFY(stop->isEnabled());
        wire.runs.last().opened(true, {});
        QTRY_VERIFY(status->text().contains(QStringLiteral("运行中")));
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/02-running-controlled.png"));
        QTest::mouseClick(stop, Qt::LeftButton);
        QVERIFY(status->text().contains(QStringLiteral("已停止")));
        QCOMPARE(wire.live, 0);
        QVERIFY(start->isEnabled());
        QVERIFY(port->isEnabled());
        QVERIFY(!stop->isEnabled());
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/03-stopped-controlled.png"));
    }
};
QTEST_MAIN(SessionTests)
#include "session_tests.moc"
