#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QQuickWidget>
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
    bytes.append(char(crc)); bytes.append(char(crc >> 8)); return bytes;
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
QJsonObject parameter(int unit, int address, const QString &name, const QString &displayUnit = "V") {
    return {{"unit", unit}, {"area", "holding"}, {"address", address}, {"name", name},
        {"displayUnit", displayUnit}, {"group", "测量"}};
}
SessionConfig configuration() {
    return {"modbus.rtu", "serial", "master", {{"port", "COM120"}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}},
        {{"parameterRows", QJsonArray{parameter(17, 107, "电压A"), parameter(17, 108, "电压B"),
            parameter(18, 107, "另一设备")}}}, {}};
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
        view->resize(1300, 900); view->show(); session.start(config); QCoreApplication::processEvents();
    }
    void advance(qint64 us = 5000) { time += us; session.poll(); QCoreApplication::processEvents(); }
    void reply(const QByteArray &payload) {
        wire.events.sent(wire.requests.last().first, true, {});
        wire.events.received(frame(payload)); QCoreApplication::processEvents(); advance();
    }
    void selectRows() {
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        table->selectAll();
    }
};
QMainWindow *trendWindow() {
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<QMainWindow *>(widget); window && window->windowTitle().contains(QStringLiteral("参数趋势"))) return window;
    return nullptr;
}
}
class ParameterTrendTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void selectedParametersOpenRealQmlWithoutSendingAndKeepTheirDevice() {
        Harness h(catalog); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        auto *open = button(*h.view, QStringLiteral("绘制选中参数"));
        QVERIFY2(open, "参数操作页需要明确打开所选参数趋势的入口");
        QTest::mouseClick(open, Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow();
        auto *qml = window->findChild<QQuickWidget *>(); QVERIFY(qml);
        QTRY_COMPARE(qml->status(), QQuickWidget::Ready);
        QCOMPARE(h.wire.requests.size(), 0);
        QVERIFY(window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(window->windowFlags().testFlag(Qt::WindowCloseButtonHint));
        auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要")); QVERIFY(summary);
        QVERIFY(summary->text().contains(QStringLiteral("电压A"))); QVERIFY(summary->text().contains(QStringLiteral("电压B")));
        QTRY_VERIFY(button(*h.view, QStringLiteral("读取当前组"))->isEnabled());
        QTest::mouseClick(button(*h.view, QStringLiteral("读取当前组")), Qt::LeftButton); h.advance();
        QCOMPARE(h.wire.requests.size(), 1); h.reply(QByteArray::fromHex("110304022b029a"));
        QTRY_VERIFY(summary->text().contains("555 V")); QTRY_VERIFY(summary->text().contains("666 V"));
        QVERIFY(h.view->selectDevice("18"));
        QTest::mouseClick(button(*h.view, QStringLiteral("读取当前组")), Qt::LeftButton); h.advance();
        h.reply(QByteArray::fromHex("1203020309")); QTest::qWait(150);
        QVERIFY(!summary->text().contains("777 V")); QVERIFY(summary->text().contains("555 V"));
        QCOMPARE(h.wire.requests.size(), 2);
        QVERIFY(!qml->grabFramebuffer().isNull());
        if (const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR"); !output.isEmpty()) QVERIFY(window->grab().save(output + "/qml-device-17.png"));
        window->close(); QCoreApplication::processEvents(); QCOMPARE(h.session.state(), SessionState::Running);
    }
    void pauseLegendZoomAndClearOnlyAffectDisplay() {
        Harness h(catalog); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow(); auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要"));
        auto *pause = button(*window, QStringLiteral("暂停显示")); QVERIFY2(pause, "需要独立的暂停显示控制");
        accessible<QComboBox>(*h.view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        accessible<QSpinBox>(*h.view, QStringLiteral("轮询周期"))->setValue(100);
        auto *start = button(*h.view, QStringLiteral("开始轮询")); QTRY_VERIFY(start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton); h.advance(); h.reply(QByteArray::fromHex("110304022b029a"));
        QTRY_VERIFY(summary->text().contains("555 V"));
        const auto before = summary->text(); QTest::mouseClick(pause, Qt::LeftButton);
        h.advance(100000); QCOMPARE(h.wire.requests.size(), 2); h.reply(QByteArray::fromHex("11030403090378"));
        QTest::qWait(220); QCOMPARE(summary->text(), before);
        QCOMPARE(accessible<QTableWidget>(*h.view, QStringLiteral("参数表"))->item(0, 3)->text(), QString("777"));
        QTest::mouseClick(button(*window, QStringLiteral("继续显示")), Qt::LeftButton);
        QTRY_VERIFY(summary->text().contains("777 V"));
        auto *legend = accessible<QCheckBox>(*window, QStringLiteral("显示曲线 电压A")); QVERIFY(legend);
        QTest::mouseClick(legend, Qt::LeftButton); QTRY_VERIFY(summary->text().contains(QStringLiteral("电压A（隐藏）")));
        auto *range = accessible<QLabel>(*window, QStringLiteral("显示时间范围")); QVERIFY(range); QVERIFY(range->text().contains("60"));
        QTest::mouseClick(button(*window, QStringLiteral("放大时间轴")), Qt::LeftButton); QVERIFY(range->text().contains("30"));
        QTest::mouseClick(button(*window, QStringLiteral("缩小时间轴")), Qt::LeftButton); QVERIFY(range->text().contains("60"));
        const auto traceCount = h.session.traces().size(), messageCount = h.session.messages().size();
        QTest::mouseClick(button(*window, QStringLiteral("清空曲线")), Qt::LeftButton);
        QTRY_VERIFY(summary->text().contains(QStringLiteral("0 点"))); QTest::qWait(220);
        QVERIFY(!summary->text().contains("777 V"));
        QCOMPARE(h.session.traces().size(), traceCount); QCOMPARE(h.session.messages().size(), messageCount);
        QCOMPARE(accessible<QTableWidget>(*h.view, QStringLiteral("参数表"))->item(0, 3)->text(), QString("777"));
        h.advance(100000); QCOMPARE(h.wire.requests.size(), 3); h.reply(QByteArray::fromHex("11030403e70457"));
        QTRY_VERIFY(summary->text().contains("999 V")); window->close();
    }
    void clearDoesNotReplaySamplesWaitingForDisplayRefresh() {
        Harness h(catalog); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow(); auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要"));
        QTRY_VERIFY(button(*h.view, QStringLiteral("读取当前组"))->isEnabled());
        QTest::mouseClick(button(*h.view, QStringLiteral("读取当前组")), Qt::LeftButton); h.advance();
        h.reply(QByteArray::fromHex("110304022b029a"));
        QTest::mouseClick(button(*window, QStringLiteral("清空曲线")), Qt::LeftButton); QTest::qWait(250);
        QVERIFY2(!summary->text().contains("555 V"), "清空之前已收到但还未显示的样本也不能重新出现"); window->close();
    }
    void capacityRefusesTooManySeriesInsteadOfSilentlyTruncating() {
        auto config = configuration(); QJsonArray rows;
        for (int i = 0; i < 33; ++i) rows.append(parameter(17, i, QStringLiteral("参数%1").arg(i)));
        config.protocol["parameterRows"] = rows;
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton);
        QVERIFY2(!trendWindow(), "超出明确容量应拒绝整个操作，不应无界添加或静默截断");
        bool explained = false;
        for (auto *label : h.view->findChildren<QLabel *>()) explained |= label->text().contains(QStringLiteral("最多 32"));
        QVERIFY(explained); QCOMPARE(h.wire.requests.size(), 0);
    }
    void windowCapacityIsReleasedByClosingAPlot() {
        Harness h(catalog); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        auto count = [] {
            int found = 0; for (auto *widget : QApplication::topLevelWidgets())
                if (qobject_cast<QMainWindow *>(widget) && widget->windowTitle().contains(QStringLiteral("参数趋势"))) ++found;
            return found;
        };
        for (int i = 0; i < 9; ++i) QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton);
        QCOMPARE(count(), 8); QCOMPARE(h.wire.requests.size(), 0);
        trendWindow()->close(); QTRY_COMPARE(count(), 7);
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QCOMPARE(count(), 8);
    }
    void equalAddressesInDifferentAreasRemainDifferentCurves() {
        auto config = configuration(); auto input = parameter(17, 107, QStringLiteral("输入电压")); input["area"] = "input";
        config.protocol["parameterRows"] = QJsonArray{parameter(17, 107, QStringLiteral("保持电压")), input};
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow(); auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要"));
        const QJsonObject scope{{"unit", 17}, {"start", 107}, {"count", 1}};
        h.session.submit("read-holding", scope); h.advance(); h.reply(QByteArray::fromHex("110302022b"));
        h.session.submit("read-input", scope); h.advance(); h.reply(QByteArray::fromHex("1104020309"));
        QTRY_VERIFY(summary->text().contains(QStringLiteral("保持电压 · 17/holding/107 · 555 V")));
        QTRY_VERIFY(summary->text().contains(QStringLiteral("输入电压 · 17/input/107 · 777 V"))); window->close();
    }
    void unsuccessfulUnsolicitedAndUncertainValuesAreNotSamples() {
        auto config = configuration(); config.protocol["responseTimeoutMs"] = 10; config.protocol["extraReadRetries"] = 0; config.protocol["retryIntervalMs"] = 1;
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow(); auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要"));
        const QJsonObject scope{{"unit", 17}, {"start", 107}, {"count", 2}};
        h.session.submit("read-holding", scope); h.advance(); h.reply(QByteArray::fromHex("110304022b029a"));
        QTRY_VERIFY(summary->text().contains("555 V")); const auto accepted = summary->text();
        h.advance(); h.wire.events.received(frame(QByteArray::fromHex("11030403090378"))); QCoreApplication::processEvents(); h.advance();
        QVERIFY(!h.session.messages().last().fields.value("operationResult").toBool());
        h.session.submit("read-holding", scope); h.advance(); h.reply(QByteArray::fromHex("118302"));
        QTest::qWait(220); QCOMPARE(summary->text(), accepted);
        const auto timedOut = h.session.submit("read-holding", scope); h.advance();
        h.wire.events.sent(h.wire.requests.last().first, true, {}); QCoreApplication::processEvents(); h.advance(10000);
        QCOMPARE(h.session.operation(timedOut).update.state, QString("timeout"));
        const auto uncertain = h.session.submit("read-holding", scope); h.advance(11000);
        h.reply(QByteArray::fromHex("11030403090378"));
        QVERIFY(h.session.operation(uncertain).update.result.value("freshnessUncertain").toBool());
        QTest::qWait(220); QCOMPARE(summary->text(), accepted); window->close();
    }
    void maximumCurvesRemainBoundedWhileRtuContinues() {
        auto config = configuration(); QJsonArray rows;
        for (int i = 0; i < 32; ++i) rows.append(parameter(17, i, QStringLiteral("负载参数%1").arg(i), i % 2 ? "mA" : "V"));
        config.protocol["parameterRows"] = rows;
        Harness h(catalog, config); QTRY_COMPARE(h.session.state(), SessionState::Running); h.selectRows();
        QTest::mouseClick(button(*h.view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        auto *window = trendWindow(); auto *summary = accessible<QLabel>(*window, QStringLiteral("曲线数据概要"));
        QElapsedTimer elapsed; elapsed.start();
        for (int sample = 0; sample < 2200; ++sample) {
            const auto id = h.session.submit("read-holding", {{"unit", 17}, {"start", 0}, {"count", 32}}); QVERIFY(id); h.advance();
            QByteArray payload = QByteArray::fromHex("110340");
            for (int curve = 0; curve < 32; ++curve) { int value = sample + curve * 1000; payload.append(char(value >> 8)); payload.append(char(value)); }
            h.reply(payload); QCOMPARE(h.session.operation(id).update.state, QString("success"));
            if (sample % 32 == 31) QTest::qWait(110);
        }
        QTRY_COMPARE(summary->text().count(QStringLiteral("2048 点")), 32);
        QCOMPARE(h.wire.requests.size(), 2200); QTRY_VERIFY(summary->text().contains("33199 mA"));
        QCOMPARE(h.session.state(), SessionState::Running);
        qInfo() << "2200 completed real RTU requests; 32 curves each capped at 2048 samples; elapsed ms" << elapsed.elapsed();
        auto *qml = window->findChild<QQuickWidget *>(); QCOMPARE(qml->status(), QQuickWidget::Ready); QVERIFY(!qml->grabFramebuffer().isNull());
        if (const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR"); !output.isEmpty()) QVERIFY(window->grab().save(output + "/qml-capacity.png"));
        window->close();
    }
    void floatingAndDockingPreserveQuickWidgetAndViewDestructionClosesPlot() {
        auto workbench = std::make_unique<Workbench>(catalog); workbench->resize(1300, 900); workbench->show();
        const int index = workbench->addSession(configuration(), QStringLiteral("趋势归属")); workbench->selectSession(index);
        auto *view = workbench->parameterView(index); QVERIFY(view);
        accessible<QTableWidget>(*view, QStringLiteral("参数表"))->selectAll();
        QTest::mouseClick(button(*view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        QPointer<QMainWindow> window = trendWindow(); QPointer<QQuickWidget> qml = window->findChild<QQuickWidget *>(); QVERIFY(qml);
        QTRY_COMPARE(qml->status(), QQuickWidget::Ready); QCOMPARE(window->parentWidget(), workbench.get());
        workbench->floatSession(index); QCoreApplication::processEvents(); QVERIFY(workbench->isSessionFloating(index));
        QCOMPARE(window->findChild<QQuickWidget *>(), qml.data()); QCOMPARE(window->parentWidget(), workbench.get());
        workbench->dockSession(index); workbench->tileSessions(Qt::Horizontal); QCoreApplication::processEvents();
        QVERIFY(!workbench->isSessionFloating(index)); QCOMPARE(qml->status(), QQuickWidget::Ready); QVERIFY(!qml->grabFramebuffer().isNull());
        workbench.reset(); QVERIFY(window.isNull()); QVERIFY(qml.isNull());
    }
    void plotDestructionCanReleaseTheLastCatalogLease() {
        auto ownedCatalog = std::make_shared<PluginCatalog>();
        ownedCatalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        std::weak_ptr<PluginCatalog> released = ownedCatalog;
        auto workbench = std::make_unique<Workbench>(ownedCatalog); workbench->resize(1300, 900); workbench->show();
        const int index = workbench->addSession(configuration(), QStringLiteral("最后租约")); workbench->selectSession(index);
        auto *view = workbench->parameterView(index); accessible<QTableWidget>(*view, QStringLiteral("参数表"))->selectAll();
        QTest::mouseClick(button(*view, QStringLiteral("绘制选中参数")), Qt::LeftButton); QTRY_VERIFY(trendWindow());
        QPointer<QMainWindow> window = trendWindow();
        QTRY_COMPARE(window->findChild<QQuickWidget *>()->status(), QQuickWidget::Ready);
        ownedCatalog.reset(); catalog.reset();
        workbench.reset(); QVERIFY(window.isNull()); QTRY_VERIFY(released.expired());
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
};
QTEST_MAIN(ParameterTrendTests)
#include "parameter_trend_tests.moc"
