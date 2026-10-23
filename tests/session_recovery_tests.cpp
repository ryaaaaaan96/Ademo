#include "slave_test_support.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>

namespace {
SessionConfig master(const QString &port, bool polls = false) {
    return {"modbus.rtu", "serial", "master", {{"port", port}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}},
        {{"extraReadRetries", 0}, {"responseTimeoutMs", 20}, {"retryIntervalMs", 2},
         {"polls", polls ? QJsonArray{QJsonObject{{"unit", 1}, {"action", "read-holding"}, {"start", 0},
             {"count", 1}, {"intervalMs", 50}}} : QJsonArray{}}}, {}};
}
struct Link {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    int opens = 0, closes = 0;
    bool openOk = true;
};
struct Hub {
    QMap<QString, std::shared_ptr<Link>> links;
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &config, EndpointEvents events) {
            const auto port = config.value("port").toString();
            if (!links.contains(port)) links[port] = std::make_shared<Link>();
            auto link = links[port]; link->events = std::move(events);
            class Port final : public Endpoint {
                std::shared_ptr<Link> link_;
            public:
                explicit Port(std::shared_ptr<Link> link) : link_(std::move(link)) {}
                void open() override { ++link_->opens; link_->events.opened(link_->openOk, link_->openOk ? QString() : QStringLiteral("端点暂不可用")); }
                void close() override { ++link_->closes; }
                bool send(quint64 id, const QByteArray &bytes) override { link_->writes.append({id, bytes}); return true; }
            };
            return std::make_unique<Port>(std::move(link));
        };
    }
};
void events() { QCoreApplication::processEvents(); }
int value(const DebugSession &session) {
    return session.snapshot().value("devices").toArray().first().toObject().value("data").toObject()
        .value("areas").toObject().value("holding").toObject().value("values").toArray().first().toInt();
}
const QJsonObject readOne{{"unit", 1}, {"start", 0}, {"count", 1}};
}
class RecoveryTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void parallelMasterSlaveAndConflictingEndpoint() {
        Hub hub; qint64 now = 0;
        DebugSession first(catalog, nullptr, hub.factory(), [&] { return now; });
        DebugSession second(catalog, nullptr, hub.factory(), [&] { return now; });
        DebugSession conflict(catalog, nullptr, hub.factory(), [&] { return now; });
        QTemporaryDir dir; const auto path = dir.filePath("slave.json"); QVERIFY(save(path, document()));
        QVERIFY(first.start(master("COM91"))); events();
        QVERIFY(second.start(config({QJsonObject{{"unit", 17}, {"path", path}}}, "COM92")));
        QTRY_COMPARE(second.state(), SessionState::Running);
        QVERIFY(!conflict.start(master("\\\\.\\com91")));
        QVERIFY(conflict.diagnostic().contains(QStringLiteral("占用")));
        const auto id = first.submit("read-holding", readOne); now += 1750; first.poll(); events();
        auto a = hub.links["COM91"], b = hub.links["COM92"];
        a->events.sent(a->writes.last().first, true, {}); a->events.received(QByteArray::fromHex("010302000a3843"));
        b->events.received(QByteArray::fromHex("1103006b00037687")); events(); now += 1750;
        first.poll(); second.poll(); events();
        QCOMPARE(first.operation(id).update.state, "success");
        QCOMPARE(b->writes.last().second.toHex(), "110306022b00000064c8ba");
        b->events.sent(b->writes.last().first, true, {}); events();
        a->events.failed(QStringLiteral("受控拔出")); events();
        QCOMPARE(first.state(), SessionState::Error); QCOMPARE(second.state(), SessionState::Running);
        b->events.received(QByteArray::fromHex("1103006b00037687")); events(); now += 1750; second.poll();
        QCOMPARE(b->writes.size(), 2);
        QVERIFY(conflict.start(master("COM91"))); events();
        QVERIFY(!first.reconnect()); QVERIFY(first.diagnostic().contains(QStringLiteral("占用")));
        conflict.stop(); QVERIFY(first.reconnect()); events(); QCOMPARE(first.state(), SessionState::Running);
    }
    void automaticReconnectResumesPollingAndRejectsOldCallbacks() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        auto settings = master("COM93", true);
        settings.recovery = {{"autoReconnect", true}, {"restorePolling", true}, {"intervalMs", 10}};
        QVERIFY(session.start(settings)); events(); now += 1750; session.poll(); events();
        auto link = hub.links["COM93"]; QCOMPARE(link->writes.size(), 1);
        const auto old = link->events; const auto oldEpoch = session.generation();
        const auto queuedWrite = session.submit("write-single-register", {{"unit", 1}, {"start", 0}, {"value", 7}});
        old.failed(QStringLiteral("断线")); events();
        QCOMPARE(session.state(), SessionState::Error); QVERIFY(session.generation() > oldEpoch);
        QCOMPARE(session.operation(queuedWrite).update.state, "transport-error");
        now += 9999; session.poll(); QCOMPARE(link->opens, 1);
        ++now; session.poll(); events(); QCOMPARE(link->opens, 2); QCOMPARE(session.state(), SessionState::Running);
        now += 1750; session.poll(); events(); QCOMPARE(link->writes.size(), 2);
        const auto currentId = session.queryTrace().operations.last().request.id;
        old.sent(link->writes.first().first, true, {}); old.received(QByteArray::fromHex("010302000a3843")); events();
        QVERIFY(!session.operation(currentId).finished);
        link->events.sent(link->writes.last().first, true, {}); events();
        link->events.received(QByteArray::fromHex("010302000a3843")); events(); now += 1750; session.poll();
        QCOMPARE(session.operation(currentId).update.state, "success");
        for (const auto &write : link->writes) QCOMPARE(write.second[1], char(3));
        QVERIFY(session.diagnostic().contains(QStringLiteral("重连完成")));
        bool restored = false; for (const auto &record : session.traces()) if (record.kind == "connection-restored") restored = true;
        QVERIFY(restored);
        session.stop(); now += 1000000; session.poll(); QCOMPARE(link->opens, 2);
    }
    void sentWriteIsUncertainAndNeverReplayed() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        QVERIFY(session.start(master("COM94"))); events(); auto link = hub.links["COM94"];
        const auto id = session.submit("write-single-register", {{"unit", 1}, {"start", 0}, {"value", 7}});
        now += 1750; session.poll(); events(); QCOMPARE(link->writes.size(), 1);
        link->events.sent(link->writes.last().first, true, {}); events();
        link->events.failed(QStringLiteral("连接断开")); events();
        QVERIFY(session.operation(id).finished); QCOMPARE(session.operation(id).update.state, "transport-error");
        QVERIFY(session.operation(id).update.result.value("uncertain").toBool());
        QVERIFY(session.reconnect()); events(); now += 1000000; session.poll();
        QCOMPARE(link->writes.size(), 1);
        const auto next = session.submit("write-single-register", {{"unit", 1}, {"start", 0}, {"value", 8}});
        now += 1750; session.poll(); QCOMPARE(link->writes.size(), 2); QVERIFY(next != id);
    }
    void manualReconnectCanLeavePollingPaused() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        auto settings = master("COM95", true); settings.recovery = {{"restorePolling", false}};
        QVERIFY(session.start(settings)); events(); auto link = hub.links["COM95"];
        link->events.failed(QStringLiteral("断线")); events(); QVERIFY(session.canReconnect());
        now += 1000000; session.poll(); QCOMPARE(link->opens, 1);
        QVERIFY(session.reconnect()); events(); now += 1000000; session.poll(); QVERIFY(link->writes.isEmpty());
        const auto id = session.submit("read-holding", readOne); now += 1750; session.poll();
        QCOMPARE(link->writes.size(), 1); QCOMPARE(session.operation(id).update.state, "submitted");
    }
    void slaveReconnectPreservesEditedDataDespiteDeletedStartupFile() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        QTemporaryDir dir; const auto path = dir.filePath("slave.json"); QVERIFY(save(path, document()));
        QVERIFY(session.start(config({QJsonObject{{"unit", 17}, {"path", path}}}, "COM96")));
        QTRY_COMPARE(session.state(), SessionState::Running); auto link = hub.links["COM96"];
        const auto edit = session.submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", 77}});
        QCOMPARE(session.operation(edit).update.state, "success"); QCOMPARE(value(session), 77);
        const auto before = session.snapshot(); const auto old = link->events;
        link->events.failed(QStringLiteral("断线")); events(); QVERIFY(QFile::remove(path));
        QCOMPARE(session.snapshot(), before); QVERIFY(session.reconnect()); events();
        QCOMPARE(session.state(), SessionState::Running); QCOMPARE(session.snapshot(), before);
        old.received(QByteArray::fromHex("ff")); events();
        link->events.received(QByteArray::fromHex("1103006b00037687")); events(); now += 1750; session.poll();
        QCOMPARE(link->writes.size(), 1); QCOMPARE(link->writes.last().second.left(9).toHex(), "110306004d00000064");
        QCOMPARE(value(session), 77);
    }
    void repeatedLifecycleReleasesEndpointAndBoundsAutomaticAttempts() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        DebugSession other(catalog, nullptr, hub.factory(), [&] { return now; });
        for (int i = 0; i < 20; ++i) {
            QVERIFY(session.start(master("COM97"))); events();
            hub.links["COM97"]->events.failed(QStringLiteral("断线")); events(); QVERIFY(session.reconnect()); events();
            session.stop(); QVERIFY(other.start(master("COM97"))); events(); other.stop();
        }
        auto settings = master("COM97"); settings.recovery = {{"autoReconnect", true}, {"intervalMs", 10}};
        hub.links["COM97"]->openOk = false; QVERIFY(session.start(settings)); events();
        const auto opens = hub.links["COM97"]->opens;
        for (int i = 0; i < 100; ++i) { ++now; session.poll(); } QCOMPARE(hub.links["COM97"]->opens, opens);
        session.stop(); now += 1000000; session.poll(); QCOMPARE(hub.links["COM97"]->opens, opens);
    }
    void backgroundSaveCompletesAcrossConnectionEpoch() {
        Hub hub; qint64 now = 0; QList<std::function<void()>> jobs;
        DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; },
            [&](std::function<void()> work) { jobs.append(std::move(work)); });
        QTemporaryDir dir; const auto path = dir.filePath("slave.json"), target = dir.filePath("saved.json");
        QVERIFY(save(path, document()));
        QVERIFY(session.start(config({QJsonObject{{"unit", 17}, {"path", path}}}, "COM103")));
        QCOMPARE(jobs.size(), 1); jobs.takeFirst()(); events(); QTRY_COMPARE(session.state(), SessionState::Running);
        session.submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", 77}});
        const auto id = session.submit("slave-save", {{"unit", 17}, {"path", target}});
        QCOMPARE(jobs.size(), 1); QVERIFY(!session.operation(id).finished);
        hub.links["COM103"]->events.failed(QStringLiteral("保存期间断线")); events();
        QVERIFY(!session.operation(id).finished); QVERIFY(session.reconnect()); events();
        QCOMPARE(session.state(), SessionState::Running); QCOMPARE(value(session), 77);
        jobs.takeFirst()(); events(); QCOMPARE(session.operation(id).update.state, "success");
        QCOMPARE(session.config().protocol.value("devices").toArray().first().toObject().value("path").toString(), target);
        QFile file(target); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(saved.value("areas").toObject().value("holding").toObject().value("values").toArray().first().toInt(), 77);
    }
    void invalidRecoveryPolicyDoesNotOpenEndpoint() {
        for (const auto &policy : QList<QJsonObject>{{{"autoReconnect", 1}}, {{"restorePolling", "yes"}}, {{"intervalMs", 0}}, {{"intervalMs", 1.5}}}) {
            Hub hub; DebugSession session(catalog, nullptr, hub.factory()); auto settings = master("COM98"); settings.recovery = policy;
            QVERIFY(!session.start(settings)); QVERIFY(hub.links.isEmpty());
        }
    }
    void qtTimerRetriesInitialOpenFailureWithoutManualPoll() {
        Hub hub; hub.links["COM101"] = std::make_shared<Link>(); hub.links["COM101"]->openOk = false;
        DebugSession session(catalog, nullptr, hub.factory());
        auto settings = master("COM101", true); settings.recovery = {{"autoReconnect", true}, {"intervalMs", 10}};
        QVERIFY(session.start(settings)); QTRY_COMPARE(session.state(), SessionState::Error);
        hub.links["COM101"]->openOk = true;
        QTRY_COMPARE(session.state(), SessionState::Running);
        QVERIFY(hub.links["COM101"]->opens >= 2);
        QTRY_VERIFY(!hub.links["COM101"]->writes.isEmpty());
        session.stop(); const auto count = hub.links["COM101"]->opens; QTest::qWait(30);
        QCOMPARE(hub.links["COM101"]->opens, count);
    }
    void newConfigurationCancelsOldRecovery() {
        Hub hub; qint64 now = 0; DebugSession session(catalog, nullptr, hub.factory(), [&] { return now; });
        auto settings = master("COM102"); settings.recovery = {{"autoReconnect", true}, {"intervalMs", 10}};
        QVERIFY(session.start(settings)); events(); auto link = hub.links["COM102"];
        link->events.failed(QStringLiteral("断线")); events(); QVERIFY(session.canReconnect());
        QVERIFY(session.configure({{"extraReadRetries", 0}})); QVERIFY(!session.canReconnect());
        now += 1000000; session.poll(); QCOMPARE(link->opens, 1);
    }
    void workbenchCreatesParallelTabsAndProtectsDirtyClose() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        QVERIFY(window.closeSession(0));
        QTemporaryDir dir; const auto path = dir.filePath("slave.json"); QVERIFY(save(path, document()));
        QCOMPARE(window.addSession(config({QJsonObject{{"unit", 17}, {"path", path}}}, "COM99"), QStringLiteral("从站A")), 0);
        QCOMPARE(window.addSession(master("COM100"), QStringLiteral("主站B")), 1); QCOMPARE(window.sessionCount(), 2);
        for (int i = 0; i < 2; ++i) QVERIFY(window.session(i)->start(window.sessionConfiguration(i)));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running); QTRY_COMPARE(window.session(1)->state(), SessionState::Running);
        auto *slave = window.session(0);
        slave->submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", 77}});
        slave->access().setDiscardGuard([] { return false; });
        QVERIFY(!window.closeSession(0)); QCOMPARE(window.sessionCount(), 2); QCOMPARE(value(*slave), 77);
        window.resize(1100, 900); window.show(); window.selectSession(1); QCOMPARE(window.currentSessionIndex(), 1);
        auto *tabs = window.findChild<QTabWidget *>("sessionTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 2);
        auto *pane = tabs->widget(1); QVERIFY(pane->findChild<QPushButton *>("reconnect"));
        pane->findChild<QCheckBox *>("autoReconnect")->setChecked(true);
        QVERIFY(window.sessionConfiguration(1).recovery.value("autoReconnect").toBool());
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/parallel-session-tabs-controlled.png"));
        pane->findChild<QCheckBox *>("autoReconnect")->setChecked(false);
        hub.links["COM100"]->events.failed(QStringLiteral("受控断线，另一个会话继续运行")); events();
        QTRY_VERIFY(pane->findChild<QPushButton *>("reconnect")->isEnabled());
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/parallel-disconnected-controlled.png"));
        QTest::mouseClick(pane->findChild<QPushButton *>("reconnect"), Qt::LeftButton);
        QTRY_COMPARE(window.session(1)->state(), SessionState::Running);
        QTRY_VERIFY(pane->findChild<QPushButton *>("readHolding")->isEnabled());
        QVERIFY(pane->findChild<QLabel *>("status")->text().contains(QStringLiteral("重连完成")));
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/parallel-reconnected-controlled.png"));
        QVERIFY(!window.close()); QVERIFY(window.isVisible());
        slave->access().setDiscardGuard([slave] { slave->submit("slave-discard", {{"unit", 17}}); return true; });
        QVERIFY(window.closeSession(0)); QCOMPARE(window.sessionCount(), 1);
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        QVERIFY(window.close()); QCOMPARE(window.session(0)->state(), SessionState::Stopped);
    }
    void windowCloseRechecksEarlierSessionAfterLaterGuard() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory()); QVERIFY(window.closeSession(0));
        QTemporaryDir dir;
        const auto firstPath = dir.filePath("first.json"), secondPath = dir.filePath("second.json");
        QVERIFY(save(firstPath, document())); QVERIFY(save(secondPath, document()));
        window.addSession(config({QJsonObject{{"unit", 17}, {"path", firstPath}}}, "COM104"));
        window.addSession(config({QJsonObject{{"unit", 17}, {"path", secondPath}}}, "COM105"));
        auto *first = window.session(0), *second = window.session(1);
        QVERIFY(first->start(window.sessionConfiguration(0))); QVERIFY(second->start(window.sessionConfiguration(1)));
        QTRY_COMPARE(first->state(), SessionState::Running); QTRY_COMPARE(second->state(), SessionState::Running);
        const auto edit = [](DebugSession *session, int data) {
            return session->submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", data}});
        };
        edit(first, 111); edit(second, 222);
        first->access().setDiscardGuard([first] { first->submit("slave-discard", {{"unit", 17}}); return true; });
        second->access().setDiscardGuard([&] {
            edit(first, 999); // 模拟第二会话模态确认期间第一会话收到新数据。
            second->submit("slave-discard", {{"unit", 17}});
            return true;
        });
        window.resize(1100, 900); window.show();
        QVERIFY(!window.close()); QVERIFY(window.isVisible()); QCOMPARE(window.sessionCount(), 2);
        QCOMPARE(value(*first), 999); QVERIFY(first->hasUnsavedOrBusy()); QVERIFY(!second->hasUnsavedOrBusy());
        QCOMPARE(first->state(), SessionState::Running); QCOMPARE(second->state(), SessionState::Running);
        QVERIFY(window.findChild<QLabel *>("workspaceStatus")->text().contains(QStringLiteral("数据已保留")));
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/close-confirmation-new-data-retained.png"));
        QVERIFY(window.close()); QCOMPARE(first->state(), SessionState::Stopped); QCOMPARE(second->state(), SessionState::Stopped);
    }
};
QTEST_MAIN(RecoveryTests)
#include "session_recovery_tests.moc"
