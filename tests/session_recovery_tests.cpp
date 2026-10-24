#include "slave_test_support.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QCursor>
#include <QScreen>
#include <QPlainTextEdit>
#include <QMenuBar>
#include <QTreeWidget>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
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
    void closingWindowRetainsSessionConfigurationAndOtherConnection() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        const auto settings = master("COM107");
        QCOMPARE(window.addSession(settings, QStringLiteral("保留配置的会话")), 1);
        auto *first = window.session(0), *second = window.session(1);
        QVERIFY(first->start(master("COM106")));
        QVERIFY(second->start(settings));
        QTRY_COMPARE(first->state(), SessionState::Running);
        QTRY_COMPARE(second->state(), SessionState::Running);
        QVERIFY(window.closeSession(1));
        QCOMPARE(window.sessionCount(), 2);
        QCOMPARE(window.session(1), second);
        QCOMPARE(second->state(), SessionState::Stopped);
        QCOMPARE(window.sessionConfiguration(1).endpoint, settings.endpoint);
        QCOMPARE(first->state(), SessionState::Running);
        QCOMPARE(hub.links["COM107"]->closes, 1);
        window.selectSession(1);
        QCOMPARE(second->state(), SessionState::Stopped);
        QCOMPARE(hub.links["COM107"]->opens, 1);
    }
    void windowArrangementIsAvailableWithoutStoppingSessions() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        window.addSession(master("COM109"));
        QVERIFY(window.session(0)->start(master("COM108")));
        QVERIFY(window.session(1)->start(window.sessionConfiguration(1)));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        QTRY_COMPARE(window.session(1)->state(), SessionState::Running);
        window.show();
        QAction *tile = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("左右平铺")) tile = action;
        QVERIFY2(tile, "工作台应提供同时显示多个会话的平铺入口");
        tile->trigger();
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        QCOMPARE(window.session(1)->state(), SessionState::Running);
        QVERIFY(!window.isSessionFloating(0)); QVERIFY(!window.isSessionFloating(1));
        QVERIFY(window.sessionView(0)->isVisible()); QVERIFY(window.sessionView(1)->isVisible());
        const auto *first = window.session(0);
        auto *content = window.sessionView(0);
        window.floatSession(0);
        auto *floating = window.sessionWindow(0); QVERIFY(floating->isWindow());
        QVERIFY(floating->windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(floating->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(floating->windowFlags().testFlag(Qt::WindowCloseButtonHint));
        QVERIFY(window.screen()->availableGeometry().contains(floating->frameGeometry()));
        QVERIFY(QLineF(floating->frameGeometry().center(), window.frameGeometry().center()).length() < 32);
        floating->showMinimized(); QTest::qWait(40);
        QCOMPARE(window.session(0), first); QCOMPARE(first->state(), SessionState::Running);
        window.selectSession(0); QVERIFY(!floating->isMinimized());
        floating->showMaximized(); QTest::qWait(40);
        QCOMPARE(first->state(), SessionState::Running);
        floating->showNormal(); window.dockSession(0); window.tileSessions(Qt::Vertical);
        QCOMPARE(window.sessionView(0), content); QCOMPARE(window.session(0), first);
        QCOMPARE(first->state(), SessionState::Running);
        window.floatSession(0); QVERIFY(floating->close());
        QVERIFY(!window.isSessionOpen(0)); QCOMPARE(first->state(), SessionState::Stopped);
        QCOMPARE(window.session(1)->state(), SessionState::Running);
        window.tileSessions(Qt::Horizontal); QVERIFY(!window.isSessionOpen(0));
        window.selectSession(0); QVERIFY(window.isSessionOpen(0));
        QCOMPARE(first->state(), SessionState::Stopped);
    }
#ifdef Q_OS_WIN
    void titleMenuDocksWithoutExtraRowOrRestartingSession() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory()); window.show();
        auto *session = window.session(0); auto *view = window.sessionView(0);
        QVERIFY(session->start(master("COM118")));
        QTRY_COMPARE(session->state(), SessionState::Running);
        auto *floating = window.sessionWindow(0);
        QVERIFY(window.isSessionFloating(0));
        QVERIFY(floating->findChildren<QMenuBar *>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
        for (int cycle = 0; cycle < 2; ++cycle) {
            const auto handle = reinterpret_cast<HWND>(floating->winId());
            const auto menu = GetSystemMenu(handle, FALSE); QVERIFY(menu);
            SendMessage(handle, WM_INITMENU, reinterpret_cast<WPARAM>(menu), 0);
            const int count = GetMenuItemCount(menu);
            SendMessage(handle, WM_INITMENU, reinterpret_cast<WPARAM>(menu), 0);
            QCOMPARE(GetMenuItemCount(menu), count);
            QVERIFY(GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) != UINT(-1));
            QVERIFY(GetMenuState(menu, SC_MINIMIZE, MF_BYCOMMAND) != UINT(-1));
            QVERIFY(GetMenuState(menu, SC_MAXIMIZE, MF_BYCOMMAND) != UINT(-1));
            UINT command = 0;
            for (int i = 0; i < count; ++i) {
                wchar_t label[128]{};
                GetMenuStringW(menu, i, label, 128, MF_BYPOSITION);
                if (QString::fromWCharArray(label) == QStringLiteral("停靠到工作台"))
                    command = GetMenuItemID(menu, i);
            }
            QVERIFY(command != 0);
            SendMessage(handle, WM_SYSCOMMAND, command, 0);
            QTRY_VERIFY(!window.isSessionFloating(0));
            QCOMPARE(window.sessionView(0), view);
            QCOMPARE(session->state(), SessionState::Running);
            QAction *floatAction = nullptr;
            for (auto *action : window.findChildren<QAction *>("floatSessionAction"))
                if (action->parent()->isWidgetType() && static_cast<QWidget *>(action->parent())->isVisible())
                    floatAction = action;
            QVERIFY(floatAction); floatAction->trigger();
            QTRY_VERIFY(window.isSessionFloating(0));
        }
        QCOMPARE(hub.links["COM118"]->opens, 1);
        QCOMPARE(hub.links["COM118"]->closes, 0);
        QCOMPARE(window.sessionView(0), view);
    }
    void nativeWindowMoveDropsIntoEmptyWorkbench() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory()); window.show();
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(window.winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
        auto *floating = window.sessionWindow(0); QVERIFY(floating);
        QVERIFY(window.isSessionFloating(0));
        window.raise(); window.activateWindow(); QTest::qWait(100);
        const auto previous = QCursor::pos();
        QCursor::setPos(window.mapToGlobal(QPoint(window.width() * 3 / 4, window.height() / 2)));
        const auto handle = reinterpret_cast<HWND>(floating->winId());
        SendMessage(handle, WM_ENTERSIZEMOVE, 0, 0);
        floating->move(floating->pos() + QPoint(10, 10));
        SendMessage(handle, WM_MOVING, 0, 0);
        SendMessage(handle, WM_EXITSIZEMOVE, 0, 0);
        QTest::qWait(100); QCursor::setPos(previous);
        QVERIFY2(!window.isSessionFloating(0), "原生窗口移动完成后应按预览停靠到空工作台");
        QCOMPARE(window.session(0)->state(), SessionState::Stopped);
    }
    void nativeMoveCancellationAndOcclusionKeepFloating() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory()); window.show();
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(window.winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
        auto *floating = window.sessionWindow(0);
        const auto handle = reinterpret_cast<HWND>(floating->winId());
        const auto previous = QCursor::pos();
        const auto point = window.mapToGlobal(QPoint(window.width() * 3 / 4, window.height() / 2));
        window.raise(); window.activateWindow(); QTest::qWait(80); QCursor::setPos(point);
        SendMessage(handle, WM_ENTERSIZEMOVE, 0, 0); floating->move(floating->pos() + QPoint(10, 10));
        SendMessage(handle, WM_MOVING, 0, 0); SendMessage(handle, WM_CANCELMODE, 0, 0);
        SendMessage(handle, WM_EXITSIZEMOVE, 0, 0); QTest::qWait(80);
        const bool cancelled = window.isSessionFloating(0);
        QWidget other; other.setWindowTitle(QStringLiteral("遮挡工作台的窗口")); other.resize(240, 180);
        other.move(point - QPoint(120, 90)); other.show(); other.raise(); other.activateWindow(); QTest::qWait(80);
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(other.winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
        POINT cursor{}; QVERIFY(GetCursorPos(&cursor));
        QCOMPARE(GetAncestor(WindowFromPoint(cursor), GA_ROOT), reinterpret_cast<HWND>(other.winId()));
        SendMessage(handle, WM_ENTERSIZEMOVE, 0, 0); floating->move(floating->pos() + QPoint(10, 10));
        SendMessage(handle, WM_MOVING, 0, 0); SendMessage(handle, WM_EXITSIZEMOVE, 0, 0); QTest::qWait(80);
        const bool occluded = window.isSessionFloating(0);
        QCursor::setPos(previous);
        QVERIFY(cancelled); QVERIFY(occluded);
    }
    void nativeMoveCanTileOnEverySideOfAnotherSession() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        window.addSession(master("COM112")); window.show();
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(window.winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
        const auto previous = QCursor::pos();
        for (int side = 0; side < 4; ++side) {
            window.dockSession(0); window.floatSession(1);
            QVERIFY(SetWindowPos(reinterpret_cast<HWND>(window.winId()), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
            window.raise(); window.activateWindow(); QTest::qWait(80);
            auto *anchor = window.sessionWindow(0), *moving = window.sessionWindow(1);
            const QList<QPoint> points{{12, anchor->height() / 2}, {anchor->width() - 12, anchor->height() / 2},
                {anchor->width() / 2, 12}, {anchor->width() / 2, anchor->height() - 12}};
            QCursor::setPos(anchor->mapToGlobal(points[side]));
            const auto handle = reinterpret_cast<HWND>(moving->winId());
            SendMessage(handle, WM_ENTERSIZEMOVE, 0, 0); moving->move(moving->pos() + QPoint(10, 10));
            SendMessage(handle, WM_MOVING, 0, 0); SendMessage(handle, WM_EXITSIZEMOVE, 0, 0);
            QTest::qWait(100); QCursor::setPos(previous);
            QVERIFY2(!window.isSessionFloating(1), qPrintable(QStringLiteral("平铺方向 %1").arg(side)));
            const auto a = anchor->mapToGlobal(anchor->rect().center());
            const auto b = moving->mapToGlobal(moving->rect().center());
            if (side == 0) QVERIFY(b.x() < a.x());
            if (side == 1) QVERIFY(b.x() > a.x());
            if (side == 2) QVERIFY(b.y() < a.y());
            if (side == 3) QVERIFY(b.y() > a.y());
        }
    }
#endif
    void navigationAndRecordsStayWithTheirSession() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        auto settings = master("COM111");
        settings.protocol["parameterRows"] = QJsonArray{
            QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107}},
            QJsonObject{{"unit", 18}, {"area", "holding"}, {"address", 107}}};
        window.addSession(settings, QStringLiteral("导航目标 B")); window.show();
        QVERIFY(window.session(0)->start(master("COM110")));
        QVERIFY(window.session(1)->start(settings));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        QTRY_COMPARE(window.session(1)->state(), SessionState::Running);
        auto recordView = [](QWidget *pane) {
            for (auto *view : pane->findChildren<QPlainTextEdit *>())
                if (view->accessibleName() == QStringLiteral("本会话通信记录")) return view;
            return static_cast<QPlainTextEdit *>(nullptr);
        };
        auto *firstRecords = recordView(window.sessionView(0)), *secondRecords = recordView(window.sessionView(1));
        QVERIFY(firstRecords && secondRecords);
        hub.links["COM110"]->events.received(QByteArray::fromHex("a1b2"));
        hub.links["COM111"]->events.received(QByteArray::fromHex("c3d4"));
        QTRY_VERIFY(firstRecords->toPlainText().contains("a1 b2"));
        QTRY_VERIFY(secondRecords->toPlainText().contains("c3 d4"));
        QVERIFY(!firstRecords->toPlainText().contains("c3 d4"));
        QVERIFY(window.selectDevice(1, "18"));
        QTreeWidget *tree = nullptr;
        for (auto *candidate : window.findChildren<QTreeWidget *>())
            if (candidate->accessibleName() == QStringLiteral("会话与设备")) tree = candidate;
        QVERIFY(tree);
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        QTRY_COMPARE(tree->topLevelItem(1)->childCount(), 2);
        auto *device = tree->topLevelItem(1)->child(0);
        QCOMPARE(device->text(0), QStringLiteral("设备 17"));
        window.sessionWindow(1)->showMinimized();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(device).center());
        QCOMPARE(window.parameterView(1)->selectedDevice(), QString("17"));
        QVERIFY(!window.sessionWindow(1)->isMinimized());
        QPushButton *fold = nullptr;
        for (auto *button : window.sessionView(1)->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("通信记录")) fold = button;
        QVERIFY2(fold, "所属窗口应提供通信记录折叠入口");
        QTest::mouseClick(fold, Qt::LeftButton); QVERIFY(!secondRecords->isVisible());
        QCOMPARE(window.session(1)->state(), SessionState::Running);
        QTest::mouseClick(fold, Qt::LeftButton);
        window.tileSessions(Qt::Horizontal); window.floatSession(1);
        QCOMPARE(recordView(window.sessionView(1)), secondRecords);
        QVERIFY(secondRecords->toPlainText().contains("c3 d4"));
        QCOMPARE(hub.links["COM111"]->opens, 1);
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) {
            QVERIFY(window.sessionWindow(1)->grab().save(output + "/floating-session-records.png"));
            window.tileSessions(Qt::Horizontal); QTest::qWait(100);
            QVERIFY(window.grab().save(output + "/two-session-workbench.png"));
        }
    }

    void workbenchCreatesParallelWindowsAndProtectsDirtyClose() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        QTemporaryDir dir; const auto path = dir.filePath("slave.json"); QVERIFY(save(path, document()));
        const auto empty = dir.filePath("empty.json"); QVERIFY(save(empty, {{"version", 1}, {"sessions", QJsonArray{}}}));
        const auto loaded = window.openWorkspace(empty); QTRY_COMPARE(window.workspaceOperation(loaded).state, QString("success"));
        QCOMPARE(window.addSession(config({QJsonObject{{"unit", 17}, {"path", path}}}, "COM99"), QStringLiteral("从站A")), 0);
        QCOMPARE(window.addSession(master("COM100"), QStringLiteral("主站B")), 1); QCOMPARE(window.sessionCount(), 2);
        for (int i = 0; i < 2; ++i) QVERIFY(window.session(i)->start(window.sessionConfiguration(i)));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running); QTRY_COMPARE(window.session(1)->state(), SessionState::Running);
        auto *slave = window.session(0);
        slave->submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", 77}});
        slave->access().setDiscardGuard([] { return false; });
        QVERIFY(!window.closeSession(0)); QCOMPARE(window.sessionCount(), 2); QCOMPARE(value(*slave), 77);
        window.resize(1100, 900); window.show(); window.selectSession(1); QCOMPARE(window.currentSessionIndex(), 1);
        QVERIFY(!window.sessionWindow(0)->close()); QVERIFY(window.isSessionOpen(0));
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        auto *pane = window.sessionView(1); QVERIFY(pane->findChild<QPushButton *>("reconnect"));
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
        QVERIFY(window.parameterView(1)); // 重连后保留正式参数操作页。
        QTRY_VERIFY(pane->findChild<QPushButton *>("stop")->isEnabled());
        QVERIFY(pane->findChild<QLabel *>("status")->text().contains(QStringLiteral("重连完成")));
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/parallel-reconnected-controlled.png"));
        QVERIFY(!window.close()); QVERIFY(window.isVisible());
        slave->access().setDiscardGuard([slave] { slave->submit("slave-discard", {{"unit", 17}}); return true; });
        QVERIFY(window.closeSession(0)); QCOMPARE(window.sessionCount(), 2);
        QCOMPARE(window.session(0)->state(), SessionState::Stopped);
        QCOMPARE(window.session(1)->state(), SessionState::Running);
        QVERIFY(window.close()); QCOMPARE(window.session(1)->state(), SessionState::Stopped);
    }
    void windowCloseRechecksEarlierSessionAfterLaterGuard() {
        Hub hub; Workbench window(catalog, nullptr, hub.factory());
        QTemporaryDir dir;
        const auto empty = dir.filePath("empty.json"); QVERIFY(save(empty, {{"version", 1}, {"sessions", QJsonArray{}}}));
        const auto loaded = window.openWorkspace(empty); QTRY_COMPARE(window.workspaceOperation(loaded).state, QString("success"));
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
