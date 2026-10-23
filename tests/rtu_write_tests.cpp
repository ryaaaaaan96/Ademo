#include "debugger/host.h"
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

using namespace debugger;
namespace {
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    bool accept = true;
    std::function<void(quint64)> onWrite;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override {
            if (!wire_.accept) return false;
            wire_.writes.append({id, bytes});
            if (wire_.onWrite) wire_.onWrite(id);
            return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents callbacks) {
            events = std::move(callbacks);
            return std::make_unique<Port>(*this);
        };
    }
};
struct Harness {
    qint64 time = 0;
    Wire wire;
    DebugSession session;
    explicit Harness(std::shared_ptr<PluginCatalog> catalog)
        : session(std::move(catalog), nullptr, wire.factory(), [this] { return time; }) {}
    bool start() {
        SessionConfig config{"modbus.rtu", "serial", "master", {{"port", "COM9999"}, {"baud", "115200"},
            {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
        const bool ok = session.start(config);
        QCoreApplication::processEvents();
        return ok && session.state() == SessionState::Running;
    }
    void advance(qint64 us) {
        time += us;
        QCoreApplication::processEvents();
        session.poll();
        QCoreApplication::processEvents();
    }
    void sent(quint64 id, bool ok = true) {
        wire.events.sent(id, ok, ok ? QString() : QStringLiteral("发送失败"));
        QCoreApplication::processEvents();
    }
    void receive(const QByteArray &bytes) {
        wire.events.received(bytes);
        QCoreApplication::processEvents();
    }
};
struct Vector {
    const char *name;
    QString action;
    QJsonObject parameters;
    QByteArray request, response, exception;
};
const QList<Vector> vectors{
    {"05", "write-single-coil", {{"unit",17},{"start",172},{"value",true}},
     "110500acff004e8b", "110500acff004e8b", "118502c294"},
    {"06", "write-single-register", {{"unit",17},{"start",1},{"value",3}},
     "1106000100039a9b", "1106000100039a9b", "118602c264"},
    {"0f", "write-multiple-coils", {{"unit",17},{"start",19},{"values",QJsonArray{true,false,true,true,false,false,true,true,true,false}}},
     "110f0013000a02cd01bf0b", "110f0013000a2699", "118f02c434"},
    {"10", "write-multiple-registers", {{"unit",17},{"start",1},{"values",QJsonArray{10,258}}},
     "11100001000204000a0102c6f0", "1110000100021298", "119002cc04"}
};
}
class WriteTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void normativeVectors_data() {
        QTest::addColumn<int>("index"); QTest::addColumn<bool>("exception");
        for (int i=0;i<vectors.size();++i) {
            QTest::newRow(vectors[i].name) << i << false;
            QTest::newRow((QByteArray(vectors[i].name)+"-exception").constData()) << i << true;
        }
    }
    void normativeVectors() {
        QFETCH(int,index); QFETCH(bool,exception);
        Harness h(catalog); QVERIFY(h.start()); const auto &v = vectors[index];
        const auto id = h.session.submit(v.action,v.parameters);
        h.advance(1750);
        QCOMPARE(h.wire.writes.size(),1);
        QCOMPARE(h.wire.writes[0].second.toHex(),v.request);
        QCOMPARE(h.session.operation(id).update.state,"submitted");
        h.sent(id);
        QCOMPARE(h.session.operation(id).update.state,"sent");
        QVERIFY(!h.session.operation(id).finished);
        const auto response = QByteArray::fromHex(exception ? v.exception : v.response);
        h.receive(response.first(3)); h.advance(500); h.receive(response.mid(3)); h.advance(1750);
        QCOMPARE(h.session.operation(id).update.state,exception ? "exception" : "success");
        QCOMPARE(h.session.messages(id).last().operationId,id);
        QCOMPARE(h.session.messages(id).last().sources.size(),2);
        if (exception) QCOMPARE(h.session.operation(id).update.result.value("exceptionCode").toInt(),2);
        h.advance(4000000); QCOMPARE(h.wire.writes.size(),1);
    }
    void invalidParameters_data() {
        QTest::addColumn<QString>("action"); QTest::addColumn<QJsonObject>("parameters");
        const QJsonObject valid{{"unit",1},{"start",0},{"value",1}};
        auto row = [&](const char *name, const QString &action, const QJsonObject &p) { QTest::newRow(name)<<action<<p; };
        auto p=valid; p["unit"]=0; row("broadcast","write-single-register",p);
        p=valid; p["unit"]=248; row("bad-unit","write-single-register",p);
        p=valid; p["start"]=65536; row("bad-start","write-single-register",p);
        p=valid; p["value"]=65536; row("bad-value","write-single-register",p);
        p=valid; p["value"]=1.5; row("fraction","write-single-register",p);
        row("coil-non-bool","write-single-coil",valid);
        row("empty","write-multiple-coils",{{"unit",1},{"start",0},{"values",QJsonArray{}}});
        row("overflow","write-multiple-registers",{{"unit",1},{"start",65535},{"values",QJsonArray{1,2}}});
        QJsonArray registers; for(int i=0;i<124;++i) registers.append(i);
        row("124-registers","write-multiple-registers",{{"unit",1},{"start",0},{"values",registers}});
        QJsonArray coils; for(int i=0;i<1969;++i) coils.append(false);
        row("1969-coils","write-multiple-coils",{{"unit",1},{"start",0},{"values",coils}});
        row("mixed-types","write-multiple-registers",{{"unit",1},{"start",0},{"values",QJsonArray{1,true}}});
    }
    void invalidParameters() {
        QFETCH(QString,action); QFETCH(QJsonObject,parameters);
        Harness h(catalog); QVERIFY(h.start());
        const auto id=h.session.submit(action,parameters);
        QCOMPARE(h.session.operation(id).update.state,"invalid"); h.advance(2000000);
        QVERIFY(h.wire.writes.isEmpty());
    }
    void timeoutAndManualRepeat() {
        Harness h(catalog); QVERIFY(h.start()); const auto &v=vectors[1];
        const auto id=h.session.submit(v.action,v.parameters); h.advance(1750); h.sent(id);
        h.advance(1000000); QCOMPARE(h.session.operation(id).update.state,"timeout");
        QVERIFY(h.session.operation(id).update.result.value("uncertain").toBool());
        QVERIFY(h.session.operation(id).update.detail.contains(QStringLiteral("结果不确定")));
        h.advance(10000000); QCOMPARE(h.wire.writes.size(),1);
        h.session.stop(); QVERIFY(h.start()); h.advance(10000000);
        QCOMPARE(h.wire.writes.size(),1); // 重启不补发。
        const auto next=h.session.submit(v.action,v.parameters); QVERIFY(next>id);
        h.advance(1750); h.sent(next); h.receive(QByteArray::fromHex(v.response)); h.advance(1750);
        QCOMPARE(h.session.operation(next).update.state,"success"); QCOMPARE(h.wire.writes.size(),2);
        QVERIFY(h.session.traces(id).last().id<h.session.traces(next).first().id);
    }
    void failuresNeverRetry_data() {
        QTest::addColumn<QString>("failure");
        for (const auto *name : {"rejected","local-failed","local-timeout","disconnected","stopped"}) QTest::newRow(name)<<QString(name);
    }
    void failuresNeverRetry() {
        QFETCH(QString,failure); Harness h(catalog); QVERIFY(h.start()); const auto &v=vectors[0];
        h.wire.accept=failure!="rejected";
        const auto id=h.session.submit(v.action,v.parameters); h.advance(1750);
        if(failure=="local-failed") h.sent(id,false);
        else if(failure=="local-timeout") h.advance(1000000);
        else if(failure=="disconnected") { h.wire.events.failed(QStringLiteral("断线")); QCoreApplication::processEvents(); }
        else if(failure=="stopped") h.session.stop();
        QVERIFY(h.session.operation(id).finished);
        QVERIFY(h.session.operation(id).update.state!="success");
        QCOMPARE(h.session.operation(id).sendAccepted,failure!="rejected");
        h.advance(10000000); QCOMPARE(h.wire.writes.size(),failure=="rejected"?0:1);
    }
    void invalidEchoNeverConfirms() {
        Harness h(catalog); QVERIFY(h.start()); const auto &v=vectors[1];
        const auto id=h.session.submit(v.action,v.parameters); h.advance(1750); h.sent(id);
        // CRC 有效但值不符（另一条合法 06 响应），不能确认本操作。
        h.receive(QByteArray::fromHex("110500acff004e8b")); h.advance(1750);
        QCOMPARE(h.session.operation(id).update.state,"sent");
        QCOMPARE(h.session.messages(id).last().operationId,quint64(0));
        h.receive(QByteArray::fromHex("110600010004db59")); h.advance(1750); // 同功能但回显值不符。
        QCOMPARE(h.session.operation(id).update.state,"sent");
        h.receive(QByteArray::fromHex("1106000200036a9b")); h.advance(1750); // 回显起址不符。
        QCOMPARE(h.session.operation(id).update.state,"sent");
        auto broken=QByteArray::fromHex(v.response); broken[3]^=1; h.receive(broken); h.advance(1750);
        QCOMPARE(h.session.operation(id).update.state,"sent");
        h.advance(1000000); QCOMPARE(h.session.operation(id).update.state,"timeout");
    }
    void widgetWrite_data() {
        QTest::addColumn<int>("index"); QTest::addColumn<QString>("outcome");
        for(int i=0;i<vectors.size();++i) QTest::newRow(vectors[i].name)<<i<<QString("success");
        for(const auto *outcome : {"timeout","exception","disconnected","stopped"}) QTest::newRow(outcome)<<0<<QString(outcome);
    }
    void widgetWrite() {
        QFETCH(int,index); QFETCH(QString,outcome); const auto &v=vectors[index]; Wire wire;
        Workbench window(catalog,nullptr,wire.factory());
        QPushButton *advanced=nullptr;
        for(auto *candidate:window.parameterView(0)->findChildren<QPushButton*>())
            if(candidate->text()==QStringLiteral("按功能码操作…")) advanced=candidate;
        QVERIFY(advanced); advanced->click();
        wire.onWrite=[&](quint64 id) {
            wire.events.sent(id,true,{});
            QTimer::singleShot(10,&window,[&]{
                if(outcome=="success" || outcome=="exception")
                    wire.events.received(QByteArray::fromHex(outcome=="success" ? v.response : v.exception));
                else if(outcome=="disconnected") wire.events.failed(QStringLiteral("可控断线"));
                else if(outcome=="stopped") QTest::mouseClick(window.findChild<QPushButton *>("stop"),Qt::LeftButton);
            });
        };
        window.resize(920,1000); window.show();
        auto *actions=window.findChild<QComboBox *>("writeAction");
        auto *button=window.findChild<QPushButton *>("writeDevice");
        QVERIFY(actions&&button);
        actions->setCurrentIndex(index);
        window.findChild<QSpinBox *>("unit")->setValue(17);
        window.findChild<QSpinBox *>("registerStart")->setValue(v.parameters.value("start").toInt());
        const QStringList texts{"true","3","[true,false,true,true,false,false,true,true,true,false]","[10,258]"};
        window.findChild<QLineEdit *>("writeValues")->setText(texts[index]);
        QTest::mouseClick(window.findChild<QPushButton *>("start"),Qt::LeftButton);
        QTRY_VERIFY(button->isEnabled()); QTest::mouseClick(button,Qt::LeftButton);
        auto *result=window.findChild<QLabel *>("readResult");
        const auto state=outcome=="disconnected" ? "transport-error" : outcome=="stopped" ? "cancelled" : outcome;
        QTRY_VERIFY(result->text().contains(state));
        if(outcome!="success" && outcome!="exception") QVERIFY(result->text().contains(QStringLiteral("不确定")));
        QCOMPARE(wire.writes.size(),1); QCOMPARE(wire.writes.last().second.toHex(),v.request);
        const auto output=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!output.isEmpty()) QVERIFY(window.grab().save(output+"/write-"+v.name+"-"+outcome+"-controlled.png"));
    }
};
QTEST_MAIN(WriteTests)
#include "rtu_write_tests.moc"
