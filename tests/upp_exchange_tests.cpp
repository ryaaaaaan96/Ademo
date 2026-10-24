#include "debugger/host.h"
#include <QTest>
#include "../plugins/can_endpoint.h"
#include <QCanBusDevice>
#include <QTimer>
#include <QJsonArray>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTableWidget>
#include <QTemporaryDir>
#include "upp_vectors.h"
using namespace debugger;
namespace {
class Device : public QCanBusDevice {
public:
    QList<QCanBusFrame> writes; bool accept=true, automatic=false, synchronous=false;
    bool writeFrame(const QCanBusFrame &f) override { if(!accept)return false; writes<<f; if(synchronous)emit framesWritten(1);else if(automatic) QTimer::singleShot(0,this,[this]{emit framesWritten(1);}); return true; }
    void complete(int n=1) { emit framesWritten(n); }
    void inject(QByteArray bytes,int id=5,bool extended=false,bool echo=false) { QCanBusFrame frame(id,bytes);frame.setExtendedFrameFormat(extended);frame.setLocalEcho(echo);enqueueReceivedFrames({frame}); }
    QString interpretErrorFrame(const QCanBusFrame &) override {return {};}
protected: bool open() override {setState(ConnectedState);return true;} void close() override {setState(UnconnectedState);}
};
struct Input {
    QPointer<Device> device; EndpointEvents callbacks;
    DebugSession::EndpointFactory factory() { return [this](const QJsonObject &c,EndpointEvents e){callbacks=e;return createCanEndpoint(c,std::move(e),[this](const QJsonObject &,QString *){device=new Device;return device.data();});}; }
    void adu(QByteArray bytes,int id=5,bool extended=false,bool echo=false) {for(int p=0;p<bytes.size();p+=8)device->inject(bytes.mid(p,8),id,extended,echo);}
};
SessionConfig config() { return {"upp","can","master",{{"backend","virtualcan"},{"interface","can0"}}, {}}; }
QJsonObject parameters(int function) {
    QJsonObject p{{"deviceId",5},{"function",function}};
    const QJsonObject read{{"model",2},{"offset",6},{"length",3}},read2{{"model",4},{"offset",6},{"length",4}};
    const QJsonObject write{{"model",4},{"offset",6},{"dataHex","02"}},write1{{"model",2},{"offset",6},{"dataHex","010203"}},write2{{"model",5},{"offset",7},{"dataHex","01020304"}};
    if(function==0x21) for(auto it=read.begin();it!=read.end();++it)p[it.key()]=it.value();
    if(function==0x22)p["units"]=QJsonArray{read,read2};
    if(function==0x23) {p["incrementType"]=1;p["subsequence"]=0;}
    if(function==0x26)for(auto it=write.begin();it!=write.end();++it)p[it.key()]=it.value();
    if(function==0x27)p["units"]=QJsonArray{write1,write2};
    if(function==0x28) {p["readUnits"]=QJsonArray{read};p["writeUnits"]=QJsonArray{write};}
    return p;
}
QByteArray reply(int function, QByteArray pdu) {
    QByteArray b=QByteArray::fromHex("05500001");b[2]=char(pdu.size()+7);b.append(char(function));b+=pdu;quint16 crc=0xffff;for(auto c:b){crc^=quint8(c);for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xa001:0);}b.append(char(crc));b.append(char(crc>>8));return b;
}
void events() { QCoreApplication::processEvents();QCoreApplication::processEvents(); }
void completeRequest(DebugSession &s,Input &input,qint64 &clock,int size) {
    for(int i=0;i<(size+7)/8;++i) {input.device->complete();events();clock+=20000;s.poll();events();}
}
}
class UppExchangeTests : public QObject {
    Q_OBJECT
private slots:
    void activeHandshakeIsSubmitted() {
        auto catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");
        EndpointEvents input;
        DebugSession s(catalog, nullptr, [&](const QJsonObject &, EndpointEvents e) {
            input=e;
            class Wire : public Endpoint { public: void open() override {} void close() override {} bool send(quint64,const QByteArray &) override { return true; } bool sendUnit(quint64,const QByteArray &,const QJsonObject &) override { return true; } };
            return std::make_unique<Wire>();
        });
        QVERIFY2(s.start({"upp","can","master",{{"backend","virtualcan"},{"interface","can0"}}, {}}),qPrintable(s.diagnostic()));
        input.opened(true,{}); QTRY_COMPARE(s.state(),SessionState::Running);
        auto id=s.submit("upp.request",{{"deviceId",5},{"function",32}});
        QVERIFY(!s.operation(id).finished);
    }
    void canBackendWaitsForActualLocalCompletion() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");
        Input input;DebugSession s(catalog,nullptr,input.factory()); QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        auto op=s.submit("upp.request",{{"deviceId",5},{"function",32}});
        QCOMPARE(input.device->writes.size(),1); QCOMPARE(input.device->writes[0].payload().toHex(),QByteArray("05500701204889"));
        QVERIFY(s.operation(op).sendAccepted);QVERIFY(!s.operation(op).localSent);QVERIFY(!s.operation(op).finished);
        input.device->complete();QTRY_VERIFY(s.operation(op).localSent);QVERIFY(!s.operation(op).finished);
        input.adu(QByteArray::fromHex("05500f012032304141ffffffffb77a"));
        QTRY_COMPARE(s.operation(op).update.state,QString("succeeded"));
        QCOMPARE(s.messages().last().operationId,op);
    }
    void allFunctionsSendExactFramesAndConfirm_data() {
        QTest::addColumn<int>("index");QTest::addColumn<bool>("exception");
        for(int i=0;i<7;++i) {QTest::newRow(qPrintable(QString("ok-%1").arg(i)))<<i<<false;QTest::newRow(qPrintable(QString("exception-%1").arg(i)))<<i<<true;}
    }
    void allFunctionsSendExactFramesAndConfirm() {
        QFETCH(int,index);QFETCH(bool,exception); auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");
        qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        auto expected=QByteArray::fromHex(vectors[index*2].hex); auto op=s.submit("upp.request",parameters(vectors[index*2].function));
        completeRequest(s,input,clock,expected.size());QByteArray actual;for(const auto &frame:input.device->writes) {actual+=frame.payload();QCOMPARE(frame.frameId(),quint32(5));QVERIFY(!frame.hasExtendedFrameFormat());}
        QCOMPARE(actual,expected);QVERIFY(!s.operation(op).finished);QVERIFY(s.operation(op).localSent);
        input.adu(exception?reply(vectors[index*2].function|0x80,QByteArray::fromHex("0002")):QByteArray::fromHex(vectors[index*2+1].hex));
        QTRY_COMPARE(s.operation(op).update.state,QString(exception?"protocol-exception":"succeeded"));
        QCOMPARE(input.device->writes.size(),(expected.size()+7)/8);
        auto trace=s.queryTrace({});QVERIFY(!trace.messages.isEmpty());
    }
    void earlyReplyWaitsForLocalCompletionAndRejectsUnrelatedTraffic() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");Input input;DebugSession s(catalog,nullptr,input.factory());
        auto c=config();c.protocol["deviceRoutes"]=QJsonObject{{"5",QJsonObject{{"txId",0x12345},{"rxId",0x23456},{"extended",true}}}};
        QVERIFY(s.start(c));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x20));
        auto reply=QByteArray::fromHex("05500f012032304141ffffffffb77a");
        input.adu(reply,0x23456,true,true);input.adu(reply,5,true);input.adu(reply,0x23456,false);events();QVERIFY(!s.operation(op).finished);
        input.adu(reply,0x23456,true);events();QVERIFY(!s.operation(op).finished);QVERIFY(!s.operation(op).localSent);
        input.device->complete();QTRY_COMPARE(s.operation(op).update.state,QString("succeeded"));
        QCOMPARE(input.device->writes[0].frameId(),quint32(0x12345));QVERIFY(input.device->writes[0].hasExtendedFrameFormat());
    }
    void partialWriteFailureIsUncertainAndNeverRetried() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x27));
        QCOMPARE(input.device->writes.size(),1);input.device->complete();events();input.device->accept=false;clock+=20000;s.poll();
        QCOMPARE(s.operation(op).update.state,QString("uncertain"));QVERIFY(s.operation(op).sendAccepted);
        clock+=10000000;s.poll();QCOMPARE(input.device->writes.size(),1);
    }
    void timeoutStopAndInvalidInputsNeverCreateRetries() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        auto invalid=parameters(0x26);invalid["dataHex"]="0z";auto bad=s.submit("upp.request",invalid);QCOMPARE(s.operation(bad).update.state,QString("rejected"));QVERIFY(input.device->writes.isEmpty());
        auto op=s.submit("upp.request",parameters(0x26));auto busy=s.submit("upp.request",parameters(0x20));QCOMPARE(s.operation(busy).update.state,QString("rejected"));
        completeRequest(s,input,clock,14);clock+=1000000;s.poll();QCOMPARE(s.operation(op).update.state,QString("uncertain"));
        auto next=s.submit("upp.request",parameters(0x26));auto callbacks=input.callbacks;s.stop();QVERIFY(s.operation(next).update.result.value("uncertain").toBool());
        callbacks.sent(next,true,{});events();QVERIFY(s.operation(next).update.result.value("uncertain").toBool());
    }
    void operationPanelSubmitsAndShowsProtocolResult() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");Input input;DebugSession s(catalog,nullptr,input.factory());
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("upp",nullptr,s.access()));
        auto send=view->findChild<QPushButton *>("uppSend");QVERIFY(send);QTest::mouseClick(send,Qt::LeftButton);
        QCOMPARE(input.device->writes.size(),1);input.device->complete();events();input.adu(QByteArray::fromHex("05500f012032304141ffffffffb77a"));
        auto status=view->findChild<QLabel *>("uppOperationStatus");QVERIFY(status);QTRY_VERIFY(status->text().contains("succeeded"));
    }
    void independentPeerConfirmsRealCanOperationsAndExports() {
        QProcess peer;peer.setProgram(QCoreApplication::applicationDirPath()+"/upp_virtual_peer.exe");
        peer.setArguments({"--interface","can7","--device","5","--rx-id","321","--tx-id","456","--extended","--duration","5000"});
        peer.start();QVERIFY2(peer.waitForStarted(2000),qPrintable(peer.errorString()));QByteArray peerLog;
        QTRY_VERIFY_WITH_TIMEOUT((peerLog+=peer.readAllStandardOutput()).contains("READY"),3000);
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");DebugSession s(catalog);
        auto c=config();c.endpoint["interface"]="can7";c.protocol["deviceRoutes"]=QJsonObject{{"5",QJsonObject{{"txId",0x321},{"rxId",0x456},{"extended",true}}}};
        QVERIFY(s.start(c));QTRY_COMPARE(s.state(),SessionState::Running);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("upp",nullptr,s.access()));view->resize(1350,900);view->show();
        auto send=view->findChild<QPushButton *>("uppSend");QVERIFY(send);QTest::mouseClick(send,Qt::LeftButton);
        QTRY_VERIFY(view->findChild<QLabel *>("uppOperationStatus")->text().contains("succeeded"));
        for(int function:{0x21,0x22,0x23,0x26,0x27,0x28}) {
            auto p=parameters(function);if(function==0x27) {auto units=p.value("units").toArray();auto second=units[1].toObject();second["model"]=3;units[1]=second;p["units"]=units;}
            auto op=s.submit("upp.request",p);QTRY_VERIFY_WITH_TIMEOUT(s.operation(op).finished,2500);QCOMPARE(s.operation(op).update.state,QString("succeeded"));
        }
        auto invalid=parameters(0x21);invalid["model"]=50;auto bad=s.submit("upp.request",invalid);QTRY_COMPARE(s.operation(bad).update.state,QString("protocol-exception"));
        auto table=view->findChild<QTableWidget *>("uppMessages");QTRY_VERIFY(table->rowCount()>=16);table->selectRow(table->rowCount()-1);events();
        const auto directory=qEnvironmentVariable("DEBUGGER_UPP_EXCHANGE_EVIDENCE_DIR");
        if(!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));QVERIFY(view->grab().save(directory+"/upp-exchange-view.png"));
            auto result=s.exportTrace({},directory+"/upp-exchange.jsonl","jsonl");result.wait();QVERIFY(result.get().ok);
            QFile exported(directory+"/upp-exchange.jsonl");QVERIFY(exported.open(QIODevice::ReadOnly));auto data=exported.readAll();QVERIFY(data.contains("sourceRanges"));QVERIFY(data.contains("fragmentIndex"));QVERIFY(data.contains("succeeded"));
        }
        s.stop();QVERIFY(peer.waitForFinished(6000));QCOMPARE(peer.exitCode(),0);peerLog+=peer.readAllStandardOutput();
        if(!directory.isEmpty()) {QFile log(directory+"/peer-output.jsonl");QVERIFY(log.open(QIODevice::WriteOnly));log.write(peerLog);}
    }
    void canBackendBoundsAndSettlesAcceptedTokensExactlyOnce() {
        Device *device=nullptr;QList<quint64> completed;QList<bool> success;
        EndpointEvents e;e.opened=[](bool,QString){};e.failed=[](QString){};e.received=[](ReceiveChunk){};e.sent=[&](quint64 id,bool ok,QString){completed<<id;success<<ok;};
        auto endpoint=createCanEndpoint(config().endpoint,e,[&](const QJsonObject &,QString *){device=new Device;return device;});endpoint->open();
        QJsonObject metadata{{"unitKind","can-frame"},{"frameType","data"},{"canId",5},{"extended",false}};
        device->synchronous=true;QVERIFY(endpoint->sendUnit(1,"a",metadata));QCOMPARE(completed,QList<quint64>{1});events();QCOMPARE(completed,QList<quint64>{1});QVERIFY(success[0]);
        device->synchronous=false;for(int i=0;i<32;++i)QVERIFY(endpoint->sendUnit(10+i,"b",metadata));QVERIFY(!endpoint->sendUnit(99,"c",metadata));
        device->complete(5);QCOMPARE(completed.size(),6);events();QCOMPARE(completed.size(),6);endpoint->close();QCOMPARE(completed.size(),33);events();QCOMPARE(completed.size(),33);
        for(int i=6;i<success.size();++i)QVERIFY(!success[i]);
    }
    void partialCompletionDoesNotMarkWholeOperationSent() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");Input input;DebugSession s(catalog,nullptr,input.factory());
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x27));input.device->complete();events();QVERIFY(!s.operation(op).localSent);s.stop();QVERIFY(!s.operation(op).localSent);QVERIFY(s.operation(op).update.result.value("uncertain").toBool());
    }
    void exceptionTargetMustBelongToRequest() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x21));completeRequest(s,input,clock,13);
        input.adu(reply(0xa1,QByteArray::fromHex("000200040006")));events();QVERIFY(!s.operation(op).finished);
        input.adu(reply(0xa1,QByteArray::fromHex("000200020009")));events();QVERIFY(!s.operation(op).finished);
        input.adu(reply(0xa1,QByteArray::fromHex("000200020008")));QTRY_COMPARE(s.operation(op).update.state,QString("protocol-exception"));
    }
    void completionUsesSignalTimeRatherThanDispatchTime() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x20));clock=999000;input.device->complete();clock=1001000;events();QVERIFY(!s.operation(op).finished);QVERIFY(s.operation(op).localSent);
    }
    void spacingTimeoutAndWrongPduCannotConfirm() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto p=parameters(0x22);p["units"]=QJsonArray{QJsonObject{{"model",2},{"offset",6},{"length",3}}};auto op=s.submit("upp.request",p);
        input.device->complete();events();clock=19999;s.poll();QCOMPARE(input.device->writes.size(),1);clock=20000;s.poll();QCOMPARE(input.device->writes.size(),2);input.device->complete();events();
        input.adu(QByteArray::fromHex(vectors[5].hex));events();QVERIFY(!s.operation(op).finished);
        clock+=1000000;s.poll();QCOMPARE(s.operation(op).update.state,QString("timeout"));QCOMPARE(input.device->writes.size(),2);
        auto waiting=s.submit("upp.request",parameters(0x20));clock+=1000000;s.poll();QCOMPARE(s.operation(waiting).update.state,QString("send-failed"));
    }
    void invalidConfigurationAndOversizeRequestsFailBeforeSending() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");Input input;DebugSession s(catalog,nullptr,input.factory());
        auto invalid=config();invalid.protocol["deviceRoutes"]="not json";QVERIFY(!s.start(invalid));
        invalid=config();invalid.protocol["deviceRoutes"]=QJsonObject{{"5",QJsonObject{{"txId",0x800}}}};QVERIFY(!s.start(invalid));
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        for(auto p:QList<QJsonObject>{{{"deviceId",0},{"function",32}},{{"deviceId",5},{"function",33},{"model",2},{"offset",6},{"length",250}},{{"deviceId",5},{"function",38},{"model",2},{"offset",6},{"dataHex",QString(500,'a')}}}) {
            auto op=s.submit("upp.request",p);QCOMPARE(s.operation(op).update.state,QString("rejected"));
        }
        QVERIFY(input.device->writes.isEmpty());
    }
    void reconnectPreservesSequenceAndRejectsOldCallbacks() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");Input input;DebugSession s(catalog,nullptr,input.factory());
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);auto op=s.submit("upp.request",parameters(0x26));auto old=input.callbacks;old.failed("offline");QTRY_COMPARE(s.state(),SessionState::Error);QVERIFY(s.operation(op).update.result.value("uncertain").toBool());
        QVERIFY(s.reconnect());QTRY_COMPARE(s.state(),SessionState::Running);QVERIFY(input.device->writes.isEmpty());
        auto next=s.submit("upp.request",parameters(0x20));QCOMPARE(quint8(input.device->writes[0].payload()[3]),quint8(2));old.sent(next,true,{});events();QVERIFY(!s.operation(next).localSent);
        s.stop();QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);s.submit("upp.request",parameters(0x20));QCOMPARE(quint8(input.device->writes[0].payload()[3]),quint8(1));
    }
    void masterWorkspaceAndRtuRemainIndependent() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");
        QTemporaryDir directory;QVERIFY(directory.isValid());Input viewInput;Workbench window(catalog,nullptr,viewInput.factory());QVERIFY(window.closeSession(0));
        auto c=config();c.protocol["sendGapMs"]=30;c.protocol["responseTimeoutMs"]=1500;c.protocol["deviceRoutes"]=QJsonObject{{"5",QJsonObject{{"txId",321},{"rxId",456},{"extended",true}}}};
        const int index=window.addSession(c);QCOMPARE(window.sessionConfiguration(index).protocol,c.protocol);auto path=directory.filePath("master.json");auto save=window.saveWorkspace(path);QTRY_COMPARE(window.workspaceOperation(save).state,QString("success"));
        Input restoredInput;Workbench restored(catalog,nullptr,restoredInput.factory());auto load=restored.openWorkspace(path);QTRY_COMPARE(restored.workspaceOperation(load).state,QString("success"));
        QCOMPARE(restored.sessionConfiguration(index).role,QString("master"));QCOMPARE(restored.sessionConfiguration(index).protocol,c.protocol);QCOMPARE(restored.sessionConfiguration(index).protocol.value("deviceRoutes"),c.protocol.value("deviceRoutes"));QCOMPARE(restored.session(index)->state(),SessionState::Stopped);QVERIFY(restoredInput.device.isNull());QVERIFY(restored.sessionWindow(index)->findChild<QPushButton *>("uppSend"));
        EndpointEvents serialEvents;QList<QByteArray> writes;quint64 token=0;
        class Serial : public Endpoint {public:EndpointEvents &e;QList<QByteArray> &w;quint64 &token;Serial(EndpointEvents &events,QList<QByteArray> &writes,quint64 &t):e(events),w(writes),token(t){}void open() override{e.opened(true,{});}void close() override{}bool send(quint64 t,const QByteArray &b)override{token=t;w<<b;return true;}};
        qint64 clock=0;Input input;DebugSession upp(catalog,nullptr,input.factory(),[&]{return clock;});DebugSession rtu(catalog,nullptr,[&](const QJsonObject &,EndpointEvents e){serialEvents=e;return std::make_unique<Serial>(serialEvents,writes,token);},[&]{return clock;});
        QVERIFY(upp.start(config()));QTRY_COMPARE(upp.state(),SessionState::Running);SessionConfig serial{"modbus.rtu","serial","master",{{"port","COM215"},{"baud","9600"},{"dataBits","8"},{"parity","even"},{"stopBits","1"}},{{"extraReadRetries",0}}};QVERIFY(rtu.start(serial));QTRY_COMPARE(rtu.state(),SessionState::Running);
        auto canOp=upp.submit("upp.request",parameters(0x20));auto rtuOp=rtu.submit("read-holding",{{"unit",1},{"start",0},{"count",2}});clock=10000;rtu.poll();QTRY_COMPARE(writes.size(),1);QCOMPARE(writes.first().toHex(),QByteArray("010300000002c40b"));
        serialEvents.sent(token,true,{});input.device->complete();events();input.adu(QByteArray::fromHex("05500f012032304141ffffffffb77a"));QTRY_COMPARE(upp.operation(canOp).update.state,QString("succeeded"));QVERIFY(!rtu.operation(rtuOp).finished);
        serialEvents.received(ReceiveChunk(QByteArray::fromHex("010304000a0014da3e")));events();clock+=10000;rtu.poll();QTRY_COMPARE(rtu.operation(rtuOp).update.state,QString("success"));QVERIFY(rtu.operation(rtuOp).localSent);upp.stop();QCOMPARE(rtu.state(),SessionState::Running);
    }
    void sequenceWrapIsBoundedAndPerDevice() {
        auto catalog=std::make_shared<PluginCatalog>();catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");qint64 clock=0;Input input;DebugSession s(catalog,nullptr,input.factory(),[&]{return clock;});
        QVERIFY(s.start(config()));QTRY_COMPARE(s.state(),SessionState::Running);
        for(int i=1;i<=256;++i) {auto op=s.submit("upp.request",parameters(0x20));QCOMPARE(quint8(input.device->writes.last().payload()[3]),quint8(i==256?1:i));input.device->complete();events();clock+=1000001;s.poll();QVERIFY(s.operation(op).finished);}
        auto p=parameters(0x20);p["deviceId"]=6;s.submit("upp.request",p);QCOMPARE(quint8(input.device->writes.last().payload()[3]),quint8(1));
    }
};
QTEST_MAIN(UppExchangeTests)
#include "upp_exchange_tests.moc"
