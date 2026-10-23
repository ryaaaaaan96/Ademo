#include "slave_test_support.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
namespace {
QJsonObject entry(QString protocol = "modbus.rtu", QString role = "master", QJsonObject data = {}) {
 return {{"title", "设备配置"}, {"futureSession", 42}, {"configuration", QJsonObject{
 {"protocolId", protocol}, {"transportId", "serial"}, {"role", role}, {"futureConfig", "opaque"},
 {"endpoint", QJsonObject{{"port", "COM120"}, {"baud", "115200"}, {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}, {"futureEndpoint", 99}}},
 {"protocol", data}, {"recovery", QJsonObject{{"autoReconnect", true}, {"intervalMs", 20}, {"futureRecovery", "kept"}}}}}};
}
QJsonObject workspace(QJsonArray sessions) {return {{"version",1},{"sessions",sessions},{"futureRoot",QJsonArray{1,2}}, {"layout",QJsonObject{{"futureLayout",true},{"activeSession",0}}}};}
QJsonObject read(const QString &path) {QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(f.readAll()).object();}
QWidget *pane(Workbench &w,int i=0) {return w.findChild<QTabWidget *>("sessionTabs")->widget(i);}
}
class WorkspaceTests : public QObject {
 Q_OBJECT
 std::shared_ptr<PluginCatalog> catalog;
private slots:
 void initTestCase() {catalog=std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath()+"/plugins"); QVERIFY(catalog->protocol("modbus.rtu"));}
 void configurationAndUnknownFieldsRoundTrip() {
 QTemporaryDir dir; auto source=dir.filePath("original.json"),target=dir.filePath("saved.json");
 auto data=workspace({entry("modbus.rtu","master",{{"futureProtocol",QJsonArray{3,4}},{"polls",QJsonArray{QJsonObject{{"unit",1},{"action","read-holding"},{"start",0},{"count",1},{"intervalMs",100}}}}}),entry("missing.protocol")});
 QVERIFY(save(source,data)); Wire wire; Workbench w(catalog,nullptr,wire.factory()); auto id=w.openWorkspace(source); QVERIFY(id); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success"));
 QCOMPARE(w.sessionCount(),2); QCOMPARE(wire.opens,0); QCOMPARE(w.session(0)->state(),SessionState::Stopped);
 QVERIFY(!pane(w,1)->findChild<QPushButton *>("start")->isEnabled()); QVERIFY(pane(w,1)->findChild<QLabel *>("status")->text().contains(QStringLiteral("不可用")));
 pane(w)->findChild<QLineEdit *>("port")->setText("COM121"); w.selectSession(1); w.resize(1080,810); w.show();
 id=w.saveWorkspace(target); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success")); auto result=read(target);
 QCOMPARE(result.value("futureRoot"),data.value("futureRoot")); QCOMPARE(result["layout"].toObject()["futureLayout"],QJsonValue(true)); QCOMPARE(result["layout"].toObject()["activeSession"].toInt(),1);
 auto item=result["sessions"].toArray()[0].toObject(), c=item["configuration"].toObject(); QCOMPARE(item["futureSession"].toInt(),42); QCOMPARE(c["futureConfig"].toString(),QString("opaque"));
 QCOMPARE(c["endpoint"].toObject()["port"].toString(),QString("COM121")); QCOMPARE(c["endpoint"].toObject()["futureEndpoint"].toInt(),99); QCOMPARE(c["protocol"].toObject()["futureProtocol"],QJsonValue(QJsonArray{3,4})); QCOMPARE(c["recovery"].toObject()["futureRecovery"].toString(),QString("kept"));
 Workbench restored(catalog,nullptr,wire.factory()); id=restored.openWorkspace(target); QTRY_COMPARE(restored.workspaceOperation(id).state,QString("success")); QCOMPARE(restored.currentSessionIndex(),1); QCOMPARE(restored.size(),w.size()); QCOMPARE(wire.opens,0);
 auto output=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR"); if(!output.isEmpty()) {QTest::qWait(100); QVERIFY(w.grab().save(output+"/workspace-restored-missing-plugin.png"));}
 }
 void externalReferencesAndPluginRepair() {
 QTemporaryDir dir; QDir base(dir.path()); QVERIFY(base.mkdir("a")); QVERIFY(base.mkdir("b")); auto a=base.filePath("a"),b=base.filePath("b"); QVERIFY(save(a+"/device.json",document(777)));
 auto data=workspace({entry("modbus.rtu","slave",{{"devices",QJsonArray{QJsonObject{{"unit",17},{"path","device.json"},{"futureDevice",true}}}}})}); QVERIFY(save(a+"/workspace.json",data));
 auto absent=std::make_shared<PluginCatalog>(); absent->discover(dir.filePath("absent")); Wire wire;
 Workbench unavailable(absent,nullptr,wire.factory()); auto id=unavailable.openWorkspace(a+"/workspace.json"); QTRY_COMPARE(unavailable.workspaceOperation(id).state,QString("success"));
 id=unavailable.saveWorkspace(b+"/opaque.json"); QTRY_COMPARE(unavailable.workspaceOperation(id).state,QString("success")); auto opaque=read(b+"/opaque.json")["sessions"].toArray()[0].toObject(); QCOMPARE(opaque["referenceBase"].toString(),a); QCOMPARE(opaque["configuration"],data["sessions"].toArray()[0].toObject()["configuration"]);
 auto repaired=std::make_shared<PluginCatalog>(); repaired->discover(QCoreApplication::applicationDirPath()+"/plugins"); Workbench w(repaired,nullptr,wire.factory()); id=w.openWorkspace(b+"/opaque.json"); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success")); QCOMPARE(wire.opens,0);
 QCOMPARE(w.sessionConfiguration(0).protocol["devices"].toArray()[0].toObject()["path"].toString(),a+"/device.json"); id=w.saveWorkspace(b+"/known.json"); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success"));
 auto known=read(b+"/known.json")["sessions"].toArray()[0].toObject(); QVERIFY(!known.contains("referenceBase")); QCOMPARE(known["configuration"].toObject()["protocol"].toObject()["devices"].toArray()[0].toObject()["path"].toString(),QString("../a/device.json"));
 QVERIFY(w.session(0)->start(w.sessionConfiguration(0))); QTRY_COMPARE(w.session(0)->state(),SessionState::Running); QCOMPARE(wire.opens,1); QCOMPARE(w.session(0)->snapshot()["devices"].toArray()[0].toObject()["data"].toObject()["areas"].toObject()["holding"].toObject()["values"].toArray()[0].toInt(),777);
 }
 void loadFailureAndIncompatiblePluginsRemainVisible() {
 QTemporaryDir dir; const auto bin=QCoreApplication::applicationDirPath(); QVERIFY(QFile::copy(bin+"/plugins/libserial_plugin.dll",dir.filePath("serial.dll"))); QVERIFY(QFile::copy(bin+"/fixtures/libincompatible_plugin.dll",dir.filePath("bad.dll"))); QFile broken(dir.filePath("broken.dll")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("invalid dll"); broken.close();
 auto unavailable=std::make_shared<PluginCatalog>(); unavailable->discover(dir.path()); QVERIFY(!unavailable->diagnostics().isEmpty());
 auto path=dir.filePath("workspace.json"); QVERIFY(save(path,workspace({entry("incompatible"),entry("missing.protocol")}))); Workbench w(unavailable); auto id=w.openWorkspace(path); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success"));
 for(int i=0;i<2;++i) {QVERIFY(!pane(w,i)->findChild<QPushButton *>("start")->isEnabled()); const auto text=pane(w,i)->findChild<QLabel *>("status")->text(); QVERIFY(text.contains("bad.dll")); QVERIFY(text.contains("broken.dll"));}
 id=w.saveWorkspace(dir.filePath("saved.json")); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success")); QCOMPARE(read(dir.filePath("saved.json"))["sessions"].toArray().size(),2);
 }
 void toolbarUsesPendingFormConfiguration() {
 QTemporaryDir dir; Wire wire; Workbench w(catalog,nullptr,wire.factory()); w.show();
 const auto path=dir.filePath("toolbar.json"); w.findChild<QLineEdit *>("workspacePath")->setText(path);
 pane(w)->findChild<QLineEdit *>("port")->setText("COM123"); QTest::mouseClick(w.findChild<QPushButton *>("workspaceSave"),Qt::LeftButton);
 QTRY_VERIFY(QFile::exists(path)); QTRY_VERIFY(w.findChild<QLabel *>("workspaceStatus")->text().contains(QStringLiteral("已保存")));
 QCOMPARE(read(path)["sessions"].toArray()[0].toObject()["configuration"].toObject()["endpoint"].toObject()["port"].toString(),QString("COM123"));
 QTest::mouseClick(w.findChild<QPushButton *>("workspaceOpen"),Qt::LeftButton); QTRY_VERIFY(w.findChild<QLabel *>("workspaceStatus")->text().contains(QStringLiteral("已恢复"))); QCOMPARE(wire.opens,0);
 }
 void invalidDocumentDoesNotReplaceOrOverwrite() {
 QTemporaryDir dir; Wire wire; Workbench w(catalog,nullptr,wire.factory()); auto *original=w.session(0); const auto path=dir.filePath("invalid.json"); QVERIFY(save(path,{{"version",2},{"sessions",QJsonArray{}}}));
 auto id=w.openWorkspace(path); QTRY_COMPARE(w.workspaceOperation(id).state,QString("file-error")); QCOMPARE(w.session(0),original); QCOMPARE(w.sessionCount(),1);
 auto absent=std::make_shared<PluginCatalog>(); Workbench empty(absent); const auto target=dir.filePath("keep.json"); auto existing=workspace({}); QVERIFY(save(target,existing)); id=empty.saveWorkspace(target); QTRY_COMPARE(empty.workspaceOperation(id).state,QString("file-error")); QCOMPARE(read(target),existing);
 }
 void asynchronousBoundedAndLifetimeSafe() {
 QTemporaryDir dir; auto path=dir.filePath("workspace.json"); QVERIFY(save(path,workspace({entry()}))); QList<std::function<void()>> jobs;
 auto executor=[&](std::function<void()> job){jobs.append(std::move(job));}; Wire wire; qint64 now=0; Workbench w(catalog,nullptr,wire.factory(),[&]{return now;},executor);
 auto cfg=w.sessionConfiguration(0); cfg.endpoint["port"]="COM122"; QVERIFY(w.session(0)->start(cfg)); QTRY_COMPARE(w.session(0)->state(),SessionState::Running);
 auto id=w.openWorkspace(path); QVERIFY(id); QCOMPARE(w.openWorkspace(path),quint64(0)); QCOMPARE(w.session(0)->state(),SessionState::Running); QCOMPARE(wire.opens,1);
 const auto readId=w.session(0)->submit("read-holding",{{"unit",1},{"start",0},{"count",2}}); now+=2000; w.session(0)->poll(); QCoreApplication::processEvents(); QVERIFY(!wire.writes.isEmpty());
 wire.events.sent(wire.writes.last().first,true,{}); wire.events.received(ReceiveChunk(QByteArray::fromHex("010304000a0014da3e"),{})); QCoreApplication::processEvents(); now+=2000; w.session(0)->poll(); QCoreApplication::processEvents(); QCOMPARE(w.session(0)->operation(readId).update.state,QString("confirmed"));
 QList<Workbench *> others; for(int i=0;i<3;++i) {auto *other=new Workbench(catalog,nullptr,{}, {},executor); others.append(other); QVERIFY(other->openWorkspace(path));}
 Workbench overflow(catalog,nullptr,{}, {},executor); QCOMPARE(overflow.openWorkspace(path),quint64(0));
 qDeleteAll(others); for(auto &job:jobs) job(); jobs.clear(); QTRY_COMPARE(w.workspaceOperation(id).state,QString("success")); QCOMPARE(wire.opens,1);
 }
 void replacementRechecksEarlierDirtySession() {
 QTemporaryDir dir; auto file=dir.filePath("device.json"),path=dir.filePath("empty.json"); QVERIFY(save(file,document())); QVERIFY(save(path,workspace({}))); Wire wire; Workbench w(catalog,nullptr,wire.factory()); QVERIFY(w.closeSession(0));
 for(int i=0;i<2;++i) {auto n=w.addSession(config({QJsonObject{{"unit",17},{"path",file}}},QString("COM%1").arg(124+i))); QVERIFY(w.session(n)->start(w.sessionConfiguration(n))); QTRY_COMPARE(w.session(n)->state(),SessionState::Running);}
 auto *first=w.session(0),*second=w.session(1); const auto edit=[](DebugSession *s,int v){s->submit("slave-edit-value",{{"unit",17},{"area","holding"},{"address",107},{"value",v}});}; edit(first,111); edit(second,222);
 first->access().setDiscardGuard([first]{first->submit("slave-discard",{{"unit",17}});return true;}); second->access().setDiscardGuard([&]{edit(first,999);second->submit("slave-discard",{{"unit",17}});return true;});
 auto id=w.openWorkspace(path); QTRY_COMPARE(w.workspaceOperation(id).state,QString("cancelled")); QCOMPARE(w.sessionCount(),2); QCOMPARE(w.session(0),first); QVERIFY(first->hasUnsavedOrBusy()); QCOMPARE(first->state(),SessionState::Running);
 }
};
QTEST_MAIN(WorkspaceTests)
#include "workspace_tests.moc"



