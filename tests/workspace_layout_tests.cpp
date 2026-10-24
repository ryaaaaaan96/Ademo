#include "slave_test_support.h"
#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QFileDialog>
#include <QPointer>
#include <QTableWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QScreen>
#include <QTest>

namespace {
SessionConfig master(const QString &port) {
    auto result = config({}, port); result.role = "master";
    result.protocol = {{"parameterRows", QJsonArray{
        QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107}},
        QJsonObject{{"unit", 18}, {"area", "holding"}, {"address", 108}}}}};
    return result;
}
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *item : root.findChildren<QPushButton *>()) if (item->text() == text) return item;
    return nullptr;
}
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *item : root.findChildren<T *>()) if (item->accessibleName() == name) return item;
    return nullptr;
}
int runtimeValue(DebugSession *session) {
    return session->snapshot()["devices"].toArray()[0].toObject()["data"].toObject()["areas"].toObject()["holding"].toObject()["values"].toArray()[0].toInt();
}
void editRuntime(DebugSession *session, int value) {
    session->submit("slave-edit-value", {{"unit",17},{"area","holding"},{"address",107},{"value",value}});
}
}
class WorkspaceLayoutTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void mixedWindowLayoutRestoresBeforeFirstShowWithoutOpeningPorts() {
        QTemporaryDir directory; Wire wire;
        Workbench source(catalog, nullptr, wire.factory()); source.resize(1300, 850); source.show();
        source.addSession(master("COM231"), "second");
        source.addSession(master("COM232"), "floating");
        source.addSession(master("COM233"), "closed");
        source.tileSessions(Qt::Horizontal); source.floatSession(2);
        QVERIFY(source.closeSession(3)); QVERIFY(source.selectDevice(2, "18"));
        auto *floating = source.sessionWindow(2); floating->resize(650, 550); floating->move(120, 90);
        QTRY_COMPARE(floating->size(), QSize(650, 550));
        const auto size = floating->size(); const auto position = floating->pos();
        const auto path = directory.filePath("layout.json");
        auto id = source.saveWorkspace(path); QTRY_COMPARE(source.workspaceOperation(id).state, QString("success"));
        Workbench restored(catalog, nullptr, wire.factory());
        id = restored.openWorkspace(path); QTRY_COMPARE(restored.workspaceOperation(id).state, QString("success"));
        QCOMPARE(restored.sessionCount(), 4);
        QVERIFY(!restored.isSessionFloating(0)); QVERIFY(!restored.isSessionFloating(1));
        QVERIFY(restored.isSessionFloating(2)); QVERIFY(!restored.isSessionOpen(3));
        QCOMPARE(restored.parameterView(2)->selectedDevice(), QString("18"));
        restored.show();
        QTRY_COMPARE(restored.sessionWindow(2)->size(), size);
        QTRY_VERIFY((restored.sessionWindow(2)->pos() - position).manhattanLength() <= 2);
        QVERIFY(restored.sessionWindow(0)->mapToGlobal(QPoint()).x() < restored.sessionWindow(1)->mapToGlobal(QPoint()).x());
        QVERIFY(!restored.isSessionOpen(3)); QCOMPARE(wire.opens, 0);
        for (int i = 0; i < restored.sessionCount(); ++i) QCOMPARE(restored.session(i)->state(), SessionState::Stopped);
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(restored.grab().save(evidence + "/workspace-restored-layout.png"));
            QVERIFY(restored.sessionWindow(2)->grab().save(evidence + "/workspace-restored-floating.png"));
        }
    }
    void restoreLayoutKeepsCurrentSessionsAndCommunication() {
        QTemporaryDir directory; Wire wire; Workbench window(catalog, nullptr, wire.factory());
        window.resize(1200, 800); window.show();
        const int index = window.addSession(master("COM234"), "working");
        window.floatSession(index); window.sessionWindow(index)->resize(640, 560); window.sessionWindow(index)->move(80, 70);
        const auto expected = window.sessionWindow(index)->pos();
        QVERIFY(window.closeSession(index));
        const auto id = window.saveWorkspace(directory.filePath("layout.json"));
        QTRY_COMPARE(window.workspaceOperation(id).state, QString("success"));
        window.selectSession(index); auto *session = window.session(index);
        QVERIFY(session->start(window.sessionConfiguration(index))); QTRY_COMPARE(session->state(), SessionState::Running);
        window.dockSession(index); auto *restore = button(window, QStringLiteral("恢复已保存布局"));
        QVERIFY2(restore, "应有不重载配置的恢复布局入口"); QTest::mouseClick(restore, Qt::LeftButton);
        QVERIFY(window.isSessionFloating(index)); QVERIFY(window.isSessionOpen(index));
        QCOMPARE(window.session(index), session); QCOMPARE(session->state(), SessionState::Running); QCOMPARE(wire.opens, 1);
        QTRY_VERIFY((window.sessionWindow(index)->pos() - expected).manhattanLength() <= 2);
    }
    void invalidLayoutCannotReplaceRunningWorkspace_data() {
        QTest::addColumn<QJsonObject>("layout");
        QTest::newRow("invalid-base64") << QJsonObject{{"dockState", "%%%"}};
        QTest::newRow("invalid-qt-state") << QJsonObject{{"dockState", "YWJjZA=="}};
        QTest::newRow("invalid-geometry") << QJsonObject{{"geometry", "YWJjZA=="}};
    }
    void invalidLayoutCannotReplaceRunningWorkspace() {
        QFETCH(QJsonObject, layout);
        QTemporaryDir directory; Wire wire; Workbench window(catalog, nullptr, wire.factory());
        auto *original = window.session(0); QVERIFY(original->start(master("COM235")));
        QTRY_COMPARE(original->state(), SessionState::Running);
        const auto path = directory.filePath("invalid-layout.json");
        QVERIFY(save(path, {{"version", 1}, {"sessions", QJsonArray{}}, {"layout", layout}}));
        const auto id = window.openWorkspace(path);
        QTRY_VERIFY(window.workspaceOperation(id).state != "working");
        QCOMPARE(window.workspaceOperation(id).state, QString("file-error"));
        QCOMPARE(window.session(0), original); QCOMPARE(original->state(), SessionState::Running); QCOMPARE(wire.opens, 1);
    }
    void workspacePickerBlocksQueuedReplacement() {
        QTemporaryDir directory; const auto path = directory.filePath("empty.json");
        QVERIFY(save(path, {{"version", 1}, {"sessions", QJsonArray{}}}));
        QList<std::function<void()>> jobs;
        Workbench window(catalog, nullptr, {}, {}, [&](auto job) { jobs.append(std::move(job)); }); window.show();
        auto *original = window.session(0); const auto id = window.openWorkspace(path); QVERIFY(id);
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, &timer, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                if (!jobs.isEmpty()) { auto job = jobs.takeFirst(); job(); }
                else if (window.workspaceOperation(id).state != "working") { dialog->reject(); timer.stop(); }
            }
        }); timer.start();
        QTest::mouseClick(button(window, QStringLiteral("保存 / 另存工作区")), Qt::LeftButton);
        QCOMPARE(window.workspaceOperation(id).state, QString("cancelled"));
        QCOMPARE(window.session(0), original); QCOMPARE(window.sessionCount(), 1);
    }
    void cancelledReplacementKeepsEachKindOfModification_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("pending") << QString("pending");
        QTest::newRow("file-draft") << QString("file");
        QTest::newRow("runtime") << QString("runtime");
    }
    void cancelledReplacementKeepsEachKindOfModification() {
        QFETCH(QString, kind);
        QTemporaryDir directory; Wire wire; Workbench window(catalog, nullptr, wire.factory()); window.show();
        const auto device = directory.filePath("device.json"), empty = directory.filePath("empty.json");
        QVERIFY(save(device, document())); QVERIFY(save(empty, {{"version",1},{"sessions",QJsonArray{}}}));
        const int index = window.addSession(kind == "runtime" ? config({QJsonObject{{"unit",17},{"path",device}}}, "COM236") : master("COM236"));
        auto *session = window.session(index); auto *view = window.parameterView(index);
        QTableWidget *table = nullptr;
        if (kind == "runtime") {
            QVERIFY(session->start(window.sessionConfiguration(index))); QTRY_COMPARE(session->state(), SessionState::Running);
            editRuntime(session, 888); QCOMPARE(runtimeValue(session), 888);
        } else if (kind == "file") {
            accessible<QTabWidget>(*view, QStringLiteral("参数页面"))->setCurrentIndex(1);
            table = accessible<QTableWidget>(*view, QStringLiteral("参数文件内容")); QVERIFY(table);
            table->item(0, 0)->setText("unsaved-file");
        } else {
            table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QVERIFY(table);
            const auto point = table->visualItemRect(table->item(0, 3)).center();
            QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, point); QTest::mouseDClick(table->viewport(), Qt::LeftButton, {}, point);
            QLineEdit *editor = nullptr;
            for (auto *candidate : table->findChildren<QLineEdit *>()) if (candidate->isVisibleTo(table)) editor = candidate;
            QVERIFY(editor); editor->selectAll(); QTest::keyClicks(editor, "123"); QTest::keyClick(editor, Qt::Key_Return);
        }
        QVERIFY(session->hasUnsavedOrBusy());
        QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, &answer, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { box->button(QMessageBox::Cancel)->click(); answer.stop(); }
        }); answer.start();
        const auto id = window.openWorkspace(empty); QTRY_COMPARE(window.workspaceOperation(id).state, QString("cancelled"));
        QCOMPARE(window.session(index), session); QVERIFY(session->hasUnsavedOrBusy()); QVERIFY(window.isSessionOpen(index));
        if (kind == "runtime") { QCOMPARE(runtimeValue(session), 888); QCOMPARE(session->state(), SessionState::Running); }
        else QCOMPARE(table->item(0, kind == "file" ? 0 : 3)->text(), kind == "file" ? QString("unsaved-file") : QString("123"));
    }
    void laterFilePromptCannotDiscardNewDataInEarlierWindow() {
        QTemporaryDir directory; Wire wire; Workbench window(catalog, nullptr, wire.factory()); window.show();
        const auto device = directory.filePath("device.json"), empty = directory.filePath("empty.json");
        QVERIFY(save(device, document())); QVERIFY(save(empty, {{"version",1},{"sessions",QJsonArray{}}}));
        const int first = window.addSession(config({QJsonObject{{"unit",17},{"path",device}}}, "COM237"));
        auto *slave = window.session(first); QVERIFY(slave->start(window.sessionConfiguration(first)));
        QTRY_COMPARE(slave->state(), SessionState::Running); editRuntime(slave, 111);
        const int second = window.addSession(master("COM238")); auto *view = window.parameterView(second);
        accessible<QTabWidget>(*view, QStringLiteral("参数页面"))->setCurrentIndex(1);
        accessible<QTableWidget>(*view, QStringLiteral("参数文件内容"))->item(0,0)->setText("later-draft");
        QTimer answer; answer.setInterval(5); QPointer<QMessageBox> answered; bool injected = false; int prompts = 0;
        connect(&answer, &QTimer::timeout, &answer, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box == answered) return;
            answered = box; ++prompts;
            if (box->windowTitle() == QStringLiteral("未保存文件草稿")) { editRuntime(slave, 999); injected = true; }
            box->button(QMessageBox::Discard)->click();
        }); answer.start();
        const auto id = window.openWorkspace(empty); QTRY_COMPARE(window.workspaceOperation(id).state, QString("cancelled")); answer.stop();
        QVERIFY(injected); QCOMPARE(prompts, 2); QCOMPARE(window.session(first), slave);
        QCOMPARE(runtimeValue(slave), 999); QVERIFY(slave->hasUnsavedOrBusy()); QCOMPARE(slave->state(), SessionState::Running);
        QCOMPARE(window.sessionCount(), 3);
    }
    void workspacePickerCanOutliveItsDeletedOwner() {
        auto *window = new Workbench(catalog); QPointer<Workbench> alive(window); window->show();
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, &timer, [&] {
            if (qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) { timer.stop(); delete window; }
        }); timer.start();
        QTest::mouseClick(button(*window, QStringLiteral("保存 / 另存工作区")), Qt::LeftButton);
        QVERIFY(alive.isNull());
    }
    void restoredReferencesAndPollingDoNotHideMissingSlaveFiles() {
        QTemporaryDir directory; QVERIFY(QDir(directory.path()).mkdir("data"));
        const auto slavePath = directory.filePath("data/slave.json"), parameterPath = directory.filePath("data/parameters.json");
        QVERIFY(save(slavePath, document(777)));
        QVERIFY(save(parameterPath, {{"kind","modbus-parameter-definition"},{"version",1},{"parameters",QJsonArray{}}}));
        auto configuration = master("COM239");
        const QJsonArray polls{QJsonObject{{"unit",18},{"action","read-holding"},{"start",108},{"count",1},{"intervalMs",500},{"parameterGroup","pressure"}}};
        configuration.protocol["polls"] = polls;
        configuration.protocol["parameterFiles"] = QJsonObject{{"18",parameterPath}};
        auto rows = configuration.protocol["parameterRows"].toArray(); auto row = rows[1].toObject();
        row["name"] = "pressure"; row["group"] = "pressure"; rows[1] = row; configuration.protocol["parameterRows"] = rows;
        Wire wire; Workbench source(catalog, nullptr, wire.factory());
        const int main = source.addSession(configuration), slave = source.addSession(config({QJsonObject{{"unit",17},{"path",slavePath}}}, "COM240"));
        const auto path = directory.filePath("workspace.json"); auto id = source.saveWorkspace(path);
        QTRY_COMPARE(source.workspaceOperation(id).state, QString("success"));
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly)); const auto document = QJsonDocument::fromJson(saved.readAll()).object(); saved.close();
        const auto savedSlave = document["sessions"].toArray()[slave].toObject()["configuration"].toObject()["protocol"].toObject();
        QCOMPARE(savedSlave["devices"].toArray()[0].toObject()["path"].toString(), QString("data/slave.json"));
        QVERIFY(!savedSlave["devices"].toArray()[0].toObject().contains("data")); QVERIFY(QFile::remove(slavePath));
        Workbench restored(catalog, nullptr, wire.factory()); id = restored.openWorkspace(path);
        QTRY_COMPARE(restored.workspaceOperation(id).state, QString("success")); QCOMPARE(wire.opens, 0);
        QCOMPARE(restored.sessionConfiguration(main).protocol["polls"], QJsonValue(polls));
        QCOMPARE(restored.sessionConfiguration(main).protocol["parameterRows"], QJsonValue(rows));
        QCOMPARE(restored.sessionConfiguration(main).protocol["parameterFiles"].toObject()["18"].toString(), parameterPath);
        QVERIFY(restored.session(slave)->start(restored.sessionConfiguration(slave)));
        QTRY_COMPARE(restored.session(slave)->state(), SessionState::Error); QCOMPARE(wire.opens, 0);
        QVERIFY(restored.session(main)->start(restored.sessionConfiguration(main)));
        QTRY_COMPARE(restored.session(main)->state(), SessionState::Running); QCOMPARE(wire.opens, 1);
    }
    void unavailableScreenGeometryReturnsToVisibleDesktop() {
        QTemporaryDir directory; Workbench source(catalog); source.show();
        source.sessionWindow(0)->resize(640, 550); source.sessionWindow(0)->move(-20000, -20000);
        const auto path = directory.filePath("off-desktop.json"); auto id = source.saveWorkspace(path);
        QTRY_COMPARE(source.workspaceOperation(id).state, QString("success"));
        Workbench alreadyVisible(catalog); alreadyVisible.show();
        id = alreadyVisible.openWorkspace(path);
        QTRY_COMPARE(alreadyVisible.workspaceOperation(id).state, QString("success"));
        auto *visibleWindow = alreadyVisible.sessionWindow(0);
        QVERIFY(visibleWindow->screen()->availableGeometry().contains(visibleWindow->frameGeometry().topLeft()));
        Workbench restored(catalog); id = restored.openWorkspace(path);
        QTRY_COMPARE(restored.workspaceOperation(id).state, QString("success")); restored.show();
        auto *window = restored.sessionWindow(0);
        QTRY_VERIFY(window->screen()->availableGeometry().intersects(window->frameGeometry()));
        QVERIFY(window->screen()->availableGeometry().contains(window->frameGeometry().topLeft()));
        QVERIFY(source.closeSession(0));
        const auto closedPath = directory.filePath("closed-off-desktop.json");
        id = source.saveWorkspace(closedPath);
        QTRY_COMPARE(source.workspaceOperation(id).state, QString("success"));
        Workbench closed(catalog); id = closed.openWorkspace(closedPath);
        QTRY_COMPARE(closed.workspaceOperation(id).state, QString("success")); closed.show();
        QVERIFY(!closed.isSessionOpen(0));
        closed.selectSession(0);
        QTRY_VERIFY(closed.isSessionOpen(0));
        QVERIFY(closed.sessionWindow(0)->screen()->availableGeometry().contains(closed.sessionWindow(0)->frameGeometry().topLeft()));
    }
};
QTEST_MAIN(WorkspaceLayoutTests)
#include "workspace_layout_tests.moc"
