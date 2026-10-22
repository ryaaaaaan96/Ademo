#include "debugger/host.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

using namespace debugger;
namespace {
SessionConfig serialConfig(const QString &baud = "9600")
{
    return {"modbus.rtu", "serial", "master", {{"port", "COM9999"}, {"baud", baud},
            {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
}
const QJsonObject twoRegisters{{"unit", 1}, {"start", 0}, {"count", 2}};
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> writes;
    bool accept = true;
    std::function<void(quint64)> onWrite;
    class Port final : public Endpoint {
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
    private:
        Wire &wire_;
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
    std::optional<qint64> previousReceiveUs;
    explicit Harness(std::shared_ptr<PluginCatalog> catalog)
        : session(std::move(catalog), nullptr, wire.factory(), [this] { return time; }) {}
    bool start(const QString &baud = "9600") {
        if (!session.start(serialConfig(baud))) return false;
        QCoreApplication::processEvents();
        return session.state() == SessionState::Running;
    }
    void advance(qint64 us) {
        time += us;
        QCoreApplication::processEvents();
        session.poll();
        QCoreApplication::processEvents();
    }
    void localSent(quint64 id, bool ok = true) {
        wire.events.sent(id, ok, ok ? QString() : QStringLiteral("后端发送失败"));
        QCoreApplication::processEvents();
    }
    void receive(const QByteArray &bytes) {
        wire.events.received(ReceiveChunk(bytes, previousReceiveUs ? time - *previousReceiveUs : 0));
        previousReceiveUs = time;
        QCoreApplication::processEvents();
    }
};
}
class ReadTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
        QVERIFY(catalog->transport("serial"));
        qInfo().noquote() << buildKey();
    }
    void readThroughSessionAndTraceSources() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        QCOMPARE(h.session.operation(id).update.state, "queued");
        const auto busy = h.session.submit("read-holding", twoRegisters);
        QCOMPARE(h.session.operation(busy).update.state, "invalid");
        h.advance(4010);
        QVERIFY(h.wire.writes.isEmpty());
        h.advance(1);
        QCOMPARE(h.wire.writes.size(), 1);
        QCOMPARE(h.wire.writes.first().second.toHex(), "010300000002c40b");
        QCOMPARE(h.session.operation(id).update.state, "submitted");
        QVERIFY(h.session.operation(id).sendAccepted);
        QVERIFY(!h.session.operation(id).localSent);
        h.localSent(id);
        QCOMPARE(h.session.operation(id).update.state, "sent");
        QVERIFY(!h.session.operation(id).finished);
        h.receive(QByteArray::fromHex("01030400"));
        h.advance(500);
        h.receive(QByteArray::fromHex("0a0014da3e"));
        h.advance(4010);
        QCOMPARE(h.session.operation(id).update.state, "sent");
        h.advance(1);
        QCOMPARE(h.session.operation(id).update.state, "success");
        QCOMPARE(h.session.operation(id).update.result.value("values").toArray(), QJsonArray({10, 20}));
        const auto message = h.session.messages(id).last();
        QCOMPARE(message.operationId, id);
        QCOMPARE(message.sources.size(), 2);
        QByteArray sourceBytes;
        for (auto ref : message.sources) {
            QVERIFY(h.session.trace(ref).has_value());
            const auto record = *h.session.trace(ref);
            QCOMPARE(record.kind, "rx");
            QCOMPARE(record.operationId, quint64(0));
            sourceBytes += record.bytes;
        }
        QCOMPARE(sourceBytes.toHex(), "010304000a0014da3e");
        QStringList kinds;
        for (const auto &record : h.session.traces(id)) kinds << record.kind;
        QVERIFY(kinds.contains("send-request") && kinds.contains("send-accepted") && kinds.contains("local-sent"));
    }
    void specificationVectorAndZeroBasedAddress() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", {{"unit", 17}, {"start", 107}, {"count", 3}});
        h.advance(5000);
        QCOMPARE(h.wire.writes.last().second.toHex(), "1103006b00037687");
        h.localSent(id);
        // 应用协议 §6.3 示例 PDU：006B / 0003 -> 022B / 0000 / 0064，外加 RTU 地址/CRC。
        h.receive(QByteArray::fromHex("110306022b00000064c8ba"));
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.result.value("values").toArray(), QJsonArray({555, 0, 100}));
        QCOMPARE(h.session.operation(id).update.result.value("start").toInt(), 107);
    }
    void invalidRequests_data() {
        QTest::addColumn<QJsonObject>("parameters");
        for (const auto &item : QList<QPair<const char *, QJsonObject>>{
            {"broadcast", {{"unit", 0}, {"start", 0}, {"count", 1}}},
            {"unit248", {{"unit", 248}, {"start", 0}, {"count", 1}}},
            {"negative", {{"unit", 1}, {"start", -1}, {"count", 1}}},
            {"zero-count", {{"unit", 1}, {"start", 0}, {"count", 0}}},
            {"count126", {{"unit", 1}, {"start", 0}, {"count", 126}}},
            {"end-overflow", {{"unit", 1}, {"start", 65535}, {"count", 2}}},
            {"fraction", {{"unit", 1.5}, {"start", 0}, {"count", 1}}},
            {"missing", {}}}) QTest::newRow(item.first) << item.second;
    }
    void invalidRequests() {
        QFETCH(QJsonObject, parameters);
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", parameters);
        QCOMPARE(h.session.operation(id).update.state, "invalid");
        h.advance(10000);
        QVERIFY(h.wire.writes.isEmpty());
        QVERIFY(!h.session.operation(id).update.detail.isEmpty());
    }
    void responseErrors_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("bad-crc") << QByteArray::fromHex("010304000a0014da3f");
        QTest::newRow("wrong-unit") << QByteArray::fromHex("020304000a0014e93e");
        QTest::newRow("wrong-function") << QByteArray::fromHex("010404000a0014db89");
        QTest::newRow("wrong-byte-count") << QByteArray::fromHex("010302000a3843");
        QTest::newRow("truncated") << QByteArray::fromHex("01030400");
        QTest::newRow("noise") << QByteArray::fromHex("ffffffffff");
        QTest::newRow("no-gap-concatenation") << QByteArray::fromHex("010304000a0014da3e010304000a0014da3e");
    }
    void responseErrors() {
        QFETCH(QByteArray, bytes);
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.localSent(id);
        h.receive(bytes);
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.state, "sent");
        QVERIFY(!h.session.messages().isEmpty());
        QCOMPARE(h.session.messages().last().operationId, quint64(0));
        bool preserved = false;
        for (const auto &record : h.session.traces(id)) if (record.bytes == bytes) preserved = true;
        QVERIFY(preserved);
        h.advance(1000000);
        QCOMPARE(h.session.operation(id).update.state, "timeout");
        QCOMPARE(h.wire.writes.size(), 1); // 不实现 05 的读取重试。
        const auto retry = h.session.submit("read-holding", twoRegisters);
        QCOMPARE(h.session.operation(retry).update.state, "invalid");
        QVERIFY(h.session.operation(retry).update.detail.contains(QStringLiteral("迟到")));
    }
    void exceptionIsNotTimeout() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.localSent(id);
        h.receive(QByteArray::fromHex("018302c0f1"));
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.state, "exception");
        QCOMPARE(h.session.operation(id).update.result.value("exceptionCode").toInt(), 2);
        QCOMPARE(h.session.messages(id).last().operationId, id);
        h.advance(1000000);
        QCOMPARE(h.wire.writes.size(), 1);
        QCOMPARE(h.session.operation(id).update.state, "exception");
    }
    void silenceBoundaries_data() {
        QTest::addColumn<QString>("baud");
        QTest::addColumn<qint64>("gap");
        QTest::newRow("9600") << QString("9600") << qint64(4011);
        QTest::newRow("19200") << QString("19200") << qint64(2006);
        QTest::newRow("38400-fixed") << QString("38400") << qint64(1750);
        QTest::newRow("115200-fixed") << QString("115200") << qint64(1750);
    }
    void silenceBoundaries() {
        QFETCH(QString, baud);
        QFETCH(qint64, gap);
        Harness h(catalog);
        QVERIFY(h.start(baud));
        h.session.submit("read-holding", twoRegisters);
        h.advance(gap - 1);
        QVERIFY(h.wire.writes.isEmpty());
        h.advance(1);
        QCOMPARE(h.wire.writes.size(), 1);
    }
    void interCharacterGapAndRecovery() {
        Harness h(catalog);
        QVERIFY(h.start("115200"));
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(1750);
        h.localSent(id);
        h.receive(QByteArray::fromHex("01030400"));
        h.advance(751); // > t1.5，但不足 t3.5。
        h.receive(QByteArray::fromHex("0a0014da3e"));
        h.advance(1750);
        QCOMPARE(h.session.operation(id).update.state, "sent");
        QVERIFY(h.session.messages(id).last().detail.contains("t1.5"));
        h.receive(QByteArray::fromHex("01030400"));
        h.advance(750); // 等于 t1.5，仍可属于同帧。
        h.receive(QByteArray::fromHex("0a0014da3e"));
        h.advance(1750);
        QCOMPARE(h.session.operation(id).update.state, "success");
    }
    void sendFailureCancellationAndOldEvents() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto first = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.localSent(first, false);
        QCOMPARE(h.session.operation(first).update.state, "transport-error");
        const auto old = h.wire.events;
        h.session.stop();
        QVERIFY(h.start());
        const auto second = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        old.sent(first, true, {});
        old.received(QByteArray::fromHex("010304000a0014da3e"));
        h.advance(5000);
        QCOMPARE(h.session.operation(second).update.state, "submitted");
        QVERIFY(!h.session.operation(second).localSent);
        h.session.stop();
        QCOMPARE(h.session.operation(second).update.state, "cancelled");
        QVERIFY(h.session.operation(second).finished);
        QVERIFY(second > first);
    }
    void localSendCannotConfirmDevice() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.receive(QByteArray::fromHex("010304000a0014da3e"));
        h.advance(5000);
        QVERIFY(!h.session.operation(id).finished);
        h.localSent(id);
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.state, "success");
        const auto traces = h.session.traces().size();
        h.localSent(id);
        QCOMPARE(h.session.traces().size(), traces); // 最终本地结果不重复。
    }
    void delayedHostChunksDoNotImplyWireSilence() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.localSent(id);
        h.wire.events.received(QByteArray::fromHex("01030400")); // 无可信线路时间，模拟 QSerialPort。
        h.advance(5000); // OS 分块交付相隔 > t3.5，线上可能始终连续。
        QVERIFY(h.session.messages(id).isEmpty());
        h.wire.events.received(QByteArray::fromHex("0a0014da3e"));
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.state, "success");
    }
    void idleNoiseDoesNotStrandQueuedRead() {
        Harness h(catalog);
        QVERIFY(h.start());
        h.wire.events.received(QByteArray::fromHex("ff"));
        QCoreApplication::processEvents();
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        QCOMPARE(h.session.operation(id).update.state, "submitted");
        QCOMPARE(h.wire.writes.size(), 1);
        QVERIFY(!h.session.messages().isEmpty());
        QCOMPARE(h.session.messages().last().operationId, quint64(0));
        QVERIFY(h.session.messages(id).isEmpty()); // 提交前的噪声不伪造操作归属。
        h.session.stop();
        QVERIFY(h.start());
        const auto busy = h.session.submit("read-holding", twoRegisters);
        const auto writesBefore = h.wire.writes.size();
        for (int i = 0; i < 1000; ++i) {
            h.time += 1000;
            h.wire.events.received(QByteArray(1, char(0xff)));
            QCoreApplication::processEvents();
            h.session.poll();
        }
        QCOMPARE(h.session.operation(busy).update.state, "timeout");
        QVERIFY(!h.session.operation(busy).sendAccepted);
        QCOMPARE(h.wire.writes.size(), writesBefore);
    }
    void localCompletionDeadlineUsesObservationTime() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.time += 1000001; // 未 poll，迟到回调不能重新获取期限。
        h.localSent(id);
        QCOMPARE(h.session.operation(id).update.state, "transport-error");
        h.session.stop();
        QVERIFY(h.start());
        const auto next = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.wire.events.sent(next, true, {}); // 及时观测，但宿主稍后才处理。
        h.time += 1000001;
        QCoreApplication::processEvents();
        h.session.poll();
        QCOMPARE(h.session.operation(next).update.state, "timeout");
    }
    void sendRejectedAndLocalSendTimeout() {
        Harness h(catalog);
        QVERIFY(h.start());
        h.wire.accept = false;
        const auto rejected = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        QCOMPARE(h.session.operation(rejected).update.state, "transport-error");
        QVERIFY(!h.session.operation(rejected).sendAccepted);
        h.wire.accept = true;
        const auto pending = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.advance(1000000);
        QCOMPARE(h.session.operation(pending).update.state, "transport-error");
        QVERIFY(!h.session.operation(pending).localSent);
    }
    void boundedRawCacheAndOversizedFrame() {
        Harness h(catalog);
        QVERIFY(h.start());
        const auto id = h.session.submit("read-holding", twoRegisters);
        h.advance(5000);
        h.localSent(id);
        const auto firstTrace = h.session.traces(id).first().id;
        for (int i = 0; i < 300; ++i) h.receive(QByteArray(1, char(0xff)));
        h.advance(5000);
        QCOMPARE(h.session.traces().size(), 256);
        QVERIFY(h.session.expiredTraceCount() > 0);
        QVERIFY(!h.session.trace(firstTrace).has_value());
        QVERIFY(h.session.messages(id).last().detail.contains("256"));
        const auto oldSources = h.session.messages(id).last().sources;
        QVERIFY(!oldSources.isEmpty());
        QVERIFY(!h.session.trace(oldSources.first()).has_value());
        h.advance(1000000);
        QCOMPARE(h.session.operation(id).update.state, "timeout");
    }
    void qtTimerDrivesProtocolWithoutManualPoll() {
        Wire wire;
        QObject lifetime;
        wire.onWrite = [&](quint64 id) {
            wire.events.sent(id, true, {});
            QTimer::singleShot(10, &lifetime, [&] { wire.events.received(QByteArray::fromHex("010304000a0014da3e")); });
        };
        DebugSession session(catalog, nullptr, wire.factory());
        QVERIFY(session.start(serialConfig("115200")));
        QTRY_COMPARE(session.state(), SessionState::Running);
        const auto id = session.submit("read-holding", twoRegisters);
        QTRY_COMPARE(session.operation(id).update.state, "success");
    }
    void widgetReadFlow_data() {
        QTest::addColumn<QString>("outcome");
        QTest::newRow("success") << QString("success");
        QTest::newRow("exception") << QString("exception");
        QTest::newRow("timeout") << QString("timeout");
    }
    void widgetReadFlow() {
        QFETCH(QString, outcome);
        Wire wire;
        Workbench window(catalog, nullptr, wire.factory());
        wire.onWrite = [&](quint64 id) {
            wire.events.sent(id, true, {});
            if (outcome != "timeout") QTimer::singleShot(10, &window, [&] {
                wire.events.received(QByteArray::fromHex(outcome == "success" ? "110306022b00000064c8ba" : "118302c134"));
            });
        };
        window.resize(820, 860);
        window.show();
        auto *start = window.findChild<QPushButton *>("start");
        auto *read = window.findChild<QPushButton *>("readHolding");
        auto *result = window.findChild<QLabel *>("readResult");
        QVERIFY(start && read && result);
        window.findChild<QSpinBox *>("unit")->setValue(17);
        window.findChild<QSpinBox *>("registerStart")->setValue(107);
        window.findChild<QSpinBox *>("registerCount")->setValue(3);
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton);
        QTRY_VERIFY(result->text().contains(outcome));
        QCOMPARE(wire.writes.size(), 1);
        QCOMPARE(wire.writes.last().second.toHex(), "1103006b00037687");
        if (outcome == "success") QVERIFY(result->text().contains("PDU 107 = 555"));
        auto *details = window.findChild<QPlainTextEdit *>("operationTrace");
        QVERIFY(details->toPlainText().contains("send-request"));
        QVERIFY(details->toPlainText().contains("local-sent"));
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/read-" + outcome + "-controlled.png"));
    }
};
QTEST_MAIN(ReadTests)
#include "rtu_read_tests.moc"
