#include "debugger/host.h"
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QTest>

using namespace debugger;
namespace {
const QJsonObject readOne{{"unit", 1}, {"start", 0}, {"count", 1}};
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    std::function<void(quint64, const QByteArray &)> written;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &bytes) override {
            wire_.writes.append({id, bytes});
            if (wire_.written) wire_.written(id, bytes);
            return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events);
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
    bool start(QJsonObject settings = {}) {
        const bool ok = session.start({"modbus.rtu", "serial", "master", {{"port", "COM9999"},
            {"baud", "115200"}, {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, settings});
        QCoreApplication::processEvents();
        return ok && session.state() == SessionState::Running;
    }
    void advance(qint64 us) { time += us; QCoreApplication::processEvents(); session.poll(); QCoreApplication::processEvents(); }
    void sent(quint64 wireId) { wire.events.sent(wireId, true, {}); QCoreApplication::processEvents(); }
    void reply(const char *hex) { wire.events.received(QByteArray::fromHex(hex)); QCoreApplication::processEvents(); advance(1750); }
};
QJsonObject task(int unit, int cycle = 1000) {
    return {{"unit", unit}, {"action", "read-holding"}, {"start", 0}, {"count", 1}, {"intervalMs", cycle}};
}
}
class PollTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void defaultTwoRetriesAndIndependentGap() {
        Harness h(catalog); QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first);
        h.advance(1000000);
        QCOMPARE(h.session.operation(id).update.state, "retry-wait");
        QVERIFY(!h.session.operation(id).finished);
        h.advance(99999); QCOMPARE(h.wire.writes.size(), 1);
        h.advance(1); QCOMPARE(h.wire.writes.size(), 2);
        const auto secondToken = h.wire.writes.last().first;
        QVERIFY(secondToken != h.wire.writes.first().first);
        h.sent(h.wire.writes.first().first); // 前一尝试迟到本地完成不能推进本次发送。
        QCOMPARE(h.session.operation(id).update.state, "submitted");
        h.sent(secondToken); h.advance(1000000); h.advance(100000);
        QCOMPARE(h.wire.writes.size(), 3);
        h.sent(h.wire.writes.last().first); h.advance(1000000);
        QCOMPARE(h.session.operation(id).update.state, "timeout");
        QCOMPARE(h.session.operation(id).update.result.value("attempts").toInt(), 3);
        int sends = 0, timeouts = 0;
        for (const auto &record : h.session.traces(id)) if (record.kind == "send-request") ++sends;
        for (const auto &message : h.session.messages(id)) if (message.status == "attempt-timeout") ++timeouts;
        QCOMPARE(sends, 3); QCOMPARE(timeouts, 3);
        h.advance(2000000); QCOMPARE(h.wire.writes.size(), 3);
    }
    void zeroRetriesAndExceptions() {
        Harness h(catalog); QVERIFY(h.start({{"extraReadRetries", 0}, {"responseTimeoutMs", 20}, {"retryIntervalMs", 7}}));
        const auto id = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(20000);
        QCOMPARE(h.session.operation(id).update.state, "timeout");
        QCOMPARE(h.wire.writes.size(), 1);
        h.advance(27000);
        const auto next = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first); h.reply("018302c0f1");
        QCOMPARE(h.session.operation(next).update.state, "exception");
        h.advance(1000000); QCOMPARE(h.wire.writes.size(), 2);
    }
    void lateGapAndAmbiguousRetryResponse() {
        Harness h(catalog); QVERIFY(h.start({{"responseTimeoutMs", 20}, {"retryIntervalMs", 10}}));
        const auto id = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(20000);
        h.reply("010302000a3843");
        QCOMPARE(h.session.messages(id).last().status, "late");
        QCOMPARE(h.session.messages(id).last().operationId, quint64(0));
        h.advance(8250); QCOMPARE(h.wire.writes.size(), 2);
        h.sent(h.wire.writes.last().first); h.reply("010302000a3843");
        QCOMPARE(h.session.operation(id).update.state, "success");
        QVERIFY(h.session.operation(id).update.result.value("attemptAttributionUncertain").toBool());
        const auto message = h.session.messages(id).last();
        QCOMPARE(message.fields.value("attempt").toInt(-1), 0);
        QCOMPARE(message.fields.value("possibleAttempts").toArray(), QJsonArray({1, 2}));
    }
    void manualAndPollingShareOneBus() {
        Harness h(catalog); QVERIFY(h.start({{"polls", QJsonArray{task(1, 50), task(2, 100)}}}));
        h.advance(1750); QCOMPARE(h.wire.writes.size(), 1);
        QCOMPARE(h.wire.writes.last().second.toHex(), "010300000001840a");
        const auto manual = h.session.submit("read-holding", {{"unit", 3}, {"start", 0}, {"count", 1}});
        QCOMPARE(h.session.operation(manual).update.state, "queued");
        h.advance(10000); QCOMPARE(h.wire.writes.size(), 1);
        h.sent(h.wire.writes.last().first); h.reply("010302000a3843");
        h.advance(1750); QCOMPARE(h.wire.writes.size(), 2);
        QCOMPARE(h.wire.writes.last().second.toHex(), "03030000000185e8");
        h.sent(h.wire.writes.last().first); h.reply("030302001e418c");
        QCOMPARE(h.session.operation(manual).update.result.value("unit").toInt(), 3);
        h.advance(1750); QCOMPARE(h.wire.writes.size(), 3);
        QCOMPARE(h.wire.writes.last().second.toHex(), "0203000000018439");
        h.sent(h.wire.writes.last().first); h.reply("0203020014fc4b");
        QList<int> units;
        for (const auto &message : h.session.messages()) if (message.status == "success") units.append(message.fields.value("unit").toInt());
        QCOMPARE(units, QList<int>({1, 3, 2}));
        h.advance(50000); QCOMPARE(h.wire.writes.size(), 4); // 第一个设备的周期，不是重试间隔。
        QCOMPARE(h.wire.writes.last().second[0], char(1));
        h.session.stop(); const auto count = h.wire.writes.size(); h.advance(10000000);
        QCOMPARE(h.wire.writes.size(), count);
    }
    void exhaustedDeviceDoesNotStrandOtherPolls() {
        Harness h(catalog); QVERIFY(h.start({{"extraReadRetries", 0}, {"responseTimeoutMs", 10},
            {"retryIntervalMs", 1}, {"polls", QJsonArray{task(1), task(2)}}}));
        h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(10000);
        h.advance(11000); h.advance(1750);
        QCOMPARE(h.wire.writes.size(), 2);
        QCOMPARE(h.wire.writes.last().second[0], char(2));
        h.sent(h.wire.writes.last().first); h.reply("0203020014fc4b");
        QCOMPARE(h.session.messages().last().status, "success");
    }
    void boundedQueueAndStop() {
        Harness h(catalog); QVERIFY(h.start());
        const auto first = h.session.submit("read-holding", readOne);
        QList<quint64> accepted;
        for (int i = 0; i < 64; ++i) accepted.append(h.session.submit("read-holding", readOne));
        const auto rejected = h.session.submit("read-holding", readOne);
        QCOMPARE(h.session.operation(rejected).update.state, "invalid");
        h.session.stop();
        QCOMPARE(h.session.operation(first).update.state, "cancelled");
        for (const auto id : accepted) QCOMPARE(h.session.operation(id).update.state, "cancelled");
        h.advance(1000000); QVERIFY(h.wire.writes.isEmpty());
    }
    void writeCannotEnterRetryOrPollPath() {
        Harness h(catalog); QVERIFY(h.start({{"responseTimeoutMs", 10}, {"extraReadRetries", 10}}));
        const auto id = h.session.submit("write-single-register", {{"unit", 1}, {"start", 0}, {"value", 7}});
        h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(10000); h.advance(10000000);
        QCOMPARE(h.wire.writes.size(), 1);
        QCOMPARE(h.session.operation(id).update.state, "timeout");
        QVERIFY(h.session.operation(id).update.result.value("uncertain").toBool());
        h.session.stop();
        QVERIFY(!h.start({{"polls", QJsonArray{QJsonObject{{"unit", 1}, {"action", "write-single-register"},
            {"start", 0}, {"count", 1}, {"intervalMs", 100}}}}}));
        QCOMPARE(h.wire.writes.size(), 1);
    }
    void invalidSettingsDoNotOpenTransport() {
        for (const auto &settings : QList<QJsonObject>{
            {{"extraReadRetries", -1}}, {{"extraReadRetries", 11}}, {{"extraReadRetries", 1.5}},
            {{"retryIntervalMs", -1}}, {{"retryIntervalMs", "100"}}, {{"responseTimeoutMs", 0}},
            {{"polls", QJsonObject{}}}, {{"polls", QJsonArray{task(0)}}}, {{"polls", QJsonArray{task(1, 0)}}}}) {
            Harness h(catalog); QVERIFY(!h.start(settings));
            QVERIFY(h.wire.writes.isEmpty()); QVERIFY(!h.session.diagnostic().isEmpty());
        }
    }
    void runtimeSettingsPersistAndTransportFailureDoesNotRetry() {
        Harness h(catalog); QVERIFY(h.start());
        const QJsonObject settings{{"extraReadRetries", 0}, {"responseTimeoutMs", 40}, {"retryIntervalMs", 7}};
        const auto configuration = h.session.submit("configure-polling", settings);
        QCOMPARE(h.session.operation(configuration).update.state, "success");
        QCOMPARE(h.session.config().protocol, settings);
        const auto id = h.session.submit("read-holding", readOne);
        h.advance(1750);
        h.wire.events.sent(h.wire.writes.last().first, false, QStringLiteral("受控发送失败"));
        QCoreApplication::processEvents();
        QCOMPARE(h.session.operation(id).update.state, "transport-error");
        h.advance(10000000); QCOMPARE(h.wire.writes.size(), 1);
        const auto rejected = h.session.submit("configure-polling", {{"extraReadRetries", 99}});
        QCOMPARE(h.session.operation(rejected).update.state, "invalid");
        QCOMPARE(h.session.config().protocol, settings);
    }
    void previousLogicalTimeoutHasUncertainFreshness() {
        Harness h(catalog); QVERIFY(h.start({{"extraReadRetries", 0}, {"responseTimeoutMs", 10}, {"retryIntervalMs", 1}}));
        const auto first = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(10000);
        QCOMPARE(h.session.operation(first).update.state, "timeout");
        const auto next = h.session.submit("read-holding", readOne);
        h.advance(11000); h.sent(h.wire.writes.last().first); h.reply("010302000a3843");
        QVERIFY(h.session.operation(next).update.result.value("freshnessUncertain").toBool());
        QCOMPARE(h.session.messages(next).last().operationId, quint64(0));
        QVERIFY(h.session.operation(next).update.detail.contains(QStringLiteral("候选值")));
        const auto manual = h.session.submit("read-holding", {{"unit", 2}, {"start", 0}, {"count", 1}, {"_pollIndex", 0}});
        h.advance(1750); h.sent(h.wire.writes.last().first); h.reply("0203020014fc4b");
        QCOMPARE(h.session.messages(manual).last().operationId, manual);
        QVERIFY(!h.session.operation(manual).update.result.value("freshnessUncertain").toBool());
    }
    void manyTimeoutShapesStayConservativelyUncertain() {
        Harness h(catalog); QVERIFY(h.start({{"extraReadRetries", 0}, {"responseTimeoutMs", 1}, {"retryIntervalMs", 0}}));
        for (int count = 1; count <= 65; ++count) {
            const auto id = h.session.submit("read-holding", {{"unit", 1}, {"start", 0}, {"count", count}});
            h.advance(1750); h.sent(h.wire.writes.last().first); h.advance(1000);
            QCOMPARE(h.session.operation(id).update.state, "timeout");
            h.advance(1000);
        }
        const auto next = h.session.submit("read-holding", {{"unit", 2}, {"start", 0}, {"count", 1}});
        h.advance(1750); h.sent(h.wire.writes.last().first);
        h.reply("0203020014fc4b");
        QVERIFY(h.session.operation(next).update.result.value("freshnessUncertain").toBool());
        QCOMPARE(h.session.messages(next).last().operationId, quint64(0));
    }
    void crcFailureRetriesOnlyAfterDeadline() {
        Harness h(catalog); QVERIFY(h.start({{"responseTimeoutMs", 10}, {"retryIntervalMs", 2}}));
        const auto id = h.session.submit("read-holding", readOne);
        h.advance(1750); h.sent(h.wire.writes.last().first); h.reply("010302000a3842");
        QCOMPARE(h.session.operation(id).update.state, "sent");
        QCOMPARE(h.wire.writes.size(), 1);
        h.advance(8250); QCOMPARE(h.session.operation(id).update.state, "retry-wait");
        h.advance(1999); QCOMPARE(h.wire.writes.size(), 1);
        h.advance(1); QCOMPARE(h.wire.writes.size(), 2);
        h.sent(h.wire.writes.last().first); h.reply("010302000a3843");
        QCOMPARE(h.session.operation(id).update.state, "success");
    }
    void widgetConfiguresTwoDevicesAndObserves() {
        Wire wire;
        Workbench window(catalog, nullptr, wire.factory());
        wire.written = [&](quint64 id, const QByteArray &bytes) {
            wire.events.sent(id, true, {});
            const auto response = QByteArray::fromHex(bytes[0] == char(1) ? "010302000a3843" : "0203020014fc4b");
            QTimer::singleShot(10, &window, [&, response] { wire.events.received(response); });
        };
        window.resize(1000, 900); window.show();
        QTest::mouseClick(window.findChild<QPushButton *>("pollSettings"), Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>("pollSettingsDialog"); QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QSpinBox *>("extraReadRetries")->value(), 2);
        QCOMPARE(dialog->findChild<QSpinBox *>("responseTimeoutMs")->value(), 1000);
        QCOMPARE(dialog->findChild<QSpinBox *>("retryIntervalMs")->value(), 100);
        dialog->findChild<QSpinBox *>("extraReadRetries")->setValue(0);
        dialog->findChild<QSpinBox *>("pollIntervalMs")->setValue(500);
        QTest::mouseClick(dialog->findChild<QPushButton *>("addPollTask"), Qt::LeftButton);
        window.findChild<QSpinBox *>("unit")->setValue(2);
        QTest::mouseClick(dialog->findChild<QPushButton *>("addPollTask"), Qt::LeftButton);
        QCOMPARE(dialog->findChild<QTableWidget *>("pollTasks")->rowCount(), 2);
        QTest::mouseClick(dialog->findChild<QPushButton *>("applyPollSettings"), Qt::LeftButton);
        QVERIFY(dialog->findChild<QLabel *>("pollSettingsStatus")->text().contains(QStringLiteral("启动后生效")));
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(dialog->grab().save(output + "/poll-configuration-controlled.png"));
        dialog->close();
        QTest::mouseClick(window.findChild<QPushButton *>("start"), Qt::LeftButton);
        auto *table = window.findChild<QTableWidget *>("masterObservations");
        QTRY_COMPARE(table->rowCount(), 2);
        QVERIFY(wire.writes.size() >= 2);
        QCOMPARE(table->item(0, 0)->text(), "1");
        QCOMPARE(table->item(1, 0)->text(), "2");
        QVERIFY(table->item(0, 3)->text().contains("10")); QVERIFY(table->item(1, 3)->text().contains("20"));
        auto *scroll = window.findChild<QScrollArea *>(); QVERIFY(scroll);
        scroll->ensureWidgetVisible(table);
        QTest::qWait(50);
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/poll-observations-controlled.png"));
        QTest::mouseClick(window.findChild<QPushButton *>("stop"), Qt::LeftButton);
        const auto count = wire.writes.size(); QTest::qWait(600); QCOMPARE(wire.writes.size(), count);
    }
};
QTEST_MAIN(PollTests)
#include "rtu_poll_tests.moc"
