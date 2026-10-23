#include "slave_test_support.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>

namespace {
QJsonObject writableDocument(int initial = 0) {
    auto result = document(); auto areas = result["areas"].toObject();
    areas["coils"] = QJsonObject{{"start", 0}, {"values", QJsonArray{false,false,false,false,false,false,false,false,false,false,false,false}}};
    areas["holding"] = QJsonObject{{"start", 0}, {"values", QJsonArray{initial,initial,initial,initial}}};
    result["areas"] = areas; return result;
}
SessionConfig writableConfig(Harness &h) {
    auto settings = h.makeConfig(); const auto devices = settings.protocol["devices"].toArray();
    save(devices[0].toObject()["path"].toString(),writableDocument());
    save(devices[1].toObject()["path"].toString(),writableDocument(99));
    return settings;
}
QJsonObject device(const DebugSession &session, int unit) {
    for(const auto &entry : session.snapshot()["devices"].toArray()) if(entry.toObject()["unit"].toInt()==unit) return entry.toObject();
    return {};
}
QJsonArray values(const DebugSession &session, int unit, const QString &area) {
    return device(session,unit)["data"].toObject()["areas"].toObject()[area].toObject()["values"].toArray();
}
}
class SlaveWriteTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog=std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath()+"/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void fourFunctionsUpdateBeforeAcknowledgement_data() {
        QTest::addColumn<QByteArray>("request"); QTest::addColumn<QByteArray>("acknowledgement");
        QTest::addColumn<QString>("area"); QTest::addColumn<QJsonArray>("expected");
        QTest::newRow("05") << QByteArray("11050000ff008eaa") << QByteArray("11050000ff008eaa") << QString("coils") << QJsonArray{true,false,false,false,false,false,false,false,false,false,false,false};
        QTest::newRow("06") << QByteArray("1106000100039a9b") << QByteArray("1106000100039a9b") << QString("holding") << QJsonArray{0,3,0,0};
        QTest::newRow("0f") << QByteArray("110f0000000a02cd01bda8") << QByteArray("110f0000000ad75c") << QString("coils") << QJsonArray{true,false,true,true,false,false,true,true,true,false,false,false};
        QTest::newRow("10") << QByteArray("11100001000204000a0102c6f0") << QByteArray("1110000100021298") << QString("holding") << QJsonArray{0,10,258,0};
    }
    void fourFunctionsUpdateBeforeAcknowledgement() {
        QFETCH(QByteArray,request); QFETCH(QByteArray,acknowledgement); QFETCH(QString,area); QFETCH(QJsonArray,expected);
        Harness h(catalog); const auto settings=writableConfig(h); QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto beforeOther=device(h.session,18); const auto beforeReadonly=values(h.session,17,"input");
        const auto bytes=QByteArray::fromHex(request);
        h.receive(bytes.first(8)); h.advance(5000);
        if(bytes.size()>8) { QVERIFY(h.wire.writes.isEmpty()); h.receive(bytes.mid(8)); h.advance(1750); }
        QCOMPARE(h.wire.writes.size(),1); QCOMPARE(h.wire.writes.last().second.toHex(),acknowledgement);
        QCOMPARE(values(h.session,17,area),expected); // ACK 本地完成之前，全部值已生效。
        QCOMPARE(device(h.session,18),beforeOther); QCOMPARE(values(h.session,17,"input"),beforeReadonly);
        QVERIFY(device(h.session,17)["dirty"].toBool()); QCOMPARE(device(h.session,17)["revision"].toString(),"1");
        const auto message=h.session.messages().last(); QCOMPARE(message.status,"response-ready");
        QVERIFY(!message.sourceRanges.isEmpty()); QVERIFY(message.fields.contains("revision")); h.complete();
        // 文件保持启动时原文；外部设备写入只更新模拟数据。
        QFile file(settings.protocol["devices"].toArray()[0].toObject()["path"].toString()); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object(),writableDocument());
    }
    void subsequentReadUsesWrittenSnapshot() {
        Harness h(catalog); QVERIFY(h.session.start(writableConfig(h))); QTRY_COMPARE(h.session.state(),SessionState::Running);
        h.receive(QByteArray::fromHex("11100001000204000a0102c6f0")); h.advance(1750); h.complete();
        h.receive(QByteArray::fromHex("110300010002975b")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.toHex(),"110304000a01024ba1"); h.complete();
        h.receive(QByteArray::fromHex("110500000000cf5a")); h.advance(1750); h.complete();
        QCOMPARE(values(h.session,17,"coils")[0],QJsonValue(false));
    }
    void invalidWritesPreserveWholeDevice_data() {
        QTest::addColumn<QByteArray>("request"); QTest::addColumn<int>("exception");
        QTest::newRow("05-invalid-value") << QByteArray("1105000000010e9a") << 3;
        QTest::newRow("06-out-of-area") << QByteArray("1106000400010b5b") << 2;
        QTest::newRow("0f-zero") << QByteArray("110f00000000001afe") << 3;
        QTest::newRow("0f-overrun") << QByteArray("110f000b000201033a5b") << 2;
        QTest::newRow("0f-padding") << QByteArray("110f000000010181effb") << 3;
        QTest::newRow("10-byte-count") << QByteArray("111000000002020001aa14") << 3;
        QTest::newRow("10-partial-range") << QByteArray("11100003000204000a01024729") << 2;
        QTest::newRow("10-too-many") << QByteArray("11100000007c003851") << 3;
    }
    void invalidWritesPreserveWholeDevice() {
        QFETCH(QByteArray,request); QFETCH(int,exception);
        Harness h(catalog); QVERIFY(h.session.start(writableConfig(h))); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto before=h.session.snapshot(); h.receive(QByteArray::fromHex(request)); h.advance(1750);
        QCOMPARE(h.wire.writes.size(),1); const auto response=h.wire.writes.last().second;
        QCOMPARE(quint8(response[1]),quint8(QByteArray::fromHex(request)[1])|0x80); QCOMPARE(quint8(response[2]),exception);
        QCOMPARE(h.session.snapshot(),before); QCOMPARE(h.session.messages().last().status,"exception");
    }
    void broadcastWritesNeverRespond_data() {
        QTest::addColumn<QByteArray>("request"); QTest::addColumn<QString>("area");
        QTest::newRow("05") << QByteArray("00050000ff008deb") << QString("coils");
        QTest::newRow("06") << QByteArray("00060001000399da") << QString("holding");
        QTest::newRow("0f") << QByteArray("000f0000000a02cd017df8") << QString("coils");
        QTest::newRow("10") << QByteArray("00100001000204000a010296cc") << QString("holding");
    }
    void broadcastWritesNeverRespond() {
        QFETCH(QByteArray,request); QFETCH(QString,area);
        Harness h(catalog); QVERIFY(h.session.start(writableConfig(h))); QTRY_COMPARE(h.session.state(),SessionState::Running);
        h.receive(QByteArray::fromHex(request)); h.advance(1750); QVERIFY(h.wire.writes.isEmpty());
        for(int unit : {17,18}) { QVERIFY(device(h.session,unit)["dirty"].toBool()); QCOMPARE(device(h.session,unit)["revision"].toString(),"1"); }
        const auto message=h.session.messages().last(); QCOMPARE(message.status,"broadcast-written");
        QCOMPARE(message.fields["updatedUnits"].toArray(),QJsonArray({17,18}));
        QCOMPARE(values(h.session,17,area)[1],values(h.session,18,area)[1]);
        if(area=="coils") { QCOMPARE(values(h.session,17,area)[0],QJsonValue(true)); QCOMPARE(values(h.session,18,area)[0],QJsonValue(true)); }
    }
    void broadcastInvalidAndIndependentRanges() {
        Harness h(catalog); auto settings=writableConfig(h);
        auto smaller=writableDocument(99); auto areas=smaller["areas"].toObject(); areas["holding"]=QJsonObject{{"start",3},{"values",QJsonArray{99}}}; smaller["areas"]=areas;
        QVERIFY(save(settings.protocol["devices"].toArray()[1].toObject()["path"].toString(),smaller));
        QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto other=device(h.session,18);
        h.receive(QByteArray::fromHex("00060001000399da")); h.advance(1750);
        QVERIFY(h.wire.writes.isEmpty()); QCOMPARE(values(h.session,17,"holding")[1],QJsonValue(3)); QCOMPARE(device(h.session,18),other);
        QCOMPARE(h.session.messages().last().fields["rejectedUnits"].toArray(),QJsonArray({18}));
        const auto before=h.session.snapshot(); h.receive(QByteArray::fromHex("0005000000010ddb")); h.advance(1750);
        QVERIFY(h.wire.writes.isEmpty()); QCOMPARE(h.session.snapshot(),before);
    }
    void busyOrDamagedRequestDoesNotWrite() {
        Harness h(catalog); QVERIFY(h.session.start(writableConfig(h))); QTRY_COMPARE(h.session.state(),SessionState::Running);
        h.receive(QByteArray::fromHex("1106000100039a9b")); h.advance(1750);
        const auto after=h.session.snapshot(); h.receive(QByteArray::fromHex("11050000ff008eaa")); h.advance(1750);
        QCOMPARE(h.session.snapshot(),after); QCOMPARE(h.session.messages().last().status,"rejected"); h.complete();
        h.receive(QByteArray::fromHex("11050000ff008eab")); h.advance(1750); QCOMPARE(h.session.snapshot(),after);
        h.receive(QByteArray::fromHex("1306000100039b79")); h.advance(1750); // CRC 有效但没有 Unit 19，不污染设备。
        QCOMPARE(h.session.snapshot(),after);
    }
    void responseFailureDoesNotUndoWrite() {
        Harness h(catalog); QVERIFY(h.session.start(writableConfig(h))); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto first=device(h.session,17); h.wire.accept=false;
        h.receive(QByteArray::fromHex("1206000100039aa8")); h.advance(1750);
        QCOMPARE(values(h.session,18,"holding"),QJsonArray({99,3,99,99})); QCOMPARE(device(h.session,17),first);
        QCOMPARE(h.session.messages().last().status,"response-send-failed");
        h.advance(1000000); QCOMPARE(device(h.session,18)["revision"].toString(),"1");
        QVERIFY(h.wire.writes.isEmpty());
    }
    void maximumLegalMultipleWrites_data() {
        QTest::addColumn<bool>("coils"); QTest::newRow("1968-coils")<<true; QTest::newRow("123-registers")<<false;
    }
    void maximumLegalMultipleWrites() {
        QFETCH(bool,coils); Harness h(catalog); auto settings=writableConfig(h);
        auto data=writableDocument(); auto areas=data["areas"].toObject(); QJsonArray initial;
        const int count=coils ? 1968 : 123; for(int i=0;i<count;++i) initial.append(coils ? QJsonValue(false) : QJsonValue(0));
        const QString area=coils ? "coils" : "holding"; areas[area]=QJsonObject{{"start",0},{"values",initial}}; data["areas"]=areas;
        QVERIFY(save(settings.protocol["devices"].toArray()[0].toObject()["path"].toString(),data));
        QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        QByteArray request=QByteArray::fromHex(coils ? "110f000007b0f6" : "11100000007bf6");
        if(coils) request.append(QByteArray(246,char(0xff)));
        else for(int i=0;i<123;++i) request.append(QByteArray::fromHex("0001"));
        request.append(QByteArray::fromHex(coils ? "d739" : "25ae"));
        QCOMPARE(request.size(),255); h.receive(request); h.advance(1750);
        QCOMPARE(h.wire.writes.size(),1); QCOMPARE(h.wire.writes.last().second.toHex(),coils ? "110f000007b054df" : "11100000007b82ba");
        const auto written=values(h.session,17,area); QCOMPARE(written.size(),count);
        for(const auto &value : written) QCOMPARE(value,coils ? QJsonValue(true) : QJsonValue(1));
    }
    void widgetReflectsExternalWrite() {
        Harness files(catalog); const auto settings=writableConfig(files); Wire wire;
        Workbench window(catalog,nullptr,wire.factory()); window.resize(1100,1100); window.show();
        auto *tabs=window.findChild<QTabWidget *>("protocolTabs"); QVERIFY(tabs); tabs->setCurrentIndex(1);
        auto *role=window.findChild<QComboBox *>("role"); role->setCurrentIndex(role->findData("slave"));
        for(const auto &binding : settings.protocol["devices"].toArray()) {
            window.findChild<QSpinBox *>("slaveUnit")->setValue(binding.toObject()["unit"].toInt());
            window.findChild<QLineEdit *>("slavePath")->setText(binding.toObject()["path"].toString());
            QTest::mouseClick(window.findChild<QPushButton *>("addSlaveDevice"),Qt::LeftButton);
        }
        QTest::mouseClick(window.findChild<QPushButton *>("applySlaveBindings"),Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("start"),Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QLabel *>("status")->text().contains(QStringLiteral("运行中")));
        wire.events.received(QByteArray::fromHex("11100001000204000a0102c6f0"));
        QTRY_COMPARE(wire.writes.size(),1); QCOMPARE(wire.writes.last().second.toHex(),"1110000100021298");
        auto *data=window.findChild<QTableWidget *>("slaveData"); QTRY_VERIFY(data->item(2,3) && data->item(2,3)->text().contains("10,258"));
        QVERIFY(data->item(6,3)->text().contains("99,99,99,99"));
        const auto evidence=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!evidence.isEmpty()) QVERIFY(window.grab().save(evidence+"/slave-external-write-controlled.png"));
    }
};
QTEST_MAIN(SlaveWriteTests)
#include "rtu_slave_write_tests.moc"
