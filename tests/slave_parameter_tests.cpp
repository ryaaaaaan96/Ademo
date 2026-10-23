#include "slave_test_support.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QSpinBox>
#include <QTest>
#include <QPointer>

namespace {
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *widget : root.findChildren<T *>())
        if (widget->accessibleName() == name && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *widget : root.findChildren<QPushButton *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QJsonObject device(const DebugSession &session, int unit = 17) {
    for (const auto &value : session.snapshot()["devices"].toArray())
        if (value.toObject()["unit"].toInt() == unit) return value.toObject();
    return {};
}
int holding(const DebugSession &session, int unit = 17) {
    return device(session, unit)["data"].toObject()["areas"].toObject()["holding"].toObject()["values"].toArray()[0].toInt();
}
void changeCell(QTableWidget &table, int row, const QString &header, const QString &value, Qt::Key finish = Qt::Key_Return) {
    int column = -1;
    for (int i = 0; i < table.columnCount(); ++i) if (table.horizontalHeaderItem(i)->text() == header) column = i;
    QVERIFY(column >= 0); QVERIFY(table.item(row, column));
    table.scrollToItem(table.item(row, column));
    const auto point = table.visualItemRect(table.item(row, column)).center();
    QTest::mouseClick(table.viewport(), Qt::LeftButton, {}, point); QTest::mouseDClick(table.viewport(), Qt::LeftButton, {}, point);
    QCoreApplication::processEvents();
    for (auto *edit : table.findChildren<QLineEdit *>()) if (edit->isVisibleTo(&table)) {
        edit->selectAll(); QTest::keyClicks(edit, value); QTest::keyClick(edit, finish);
        QCoreApplication::processEvents(); return;
    }
    QFAIL("必须能够通过真实单元格编辑器输入");
}
std::shared_ptr<bool> answerNext(QMessageBox::StandardButton choice) {
    auto answered=std::make_shared<bool>(false);
    auto *timer = new QTimer(qApp); timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, choice, answered] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            *answered=true; box->button(choice)->click(); timer->stop(); timer->deleteLater();
        }
    }); timer->start(); return answered;
}
}
class SlaveParameterTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void roleAndConfiguredDevicesAreVisibleBeforeStarting() {
        Harness h(catalog); auto configuration = h.makeConfig();
        Workbench window(catalog, nullptr, h.wire.factory());
        const int index = window.addSession(configuration);
        auto *view = window.parameterView(index); QVERIFY(view);
        QTRY_VERIFY(accessible<QTableWidget>(*view, QStringLiteral("从站模拟参数")));
        QCOMPARE(view->devices().size(), 2);
        QVERIFY(view->selectDevice("18")); QCOMPARE(view->selectedDevice(), QString("18"));
        QCOMPARE(window.session(index)->state(), SessionState::Stopped); QCOMPARE(h.wire.opens, 0);
    }
    void runtimeCellEditIsImmediateAndSurvivesStop() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, h.session.access()));
        view->resize(1100, 700); view->show();
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("从站模拟参数")); QVERIFY(table);
        QTRY_COMPARE(table->rowCount(), 3);
        changeCell(*table, 0, QStringLiteral("值"), "42");
        QTRY_COMPARE(holding(h.session), 42); QCOMPARE(holding(h.session,18),111); QVERIFY(h.wire.writes.isEmpty());
        changeCell(*table,0,QStringLiteral("值"),"65536");
        QTRY_VERIFY(accessible<QLabel>(*view,QStringLiteral("模拟值操作状态"))->text().contains("0..65535")); QCOMPARE(holding(h.session),42);
        QTRY_COMPARE(table->item(0,3)->text(),QString("42"));
        changeCell(*table,0,QStringLiteral("值"),"99",Qt::Key_Escape); QCOMPARE(holding(h.session),42);
        h.receive(QByteArray::fromHex("1103006b00037687")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.first(9).toHex(), "110306002a00000064"); h.complete();
        const auto evidence=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(view->grab().save(evidence+"/slave-runtime-parameters.png")); }
        h.session.stop(); QCoreApplication::processEvents();
        changeCell(*table, 0, QStringLiteral("值"), "43"); QTRY_COMPARE(holding(h.session), 43);
        QVERIFY(device(h.session)["dirty"].toBool());
    }
    void fileSaveAndApplyRemainSeparateFromRuntime() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(), SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, h.session.access()));
        view->resize(1100,700); view->show();
        h.session.submit("slave-edit-value", {{"unit",17},{"area","holding"},{"address",107},{"value",42}});
        auto *tabs = accessible<QTabWidget>(*view, QStringLiteral("参数页面")); QVERIFY(tabs); tabs->setCurrentIndex(1);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("从站文件内容")); QVERIFY(table);
        QCOMPARE(table->rowCount(),3); changeCell(*table,0,QStringLiteral("值"),"777");
        const auto path = h.directory.filePath("edited.json");
        accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(path);
        button(*view,QStringLiteral("保存文件"))->click();
        QTRY_VERIFY(QFile::exists(path));
        auto *apply = button(*view,QStringLiteral("应用到目标设备")); QTRY_VERIFY(apply->isEnabled());
        const auto evidence=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(view->grab().save(evidence+"/slave-file-editor.png")); }
        QCOMPARE(holding(h.session),42); QVERIFY(h.wire.writes.isEmpty());
        answerNext(QMessageBox::Cancel); QTest::mouseClick(apply,Qt::LeftButton); QCOMPARE(holding(h.session),42);
        answerNext(QMessageBox::Discard); QTest::mouseClick(apply,Qt::LeftButton); QTRY_COMPARE(holding(h.session),777);
        QCOMPARE(holding(h.session,18),111); QCOMPARE(device(h.session)["path"].toString(),path);
        QVERIFY(!device(h.session)["dirty"].toBool()); QVERIFY(h.wire.writes.isEmpty());
    }
    void createExternalFileAndBindBeforeStarting() {
        QTemporaryDir directory; Wire wire; Workbench window(catalog,nullptr,wire.factory());
        const auto index=window.addSession(config()); auto *view=window.parameterView(index); QVERIFY(view);
        view->resize(1100,750); view->show();
        auto *add=button(*view,QStringLiteral("添加模拟设备")); QVERIFY(add);
        accessible<QSpinBox>(*view,QStringLiteral("新增模拟设备地址"))->setValue(17); QTest::mouseClick(add,Qt::LeftButton);
        QCOMPARE(view->selectedDevice(),QString("17"));
        accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        QVERIFY2(button(*view,QStringLiteral("保存文件"))->isEnabled(),qPrintable(accessible<QLabel>(*view,QStringLiteral("文件目标设备"))->text()));
        accessible<QSpinBox>(*view,QStringLiteral("文件范围起址"))->setValue(107);
        accessible<QSpinBox>(*view,QStringLiteral("文件范围数量"))->setValue(1);
        changeCell(*accessible<QTableWidget>(*view,QStringLiteral("从站文件内容")),0,QStringLiteral("值"),"555");
        const auto path=directory.filePath("created.json"); accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(path);
        button(*view,QStringLiteral("保存文件"))->click();
        auto *apply=button(*view,QStringLiteral("应用到目标设备")); QTRY_VERIFY2(apply->isEnabled(),qPrintable(accessible<QLabel>(*view,QStringLiteral("文件状态"))->text())); apply->click();
        QCOMPARE(window.sessionConfiguration(index).protocol["devices"].toArray()[0].toObject()["path"].toString(),path);
        QCOMPARE(wire.opens,0); QVERIFY(window.session(index)->start(window.sessionConfiguration(index)));
        QTRY_COMPARE(window.session(index)->state(),SessionState::Running); QCOMPARE(holding(*window.session(index)),555);
    }
    void roleCancellationKeepsPageAndRuntimeThenDiscardRemovesGuard() {
        Harness h(catalog); const auto configuration=h.makeConfig(); Workbench window(catalog,nullptr,h.wire.factory());
        const auto index=window.addSession(configuration); auto *session=window.session(index); QVERIFY(session->start(configuration)); QTRY_COMPARE(session->state(),SessionState::Running);
        session->submit("slave-edit-value",{{"unit",17},{"area","holding"},{"address",107},{"value",42}}); session->stop();
        auto *view=window.parameterView(index); QPointer<ParameterView> old(view);
        QComboBox *role=nullptr;
        for(auto *combo:window.sessionView(index)->findChildren<QComboBox *>()) if(combo->accessibleName()==QStringLiteral("会话角色")) role=combo;
        QVERIFY(role); const auto cancelled=answerNext(QMessageBox::Cancel); role->setCurrentIndex(role->findData("master"));
        QTRY_VERIFY(*cancelled); QTRY_COMPARE(role->currentData().toString(),QString("slave")); QCOMPARE(window.parameterView(index),view); QCOMPARE(holding(*session),42);
        answerNext(QMessageBox::Discard); role->setCurrentIndex(role->findData("master")); QTRY_VERIFY(old.isNull());
        QCOMPARE(window.sessionConfiguration(index).role,QString("master")); QVERIFY(!session->hasUnsavedOrBusy()); QCOMPARE(session->state(),SessionState::Stopped);
        QVERIFY(window.parameterView(index));
    }
    void runtimeSaveKeepsNewExternalWriteAndFileDraft() {
        QTemporaryDir directory; Wire wire; QList<std::function<void()>> jobs; qint64 now=0;
        DebugSession session(catalog,nullptr,wire.factory(),[&]{return now;},[&](std::function<void()> job){jobs.append(std::move(job));});
        const auto path=directory.filePath("device.json"); QVERIFY(save(path,document()));
        QVERIFY(session.start(config({QJsonObject{{"unit",17},{"path",path}}}))); jobs.takeFirst()(); QCoreApplication::processEvents(); QTRY_COMPARE(session.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,session.access())); view->resize(1100,800); view->show();
        auto *tabs=accessible<QTabWidget>(*view,QStringLiteral("参数页面")); tabs->setCurrentIndex(1);
        auto *draft=accessible<QTableWidget>(*view,QStringLiteral("从站文件内容")); changeCell(*draft,0,QStringLiteral("值"),"777"); tabs->setCurrentIndex(0);
        changeCell(*accessible<QTableWidget>(*view,QStringLiteral("从站模拟参数")),0,QStringLiteral("值"),"42");
        QTRY_COMPARE(holding(session),42);
        answerNext(QMessageBox::Yes); button(*view,QStringLiteral("保存运行值（不保存文件草稿）"))->click(); QCOMPARE(jobs.size(),1);
        // 外部请求写保持寄存器107=123，CRC由独立固定帧给出。
        wire.events.received(QByteArray::fromHex("1106006b007bbaa5")); QCoreApplication::processEvents(); now+=1750; session.poll(); QCoreApplication::processEvents();
        QTRY_COMPARE(holding(session),123); session.stop(); jobs.takeFirst()(); QCoreApplication::processEvents();
        QVERIFY(device(session)["dirty"].toBool()); QCOMPARE(holding(session),123);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto saved=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(saved["areas"].toObject()["holding"].toObject()["values"].toArray()[0].toInt(),42);
        tabs->setCurrentIndex(1); QCOMPARE(draft->item(0,1)->text(),QString("777")); QVERIFY(session.hasUnsavedOrBusy());
    }
    void fileChangesAndCrossDeviceSaveAreRejectedWithoutReplacingRuntime() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,h.session.access())); view->resize(1100,800); view->show();
        accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        const auto original=device(h.session)["path"].toString(); QVERIFY(save(original,document(999)));
        button(*view,QStringLiteral("应用到目标设备"))->click();
        QTRY_VERIFY(!h.session.hasUnsavedOrBusy()); QCOMPARE(holding(h.session),555);
        const auto other=device(h.session,18)["path"].toString(); accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(other);
        changeCell(*accessible<QTableWidget>(*view,QStringLiteral("从站文件内容")),0,QStringLiteral("值"),"888");
        button(*view,QStringLiteral("保存文件"))->click();
        QVERIFY(accessible<QLabel>(*view,QStringLiteral("文件状态"))->text().contains(QStringLiteral("另一")));
        QFile file(other); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["areas"].toObject()["holding"].toObject()["values"].toArray()[0].toInt(),111);
        QCOMPARE(holding(h.session,18),111);
    }
    void fullFileBudgetAndPagedValuesRoundTrip() {
        Harness h(catalog); auto configuration=h.makeConfig(); const auto path=configuration.protocol["devices"].toArray()[0].toObject()["path"].toString();
        auto content=document(); content["vendorPadding"]=QString(1100000,'x');
        auto areas=content["areas"].toObject(); auto holdingArea=areas["holding"].toObject(); QJsonArray values;
        for(int i=0;i<300;++i) values.append(1000+i); holdingArea["values"]=values; areas["holding"]=holdingArea; content["areas"]=areas;
        QVERIFY(save(path,content)); QVERIFY(h.session.start(configuration)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,h.session.access())); view->resize(1100,800); view->show();
        auto *table=accessible<QTableWidget>(*view,QStringLiteral("从站模拟参数")); QCOMPARE(table->rowCount(),256);
        accessible<QSpinBox>(*view,QStringLiteral("模拟显示偏移"))->setValue(256); QCOMPARE(table->rowCount(),44); QCOMPARE(table->item(0,0)->text(),QString("PDU 363"));
        auto *tabs=accessible<QTabWidget>(*view,QStringLiteral("参数页面")); tabs->setCurrentIndex(1);
        const auto copy=h.directory.filePath("large-copy.json"); accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(copy);
        button(*view,QStringLiteral("保存文件"))->click(); QTRY_VERIFY(QFile::exists(copy)); QTRY_VERIFY(button(*view,QStringLiteral("应用到目标设备"))->isEnabled());
        QFile file(copy); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); QVERIFY(bytes.size()>1024*1024); QCOMPARE(QJsonDocument::fromJson(bytes).object(),content); file.close();
        auto replacement=content; replacement["note"]="new-file"; QVERIFY(save(copy,replacement));
        button(*view,QStringLiteral("打开文件"))->click(); QTRY_VERIFY(button(*view,QStringLiteral("应用到目标设备"))->isEnabled());
        button(*view,QStringLiteral("应用到目标设备"))->click(); QTRY_VERIFY(!h.session.hasUnsavedOrBusy());
        QCOMPARE(device(h.session)["data"].toObject(),replacement); QCOMPARE(holding(h.session,18),111);
    }
    void dirtyDialogNewWriteAndDestroyedPageKeepOtherSessionSafe() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,h.session.access())); view->resize(1100,800); view->show();
        h.session.submit("slave-edit-value",{{"unit",17},{"area","holding"},{"address",107},{"value",42}});
        QTimer::singleShot(0,[&]{
            auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
            h.session.submit("slave-edit-value",{{"unit",17},{"area","holding"},{"address",107},{"value",999}}); box->button(QMessageBox::Discard)->click();
        });
        QVERIFY(!h.session.resolveUnsaved()); QCOMPARE(holding(h.session),999); QCOMPARE(holding(h.session,18),111);
        QTimer::singleShot(0,[&]{ view.reset(); });
        QVERIFY(!h.session.resolveUnsaved()); QCOMPARE(holding(h.session),999); QCOMPARE(holding(h.session,18),111);
    }
};
QTEST_MAIN(SlaveParameterTests)
#include "slave_parameter_tests.moc"
