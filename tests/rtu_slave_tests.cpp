#include "debugger/host.h"
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <qt_windows.h>

using namespace debugger;
namespace {
QJsonArray bits(const char *text) { QJsonArray values; for (; *text; ++text) values.append(*text == '1'); return values; }
QJsonObject document(int holdingValue = 555) {
    return {{"version", 1}, {"note", "保留未知字段"}, {"areas", QJsonObject{
        {"coils", QJsonObject{{"start", 19}, {"values", bits("1011001111010110010011010111000011011")}}},
        {"discrete", QJsonObject{{"start", 196}, {"values", bits("0011010111011011101011")}}},
        {"holding", QJsonObject{{"start", 107}, {"values", QJsonArray{holdingValue, 0, 100}}}},
        {"input", QJsonObject{{"start", 8}, {"values", QJsonArray{10}}}}
    }}};
}
bool save(const QString &path, const QJsonObject &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(data).toJson()) > 0;
}
SessionConfig config(QJsonArray devices = {}, const QString &port = "COM9999") {
    return {"modbus.rtu", "serial", "slave", {{"port", port}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {{"devices", devices}}};
}
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    int opens = 0;
    bool accept = true;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { ++wire_.opens; wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override {
            if (!wire_.accept) return false;
            wire_.writes.append({id, bytes}); return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
};
struct Harness {
    QTemporaryDir directory;
    qint64 time = 0;
    Wire wire;
    DebugSession session;
    explicit Harness(std::shared_ptr<PluginCatalog> catalog)
        : session(std::move(catalog), nullptr, wire.factory(), [this] { return time; }) {}
    SessionConfig makeConfig() {
        const auto first = directory.filePath("device17.json"), second = directory.filePath("device18.json");
        save(first, document()); save(second, document(111));
        return config({QJsonObject{{"unit", 17}, {"path", first}}, QJsonObject{{"unit", 18}, {"path", second}}});
    }
    void advance(qint64 us) {
        time += us; QCoreApplication::processEvents(); session.poll(); QCoreApplication::processEvents();
    }
    void receive(const QByteArray &bytes, std::optional<qint64> idle = {}) {
        wire.events.received(ReceiveChunk(bytes, idle)); QCoreApplication::processEvents();
    }
    void complete() {
        wire.events.sent(wire.writes.last().first, true, {}); QCoreApplication::processEvents();
    }
};
}
class SlaveTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu")); QVERIFY(catalog->protocol("modbus.rtu")->describe().roles.contains("slave"));
    }
    void fourAreaNormativeVectors_data() {
        QTest::addColumn<QByteArray>("request"); QTest::addColumn<QByteArray>("response");
        QTest::newRow("01") << QByteArray("1101001300250e84") << QByteArray("110105cd6bb20e1b45e6");
        QTest::newRow("02") << QByteArray("110200c40016baa9") << QByteArray("110203acdb352018");
        QTest::newRow("03") << QByteArray("1103006b00037687") << QByteArray("110306022b00000064c8ba");
        QTest::newRow("04") << QByteArray("110400080001b298") << QByteArray("110402000af8f4");
    }
    void fourAreaNormativeVectors() {
        QFETCH(QByteArray, request); QFETCH(QByteArray, response);
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig()));
        QCOMPARE(h.wire.opens, 0); QTRY_COMPARE(h.session.state(), SessionState::Running);
        const auto bytes = QByteArray::fromHex(request);
        h.receive(bytes.first(3)); h.advance(5000); // 无可信线路时间，主机分块延迟不等于截帧。
        QVERIFY(h.wire.writes.isEmpty()); h.receive(bytes.mid(3)); h.advance(1749);
        QVERIFY(h.wire.writes.isEmpty()); h.advance(1);
        QCOMPARE(h.wire.writes.size(), 1); QCOMPARE(h.wire.writes.last().second.toHex(), response);
        const auto message = h.session.messages().last(); QCOMPARE(message.sources.size(), 2);
        QCOMPARE(message.sourceRanges.size(), 2); QCOMPARE(message.operationId, quint64(0));
        QCOMPARE(message.status, "response-ready"); h.complete();
        QStringList kinds; for (const auto &record : h.session.traces()) kinds.append(record.kind);
        QVERIFY(kinds.contains("rx") && kinds.contains("send-request") && kinds.contains("local-sent"));
        QCOMPARE(h.session.snapshot().value("devices").toArray().size(), 2);
    }
    void invalidStartup_data() {
        QTest::addColumn<QString>("failure");
        for (const auto *name : {"missing", "directory", "invalid-json", "version", "missing-area", "bool-type",
             "register-type", "fraction", "overflow", "duplicate-unit", "duplicate-path", "relative", "oversized", "unreadable", "empty"})
            QTest::newRow(name) << QString(name);
    }
    void invalidStartup() {
        QFETCH(QString, failure); Harness h(catalog); auto settings = h.makeConfig();
        auto devices = settings.protocol.value("devices").toArray();
        const auto path = devices[1].toObject().value("path").toString();
        auto data = document();
        HANDLE exclusive = INVALID_HANDLE_VALUE;
        if (failure == "missing") QFile::remove(path);
        else if (failure == "directory") { auto binding=devices[1].toObject(); binding["path"]=h.directory.path(); devices[1]=binding; }
        else if (failure == "invalid-json") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{"); }
        else if (failure == "version") { data["version"]=2; QVERIFY(save(path,data)); }
        else if (failure == "missing-area") { auto areas=data["areas"].toObject(); areas.remove("input"); data["areas"]=areas; QVERIFY(save(path,data)); }
        else if (failure == "bool-type" || failure == "register-type" || failure == "fraction" || failure == "overflow") {
            auto areas=data["areas"].toObject(); const QString name=failure=="bool-type" ? "coils" : "holding";
            auto area=areas[name].toObject();
            if (failure=="overflow") area["start"]=65535;
            else area["values"]=QJsonArray{failure=="bool-type" ? QJsonValue(1) : failure=="fraction" ? QJsonValue(1.5) : QJsonValue(true)};
            areas[name]=area; data["areas"]=areas; QVERIFY(save(path,data));
        } else if (failure == "duplicate-unit" || failure == "duplicate-path") {
            auto binding=devices[1].toObject(); const auto first=devices[0].toObject();
            binding[failure=="duplicate-unit" ? "unit" : "path"]=first[failure=="duplicate-unit" ? "unit" : "path"]; devices[1]=binding;
        } else if (failure == "relative") { auto binding=devices[1].toObject(); binding["path"]="device18.json"; devices[1]=binding; }
        else if (failure == "oversized") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(4*1024*1024+1)); }
        else if (failure == "unreadable") { exclusive=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr); QVERIFY(exclusive!=INVALID_HANDLE_VALUE); }
        else if (failure == "empty") devices={};
        settings.protocol["devices"]=devices; QVERIFY(h.session.start(settings));
        QTRY_COMPARE(h.session.state(),SessionState::Error);
        QCOMPARE(h.wire.opens,0); QVERIFY(h.wire.writes.isEmpty()); QVERIFY(!h.session.diagnostic().isEmpty());
        if(failure!="empty") QVERIFY(h.session.diagnostic().contains(QStringLiteral("设备")));
        if(exclusive!=INVALID_HANDLE_VALUE) CloseHandle(exclusive);
    }
    void errorsBroadcastAndIsolation() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const QList<QPair<QByteArray,QByteArray>> examples{
            {"1203006b000376b4","120306006f000000642da7"},
            {"110300000001869a","118302c134"},
            {"1103006b00003686","11830300f4"},
            {"11070000b4d9","11870183f5"}
        };
        for (const auto &example : examples) {
            const auto previous=h.wire.writes.size(); h.receive(QByteArray::fromHex(example.first)); h.advance(1750);
            QCOMPARE(h.wire.writes.size(),previous+1); QCOMPARE(h.wire.writes.last().second.toHex(),example.second); h.complete();
        }
        const int sent=h.wire.writes.size();
        for(const auto &hex : {"0003006b000375c6","1303006b00037765","1103006b00037686"}) { h.receive(QByteArray::fromHex(hex)); h.advance(1750); }
        QCOMPARE(h.wire.writes.size(),sent);
        QVERIFY(!h.session.messages().isEmpty());
    }
    void fileChangesOnlyOnRestart() {
        Harness h(catalog); const auto settings=h.makeConfig(); const auto path=settings.protocol["devices"].toArray()[0].toObject()["path"].toString();
        QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        QVERIFY(save(path,document(777))); h.receive(QByteArray::fromHex("1103006b0001f746")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.toHex(),"110302022b38f8"); h.complete();
        h.session.stop(); QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Running);
        h.receive(QByteArray::fromHex("1103006b0001f746")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.toHex(),"1103020309b971"); h.complete();
        QVERIFY(QFile::remove(path)); h.receive(QByteArray::fromHex("1103006b0001f746")); h.advance(1750);
        QCOMPARE(h.wire.writes.last().second.toHex(),"1103020309b971");
        h.session.stop(); QVERIFY(h.session.start(settings)); QTRY_COMPARE(h.session.state(),SessionState::Error);
        QCOMPARE(h.wire.opens,2);
    }
    void cancelledPreparationAndIndependentSession() {
        Harness files(catalog); const auto settings=files.makeConfig(); QList<std::function<void()>> jobs; Wire wire;
        auto execute=[&](std::function<void()> task){jobs.append(std::move(task));};
        auto session=std::make_unique<DebugSession>(catalog,nullptr,wire.factory(),std::function<qint64()>{},execute);
        QVERIFY(session->start(settings)); QCOMPARE(wire.opens,0); QCOMPARE(session->state(),SessionState::Starting);
        session->stop(); QVERIFY(session->start(settings)); session.reset();
        Wire independent; DebugSession other(catalog,nullptr,independent.factory()); auto master=config({},"COM9998"); master.role="master";
        QVERIFY(other.start(master)); QTRY_COMPARE(other.state(),SessionState::Running); QCOMPARE(independent.opens,1);
        for(auto &job : jobs) job(); jobs.clear(); QCoreApplication::processEvents(); QCOMPARE(wire.opens,0);
    }
    void passiveSendFailureAndBoundedWait() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto request=QByteArray::fromHex("1103006b0001f746");
        h.receive(request); h.advance(1750); QCOMPARE(h.wire.writes.size(),1);
        h.advance(1000000); QCOMPARE(h.session.messages().last().status,"response-send-failed");
        QVERIFY(h.session.messages().last().detail.contains(QStringLiteral("超时")));
        QVERIFY(!h.session.messages().last().sourceRanges.isEmpty());
        QCOMPARE(h.wire.writes.size(),1); // 无后端完成时不自动重试。
        h.receive(request); h.advance(1750); QCOMPARE(h.wire.writes.size(),2);
        h.wire.events.sent(h.wire.writes.last().first,false,QStringLiteral("可控发送失败")); QCoreApplication::processEvents();
        QCOMPARE(h.session.messages().last().status,"response-send-failed");
        QVERIFY(h.session.messages().last().detail.contains(QStringLiteral("可控发送失败")));
        h.wire.accept=false; h.receive(request); h.advance(1750);
        QCOMPARE(h.session.messages().last().status,"response-send-failed");
        QVERIFY(h.session.messages().last().detail.contains(QStringLiteral("拒绝")));
    }
    void trustedInterCharacterViolation() {
        Harness h(catalog); QVERIFY(h.session.start(h.makeConfig())); QTRY_COMPARE(h.session.state(),SessionState::Running);
        const auto request=QByteArray::fromHex("1103006b0001f746");
        h.receive(request.first(3),0); h.advance(751); h.receive(request.mid(3),751); h.advance(1750);
        QVERIFY(h.wire.writes.isEmpty()); QCOMPARE(h.session.messages().last().status,"invalid");
        h.receive(request,1750); h.advance(1750); QCOMPARE(h.wire.writes.size(),1);
    }
    void preparationLimitAndCatalogLease() {
        Harness files(catalog); const auto settings=files.makeConfig(); QList<std::function<void()>> jobs; Wire wire;
        auto local=std::make_shared<PluginCatalog>(); local->discover(QCoreApplication::applicationDirPath()+"/plugins");
        std::weak_ptr<PluginCatalog> lease=local;
        auto session=std::make_unique<DebugSession>(local,nullptr,wire.factory(),std::function<qint64()>{},[&](std::function<void()> work){jobs.append(std::move(work));});
        for(int i=0;i<4;++i) { QVERIFY(session->start(settings)); session->stop(); }
        QVERIFY(!session->start(settings)); QVERIFY(session->diagnostic().contains(QStringLiteral("繁忙")));
        session.reset(); local.reset(); QVERIFY(!lease.expired());
        for(auto &job : jobs) job(); jobs.clear(); QCoreApplication::processEvents(); QTRY_VERIFY(lease.expired()); QCOMPARE(wire.opens,0);
    }
    void widgetBindingsAndData() {
        Harness files(catalog); const auto settings=files.makeConfig(); Wire wire;
        Workbench window(catalog,nullptr,wire.factory()); window.resize(1100,1050); window.show();
        auto *tabs=window.findChild<QTabWidget *>(); QVERIFY(tabs); tabs->setCurrentIndex(1);
        auto *role=window.findChild<QComboBox *>("role"); role->setCurrentIndex(role->findData("slave"));
        auto *unit=window.findChild<QSpinBox *>("slaveUnit"); auto *path=window.findChild<QLineEdit *>("slavePath");
        for(const auto &binding : settings.protocol["devices"].toArray()) {
            unit->setValue(binding.toObject()["unit"].toInt()); path->setText(binding.toObject()["path"].toString());
            QTest::mouseClick(window.findChild<QPushButton *>("addSlaveDevice"),Qt::LeftButton);
        }
        QTest::mouseClick(window.findChild<QPushButton *>("applySlaveBindings"),Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("start"),Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QLabel *>("status")->text().contains(QStringLiteral("运行中")));
        auto *data=window.findChild<QTableWidget *>("slaveData"); QTRY_COMPARE(data->rowCount(),8);
        QVERIFY(data->item(2,3)->text().contains("555")); QVERIFY(data->item(6,3)->text().contains("111"));
        const auto evidence=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!evidence.isEmpty()) QVERIFY(window.grab().save(evidence+"/slave-files-data-controlled.png"));
    }
    void widgetMissingFileError() {
        Wire wire; Workbench window(catalog,nullptr,wire.factory()); window.resize(1100,1050); window.show();
        window.findChild<QTabWidget *>()->setCurrentIndex(1);
        auto *role=window.findChild<QComboBox *>("role"); role->setCurrentIndex(role->findData("slave"));
        const auto path=QDir::tempPath()+"/missing-debugger-device-06.json";
        window.findChild<QSpinBox *>("slaveUnit")->setValue(17); window.findChild<QLineEdit *>("slavePath")->setText(path);
        QTest::mouseClick(window.findChild<QPushButton *>("addSlaveDevice"),Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("applySlaveBindings"),Qt::LeftButton);
        QTest::mouseClick(window.findChild<QPushButton *>("start"),Qt::LeftButton);
        auto *status=window.findChild<QLabel *>("status"); QTRY_VERIFY(status->text().contains(QStringLiteral("文件不存在")));
        QVERIFY(status->text().contains("17") && status->text().contains(path)); QCOMPARE(wire.opens,0);
        const auto evidence=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!evidence.isEmpty()) QVERIFY(window.grab().save(evidence+"/slave-file-error-controlled.png"));
    }
};
QTEST_MAIN(SlaveTests)
#include "rtu_slave_tests.moc"
