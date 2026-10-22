#include "slave_test_support.h"
#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>

namespace {
QJsonObject device(const DebugSession &session, int unit = 17) {
    for (const auto &entry : session.snapshot().value("devices").toArray())
        if (entry.toObject().value("unit").toInt() == unit) return entry.toObject();
    return {};
}
int holding(const DebugSession &session, int unit = 17) {
    return device(session, unit).value("data").toObject().value("areas").toObject().value("holding")
        .toObject().value("values").toArray().first().toInt();
}
QJsonObject readFile(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(file.readAll()).object(); }
quint64 edit(DebugSession &session, int value) {
    return session.submit("slave-edit-value", {{"unit", 17}, {"area", "holding"}, {"address", 107}, {"value", value}});
}
}
class SlaveEditTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void editAndRangeAreImmediateAndIsolated() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        const auto id = edit(h.session, 42); QCOMPARE(h.session.operation(id).update.state, "success");
        QCOMPARE(holding(h.session), 42); QCOMPARE(holding(h.session, 18), 111); QVERIFY(h.wire.writes.isEmpty());
        h.receive(QByteArray::fromHex("1103006b00037687")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.first(9).toHex(), "110306002a00000064"); h.complete(); h.advance(1750);
        const auto before = device(h.session).value("data");
        const auto bad = h.session.submit("slave-replace-area", {{"unit", 17}, {"area", "holding"}, {"start", 65535}, {"values", QJsonArray{1, 2}}});
        QCOMPARE(h.session.operation(bad).update.state, "invalid"); QCOMPARE(device(h.session).value("data"), before);
        const auto range = h.session.submit("slave-replace-area", {{"unit", 17}, {"area", "input"}, {"start", 65535}, {"values", QJsonArray{65535}}});
        QCOMPARE(h.session.operation(range).update.state, "success");
        const auto invalidValue = h.session.submit("slave-edit-value", {{"unit", 17}, {"area", "coils"}, {"address", 19}, {"value", 1}});
        QCOMPARE(h.session.operation(invalidValue).update.state, "invalid");
        QCOMPARE(device(h.session).value("data").toObject().value("areas").toObject().value("input").toObject().value("start").toInt(), 65535);
    }
    void importValidationDecisionsAndBinding() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        edit(h.session, 42); const auto before = device(h.session);
        const auto imported = h.directory.filePath("import.json"); QVERIFY(save(imported, {{"version", 999}}));
        auto id = h.session.submit("slave-import", {{"unit", 17}, {"path", imported}});
        QCOMPARE(h.session.operation(id).update.state, "needs-decision");
        id = h.session.submit("slave-import", {{"unit", 17}, {"path", imported}, {"decision", "cancel"}});
        QCOMPARE(h.session.operation(id).update.state, "cancelled");
        id = h.session.submit("slave-import", {{"unit", 17}, {"path", imported}, {"decision", "discard"}});
        QTRY_VERIFY(h.session.operation(id).finished); QCOMPARE(h.session.operation(id).update.state, "file-error");
        QCOMPARE(device(h.session), before);
        QVERIFY(save(imported, document(900)));
        id = h.session.submit("slave-import", {{"unit", 17}, {"path", imported}, {"decision", "save"}});
        QTRY_VERIFY(h.session.operation(id).finished); QCOMPARE(h.session.operation(id).update.state, "success");
        QCOMPARE(holding(h.session), 900); QCOMPARE(holding(h.session, 18), 111);
        QCOMPARE(device(h.session).value("path").toString(), imported); QVERIFY(!device(h.session).value("dirty").toBool());
        const auto oldDocument = readFile(before.value("path").toString());
        QCOMPARE(oldDocument.value("areas").toObject().value("holding").toObject().value("values").toArray().first().toInt(), 42);
        edit(h.session, 901); QVERIFY(device(h.session).value("dirty").toBool()); QVERIFY(h.wire.writes.isEmpty());
        const auto other = device(h.session, 18).value("path").toString();
        id = h.session.submit("slave-save", {{"unit", 17}, {"path", other}});
        QCOMPARE(h.session.operation(id).update.state, "invalid");
    }
    void saveFailureSaveAsAndStoppedData() {
        Harness h(catalog); auto config = h.makeConfig(); QVERIFY(h.session.start(config)); QTRY_COMPARE(h.session.state(), SessionState::Running);
        edit(h.session, 42); const auto original = device(h.session).value("path").toString();
        auto id = h.session.submit("slave-save", {{"unit", 17}, {"path", h.directory.filePath("missing/device.json")}});
        QTRY_VERIFY(h.session.operation(id).finished); QCOMPARE(h.session.operation(id).update.state, "file-error");
        QVERIFY(device(h.session).value("dirty").toBool()); QCOMPARE(device(h.session).value("path").toString(), original);
        const auto target = h.directory.filePath("saved-as.json");
        id = h.session.submit("slave-save", {{"unit", 17}, {"path", target}});
        QTRY_VERIFY(h.session.operation(id).finished); QCOMPARE(h.session.operation(id).update.state, "success");
        QCOMPARE(readFile(target).value("note").toString(), QStringLiteral("保留未知字段")); QCOMPARE(device(h.session).value("path").toString(), target);
        h.session.stop(); QCOMPARE(holding(h.session), 42); edit(h.session, 99);
        QVERIFY(!h.session.start(h.session.config())); QCOMPARE(holding(h.session), 99); QCOMPARE(h.wire.opens, 1);
        id = h.session.submit("slave-discard", {{"unit", 17}}); QCOMPARE(h.session.operation(id).update.state, "success"); QCOMPARE(holding(h.session), 42);
        QVERIFY(h.session.start(h.session.config())); QTRY_COMPARE(h.session.state(), SessionState::Running); QCOMPARE(holding(h.session), 42);
        h.session.stop(); QVERIFY(QFile::remove(target)); QVERIFY(h.session.start(h.session.config()));
        QTRY_COMPARE(h.session.state(), SessionState::Error); QVERIFY(h.session.diagnostic().contains(target));
    }
    void saveSnapshotDoesNotClearNewEditsAndCompletesAfterStop() {
        QTemporaryDir directory; Wire wire; QList<std::function<void()>> jobs;
        DebugSession session(catalog, nullptr, wire.factory(), {}, [&](std::function<void()> job) { jobs.append(std::move(job)); });
        const auto path = directory.filePath("device.json"); QVERIFY(save(path, document()));
        QVERIFY(session.start(config({QJsonObject{{"unit", 17}, {"path", path}}})));
        QCOMPARE(jobs.size(), 1); jobs.takeFirst()(); QCoreApplication::processEvents(); QTRY_COMPARE(session.state(), SessionState::Running);
        edit(session, 42); const auto id = session.submit("slave-save", {{"unit", 17}}); QCOMPARE(jobs.size(), 1);
        QVERIFY(!session.operation(id).finished); edit(session, 99); jobs.takeFirst()(); QCoreApplication::processEvents();
        QCOMPARE(session.operation(id).update.state, "success"); QCOMPARE(holding(session), 99); QVERIFY(device(session).value("dirty").toBool());
        QCOMPARE(readFile(path).value("areas").toObject().value("holding").toObject().value("values").toArray().first().toInt(), 42);
        const auto next = session.submit("slave-save", {{"unit", 17}});
        wire.events.received(QByteArray::fromHex("1103006b00037687")); QCoreApplication::processEvents();
        QTest::qWait(10); session.poll(); QVERIFY(!wire.writes.isEmpty());
        QCOMPARE(wire.writes.last().second.first(9).toHex(), "110306006300000064");
        session.stop(); QVERIFY(!session.operation(next).finished);
        jobs.takeFirst()(); QCoreApplication::processEvents(); QCOMPARE(session.operation(next).update.state, "success");
        QVERIFY(!device(session).value("dirty").toBool()); QCOMPARE(holding(session), 99);
    }
    void importDuringEditDoesNotOverwriteNewData() {
        QTemporaryDir directory; Wire wire; QList<std::function<void()>> jobs;
        DebugSession session(catalog, nullptr, wire.factory(), {}, [&](std::function<void()> job) { jobs.append(std::move(job)); });
        const auto path = directory.filePath("device.json"), target = directory.filePath("target.json");
        QVERIFY(save(path, document())); QVERIFY(save(target, document(888)));
        QVERIFY(session.start(config({QJsonObject{{"unit", 17}, {"path", path}}}))); jobs.takeFirst()(); QCoreApplication::processEvents();
        QTRY_COMPARE(session.state(), SessionState::Running);
        const auto id = session.submit("slave-import", {{"unit", 17}, {"path", target}}); edit(session, 123);
        jobs.takeFirst()(); QCoreApplication::processEvents(); QCOMPARE(session.operation(id).update.state, "file-error");
        QCOMPARE(holding(session), 123); QCOMPARE(device(session).value("path").toString(), path);
    }
    void changedStartupConfigCannotBeOverwrittenByOldModel() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        edit(h.session, 42); h.session.stop(); auto next = h.session.config().protocol;
        next["customSetting"] = "preserve-new-config";
        QVERIFY(!h.session.configure(next)); // 无界面不能隐式丢弃修改。
        h.session.submit("slave-discard", {{"unit", 17}});
        const auto changed = h.directory.filePath("new-device.json"); QVERIFY(save(changed, document(777)));
        auto devices = next.value("devices").toArray(); auto first = devices[0].toObject(); first["path"] = changed; devices[0] = first; next["devices"] = devices;
        QVERIFY(h.session.configure(next)); QVERIFY(h.session.snapshot().isEmpty());
        const auto staleSave = h.session.submit("slave-save", {{"unit", 17}});
        QCOMPARE(h.session.operation(staleSave).update.state, "invalid"); QCOMPARE(h.session.config().protocol, next);
        QVERIFY(h.session.start(h.session.config())); QTRY_COMPARE(h.session.state(), SessionState::Running); QCOMPARE(holding(h.session), 777);
        QCOMPARE(h.session.config().protocol.value("customSetting").toString(), "preserve-new-config");
        h.session.stop(); edit(h.session, 999);
        int guardCalls = 0; bool nestedStarted = true;
        h.session.access().setDiscardGuard([&] {
            if (++guardCalls > 1) return false;
            nestedStarted = h.session.start(h.session.config());
            h.session.submit("slave-discard", {{"unit", 17}}); return true;
        });
        QVERIFY(h.session.start(h.session.config())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        QCOMPARE(guardCalls, 1); QVERIFY(!nestedStarted);
    }
    void widgetEditSaveAndReloadCancel() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        std::unique_ptr<QWidget> view(catalog->createView("modbus.rtu", nullptr, h.session.access())); view->resize(950, 850); view->show();
        qobject_cast<QTabWidget *>(view.get())->setCurrentIndex(1);
        auto *scroll = view->findChild<QScrollArea *>("slaveScroll");
        scroll->ensureWidgetVisible(view->findChild<QPushButton *>("slaveEditApply"));
        view->findChild<QSpinBox *>("slaveEditUnit")->setValue(17); view->findChild<QSpinBox *>("slaveEditAddress")->setValue(107);
        view->findChild<QLineEdit *>("slaveEditValue")->setText("42");
        QTest::mouseClick(view->findChild<QPushButton *>("slaveEditApply"), Qt::LeftButton); QCOMPARE(holding(h.session), 42);
        QTest::mouseClick(view->findChild<QPushButton *>("slaveSave"), Qt::LeftButton); QTRY_VERIFY(!device(h.session).value("dirty").toBool());
        edit(h.session, 77); h.session.stop();
        QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->done(QMessageBox::Cancel); });
        QVERIFY(!h.session.start(h.session.config())); QCOMPARE(holding(h.session), 77);
        QTimer::singleShot(0, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) QTest::mouseClick(box->button(QMessageBox::Save), Qt::LeftButton); });
        QVERIFY2(h.session.start(h.session.config()), qPrintable(h.session.diagnostic())); QTRY_COMPARE(h.session.state(), SessionState::Running); QCOMPARE(holding(h.session), 77);
        QTRY_COMPARE(view->findChild<QTableWidget *>("slaveData")->rowCount(), 8);
        QTRY_VERIFY(!view->findChild<QLabel *>("slaveDirtyStatus")->text().isEmpty());
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QDir().mkpath(evidence); scroll->ensureWidgetVisible(view->findChild<QTableWidget *>("slaveData")); QCoreApplication::processEvents();
            QVERIFY(view->grab().save(evidence + "/slave-edited-data-controlled.png"));
            scroll->ensureWidgetVisible(view->findChild<QLabel *>("slaveEditStatus")); QCoreApplication::processEvents();
            QVERIFY(view->grab().save(evidence + "/slave-edit-save-controlled.png"));
        }
    }
};
QTEST_MAIN(SlaveEditTests)
#include "slave_edit_tests.moc"
