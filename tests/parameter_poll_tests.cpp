#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

using namespace debugger;
namespace {
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *widget : root.findChildren<T *>()) if (widget->accessibleName() == name) return widget;
    return nullptr;
}
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *widget : root.findChildren<QPushButton *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QByteArray frame(QByteArray bytes) {
    quint16 crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    bytes.append(char(crc & 0xff)); bytes.append(char(crc >> 8)); return bytes;
}
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> requests;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override { wire_.requests.append({id, bytes}); return true; }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
};
QJsonObject parameter(int unit, QString area, int address, QString group = "测量") {
    return {{"unit", unit}, {"area", area}, {"address", address}, {"group", group}};
}
SessionConfig configuration() {
    return {"modbus.rtu", "serial", "master", {{"port", "COM120"}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}},
        {{"parameterRows", QJsonArray{parameter(17, "holding", 107), parameter(17, "holding", 108),
            parameter(17, "holding", 110), parameter(17, "input", 107), parameter(18, "holding", 107)}}}, {}};
}
struct Harness {
    qint64 time = 0;
    Wire wire;
    DebugSession session;
    std::unique_ptr<ParameterView> view;
    explicit Harness(std::shared_ptr<PluginCatalog> catalog, SessionConfig config = configuration())
        : session(catalog, nullptr, wire.factory(), [&] { return time; }) {
        session.configure(config.protocol);
        view.reset(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1300, 650); view->show(); session.start(config); QCoreApplication::processEvents();
    }
    void advance(qint64 us = 5000) { time += us; session.poll(); QCoreApplication::processEvents(); }
    void reply(const QByteArray &payload) {
        wire.events.sent(wire.requests.last().first, true, {});
        wire.events.received(frame(payload)); QCoreApplication::processEvents(); advance();
    }
};
}
class ParameterPollTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void selectedReadsSeparateAreasAndDoNotIncludeUnselectedAddresses() {
        Harness h(catalog); QTRY_COMPARE(h.session.state(), SessionState::Running);
        auto *table = accessible<QTableWidget>(*h.view, QStringLiteral("参数表")); QVERIFY(table);
        QCOMPARE(table->rowCount(), 4);
        for (int row : {0, 1, 3}) table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        auto *read = button(*h.view, QStringLiteral("读取选中项"));
        QVERIFY2(read, "参数操作页需要明确的选中项读取入口");
        QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton); h.advance();
        QCOMPARE(h.wire.requests.size(), 1);
        QCOMPARE(h.wire.requests[0].second.left(6).toHex(), QByteArray("1103006b0002"));
        h.reply(QByteArray::fromHex("110304000a0014")); h.advance();
        QCOMPARE(h.wire.requests.size(), 2);
        QCOMPARE(h.wire.requests[1].second.left(6).toHex(), QByteArray("1104006b0001"));
        h.reply(QByteArray::fromHex("110402001e")); h.advance();
        QTRY_COMPARE(table->item(0, 3)->text(), QString("10"));
        QCOMPARE(table->item(1, 3)->text(), QString("20"));
        QCOMPARE(table->item(2, 3)->text(), QStringLiteral("未读取"));
        QTRY_COMPARE(table->item(3, 3)->text(), QString("30"));
        QCOMPARE(h.wire.requests.size(), 2);
    }
    void pollingKeepsCapturedScopeAndPendingWhileGroupsChange() {
        auto config = configuration();
        config.protocol["parameterRows"] = QJsonArray{parameter(17, "holding", 107), parameter(17, "input", 0, "其他")};
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        auto *start = button(*h.view, QStringLiteral("开始轮询"));
        QVERIFY2(start, "参数页需要明确范围和周期的开始轮询入口");
        auto *cycle = accessible<QSpinBox>(*h.view, QStringLiteral("轮询周期")); QVERIFY(cycle); cycle->setValue(100);
        auto *scope = accessible<QComboBox>(*h.view, QStringLiteral("轮询范围")); QVERIFY(scope);
        scope->setCurrentText(QStringLiteral("当前组")); QTRY_VERIFY(start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton); h.advance();
        QCOMPARE(h.wire.requests.size(), 1);
        QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("1103006b0001"));
        h.reply(QByteArray::fromHex("110302022b"));
        auto *table = accessible<QTableWidget>(*h.view, QStringLiteral("参数表"));
        QTRY_COMPARE(table->item(0, 3)->text(), QString("555"));
        const auto point = table->visualItemRect(table->item(0, 3)).center();
        QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, point);
        QTest::mouseDClick(table->viewport(), Qt::LeftButton, {}, point); QCoreApplication::processEvents();
        QLineEdit *editor = nullptr;
        for (auto *candidate : table->findChildren<QLineEdit *>()) if (candidate->isVisible()) editor = candidate;
        QVERIFY(editor); editor->selectAll(); QTest::keyClicks(editor, "123");
        h.advance(100000); QCOMPARE(h.wire.requests.size(), 2);
        h.reply(QByteArray::fromHex("110302029a")); QTest::qWait(70);
        QCOMPARE(editor->text(), QString("123")); QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(20);
        auto *groups = accessible<QComboBox>(*h.view, QStringLiteral("参数分组")); groups->setCurrentText(QStringLiteral("其他"));
        h.advance(100000); QCOMPARE(h.wire.requests.size(), 3);
        QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("1103006b0001"));
        h.reply(QByteArray::fromHex("1103020309")); QTest::qWait(70);
        groups->setCurrentText(QStringLiteral("测量"));
        QCOMPARE(table->item(0, 3)->text(), QString("123"));
        QTRY_VERIFY2(table->item(0, 3)->toolTip().contains("777"), qPrintable(table->item(0, 3)->toolTip()));
        QVERIFY(table->item(0, 3)->toolTip().contains(QStringLiteral("待提交")));
        QCOMPARE(h.session.config().protocol["polls"].toArray().size(), 1);
        QCOMPARE(h.session.config().protocol["polls"].toArray()[0].toObject()["intervalMs"].toInt(), 100);
    }
    void stoppingPollingKeepsInflightReadAndSessionAlive() {
        auto config = configuration(); config.protocol["parameterRows"] = QJsonArray{parameter(17, "holding", 107)};
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        accessible<QComboBox>(*h.view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        auto *start = button(*h.view, QStringLiteral("开始轮询")); QTRY_VERIFY(start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton); h.advance(); QCOMPARE(h.wire.requests.size(), 1);
        auto *stop = button(*h.view, QStringLiteral("停止会话轮询")); QTRY_VERIFY(stop->isEnabled());
        QTest::mouseClick(stop, Qt::LeftButton);
        QVERIFY2(h.session.config().protocol["polls"].toArray().isEmpty(), "在途读取不应阻止用户停止后续轮询");
        h.reply(QByteArray::fromHex("110302022b")); h.advance(2000000);
        QCOMPARE(h.wire.requests.size(), 1); QCOMPARE(h.session.state(), SessionState::Running);
        auto *table = accessible<QTableWidget>(*h.view, QStringLiteral("参数表"));
        QTRY_COMPARE(table->item(0, 3)->text(), QString("555"));
        QTest::mouseClick(button(*h.view, QStringLiteral("读取当前组")), Qt::LeftButton); h.advance();
        QCOMPARE(h.wire.requests.size(), 2);
        QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("1103006b0001"));
    }
    void pollingDifferentDevicesRetainsCapturedTargets() {
        auto config = configuration();
        config.protocol["parameterRows"] = QJsonArray{parameter(17, "holding", 107), parameter(18, "holding", 107)};
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        accessible<QComboBox>(*h.view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        accessible<QSpinBox>(*h.view, QStringLiteral("轮询周期"))->setValue(200);
        auto *start = button(*h.view, QStringLiteral("开始轮询")); QTRY_VERIFY(start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton); h.advance(); h.reply(QByteArray::fromHex("110302022b"));
        QVERIFY(h.view->selectDevice("18")); h.advance(200000);
        QCOMPARE(h.wire.requests.size(), 2);
        QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("1103006b0001"));
        h.reply(QByteArray::fromHex("110302022b"));
        accessible<QSpinBox>(*h.view, QStringLiteral("轮询周期"))->setValue(500);
        QTest::mouseClick(start, Qt::LeftButton); h.advance();
        QCOMPARE(h.session.config().protocol["polls"].toArray().size(), 2);
        QCOMPARE(h.wire.requests.size(), 3); QCOMPARE(h.wire.requests.last().second[0], char(17));
        h.reply(QByteArray::fromHex("110302022b")); h.advance();
        QCOMPARE(h.wire.requests.size(), 4); QCOMPARE(h.wire.requests.last().second[0], char(18));
        h.reply(QByteArray::fromHex("1203020309"));
        auto *table = accessible<QTableWidget>(*h.view, QStringLiteral("参数表"));
        QTRY_COMPARE(table->item(0, 3)->text(), QString("777"));
        QVERIFY(h.view->selectDevice("17")); QCOMPARE(table->item(0, 3)->text(), QString("555"));
        auto *summary = accessible<QPlainTextEdit>(*h.view, QStringLiteral("轮询任务")); QVERIFY(summary);
        QVERIFY(summary->toPlainText().contains(QStringLiteral("设备 17")));
        QVERIFY(summary->toPlainText().contains(QStringLiteral("设备 18")));
        QVERIFY(summary->toPlainText().contains("200 ms")); QVERIFY(summary->toPlainText().contains("500 ms"));
        if (const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR"); !output.isEmpty())
            QVERIFY(h.view->grab().save(output + "/multi-device-polling.png"));
        QTest::mouseClick(button(*h.view, QStringLiteral("停止会话轮询")), Qt::LeftButton);
        h.advance(2000000); QCOMPARE(h.wire.requests.size(), 4);
        QCOMPARE(h.session.state(), SessionState::Running);
    }
    void groupReadsSplitAtProtocolLimitWithoutReadingAnotherDevice() {
        auto config = configuration(); QJsonArray rows;
        for (int address = 0; address < 126; ++address) rows.append(parameter(17, "holding", address));
        rows.append(parameter(18, "holding", 0)); config.protocol["parameterRows"] = rows;
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        auto *read = button(*h.view, QStringLiteral("读取当前组")); QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton); h.advance();
        QCOMPARE(h.wire.requests.size(), 1); QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("11030000007d"));
        QByteArray values = QByteArray::fromHex("1103fa");
        for (int i = 0; i < 125; ++i) values += QByteArray::fromHex("002a");
        h.reply(values); h.advance();
        QCOMPARE(h.wire.requests.size(), 2); QCOMPARE(h.wire.requests.last().second.left(6).toHex(), QByteArray("1103007d0001"));
        h.reply(QByteArray::fromHex("110302002b")); h.advance();
        auto *table = accessible<QTableWidget>(*h.view, QStringLiteral("参数表"));
        QTRY_COMPARE(table->item(125, 3)->text(), QString("43"));
        QCOMPARE(table->item(124, 3)->text(), QString("42"));
        QCOMPARE(h.wire.requests.size(), 2);
    }
    void overCapacityScopesAreRejectedWithoutPartialRequests() {
        auto config = configuration(); QJsonArray rows;
        for (int index = 0; index < 65; ++index) rows.append(parameter(17, "holding", index * 2));
        config.protocol["parameterRows"] = rows;
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        auto *read = button(*h.view, QStringLiteral("读取当前组")); QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton); h.advance(); QCOMPARE(h.wire.requests.size(), 0);
        accessible<QComboBox>(*h.view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        QTest::mouseClick(button(*h.view, QStringLiteral("开始轮询")), Qt::LeftButton); h.advance();
        QVERIFY(h.session.config().protocol["polls"].toArray().isEmpty()); QCOMPARE(h.wire.requests.size(), 0);
        bool visible = false;
        for (auto *label : h.view->findChildren<QLabel *>())
            if (label->isVisible() && label->text().contains(QStringLiteral("64 个独立请求"))) visible = true;
        QVERIFY(visible);
    }
    void stopKeepsInflightReadRetriesAndOtherSettings() {
        auto config = configuration(); config.protocol["parameterRows"] = QJsonArray{parameter(17, "holding", 107)};
        config.protocol["responseTimeoutMs"] = 20; config.protocol["extraReadRetries"] = 1;
        config.protocol["retryIntervalMs"] = 10; config.protocol["futureSetting"] = "kept";
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running);
        accessible<QComboBox>(*h.view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        auto *start = button(*h.view, QStringLiteral("开始轮询")); QTRY_VERIFY(start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton); h.advance(); QCOMPARE(h.wire.requests.size(), 1);
        h.wire.events.sent(h.wire.requests.last().first, true, {}); QCoreApplication::processEvents();
        QTest::mouseClick(button(*h.view, QStringLiteral("停止会话轮询")), Qt::LeftButton);
        h.advance(20000); h.advance(10000); QCOMPARE(h.wire.requests.size(), 2);
        QCOMPARE(h.wire.requests.last().second, h.wire.requests.first().second);
        h.wire.events.sent(h.wire.requests.last().first, true, {}); QCoreApplication::processEvents();
        h.advance(20000); h.advance(2000000); QCOMPARE(h.wire.requests.size(), 2);
        const auto settings = h.session.config().protocol;
        QCOMPARE(settings["extraReadRetries"].toInt(), 1); QCOMPARE(settings["retryIntervalMs"].toInt(), 10);
        QCOMPARE(settings["futureSetting"].toString(), QString("kept")); QVERIFY(settings["polls"].toArray().isEmpty());
        QCOMPARE(h.session.state(), SessionState::Running);
    }
};
QTEST_MAIN(ParameterPollTests)
#include "parameter_poll_tests.moc"
