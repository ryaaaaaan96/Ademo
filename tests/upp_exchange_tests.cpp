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
#include "upp_vectors.h"
using namespace debugger;
namespace {
class Device : public QCanBusDevice {
public:
    QList<QCanBusFrame> writes; bool accept=true, automatic=false;
    bool writeFrame(const QCanBusFrame &f) override { if(!accept)return false; writes<<f; if(automatic) QTimer::singleShot(0,this,[this]{emit framesWritten(1);}); return true; }
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
        input.adu(QByteArray::fromHex(vectors[exception?14+index:index*2+1].hex));
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
};
QTEST_MAIN(UppExchangeTests)
#include "upp_exchange_tests.moc"
