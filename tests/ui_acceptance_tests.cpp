#include "debugger/host.h"
#include <QApplication>
#include "debugger/application_style.h"
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTest>
#include <QDir>
#include <QDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QFile>
#include <QMainWindow>
#include <QQuickWidget>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
using namespace debugger;
namespace {
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *b : root.findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}
template <class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *w : root.findChildren<T *>())
        if (w->accessibleName() == name)
            return w;
    return nullptr;
}
} // namespace
class UiAcceptanceTests : public QObject {
    Q_OBJECT
  private slots:
    void configurationWindowKeepsMainPageCompactWithoutImplicitActions() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Workbench window(catalog); window.resize(1200, 820); window.show();
        auto *view = window.parameterView(0); QVERIFY(view);
        auto *address = accessible<QSpinBox>(*view, QStringLiteral("参数 PDU 地址"));
        auto *interval = accessible<QSpinBox>(*view, QStringLiteral("轮询周期"));
        auto *configuration = accessible<QDialog>(*view, QStringLiteral("参数配置窗口"));
        QVERIFY(address && interval && configuration);
        QVERIFY(!configuration->isVisible()); QVERIFY(!address->isVisible());
        const auto original = window.session(0)->config().protocol;
        const auto traces = window.session(0)->traces().size();
        for (const auto &text : {QStringLiteral("配置…"), QStringLiteral("读取选中项"),
                QStringLiteral("绘制选中参数"), QStringLiteral("开始轮询"), QStringLiteral("停止会话轮询")})
            QVERIFY(button(*view, text)->isVisible());
        button(*view, QStringLiteral("配置…"))->click();
        QTRY_VERIFY(configuration->isVisible()); QVERIFY(address->isVisible());
        address->setValue(120); interval->setValue(250);
        button(*view, QStringLiteral("配置…"))->click();
        QCOMPARE(accessible<QDialog>(*view, QStringLiteral("参数配置窗口")), configuration);
        QTest::keyClick(configuration, Qt::Key_Escape);
        QTRY_VERIFY(!configuration->isVisible());
        QCOMPARE(window.session(0)->config().protocol, original);
        QCOMPARE(window.session(0)->traces().size(), traces);
        QCOMPARE(window.session(0)->state(), SessionState::Stopped);
        button(*view, QStringLiteral("配置…"))->click();
        QCOMPARE(address->value(), 120); QCOMPARE(interval->value(), 250);
        button(*view, QStringLiteral("添加参数"))->click();
        QTRY_COMPARE(accessible<QTableWidget>(*view, QStringLiteral("参数表"))->rowCount(), 1);
        QCOMPARE(window.session(0)->traces().size(), traces);
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) {
            QDir().mkpath(output); QTest::qWait(100);
            QVERIFY(configuration->grab().save(output + "/parameter-configuration.png"));
        }
        configuration->close();
        QTRY_VERIFY(!configuration->isVisible());
        auto *parameterTable = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        auto *sidebar = accessible<QScrollArea>(*view, QStringLiteral("会话操作栏"));
        QVERIFY(sidebar && sidebar->isVisible());
        auto *start = button(*window.sessionView(0), QStringLiteral("启动会话"));
        QVERIFY(start && sidebar->isAncestorOf(start));
        const int tableWidth = parameterTable->width();
        accessible<QPushButton>(*view, QStringLiteral("收起操作栏"))->click();
        QTRY_VERIFY(!sidebar->isVisible()); QVERIFY(!start->isVisible());
        QTRY_VERIFY(parameterTable->width() > tableWidth);
        QVERIFY(parameterTable->isVisible());
        if (!output.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(view->grab().save(output + "/parameters-focused.png"));
        }
        accessible<QPushButton>(*view, QStringLiteral("展开操作栏"))->click();
        QTRY_VERIFY(sidebar->isVisible()); QVERIFY(start->isVisible());
        window.dockSession(0);
        if (!output.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window.grab().save(output + "/compact-parameter-workbench.png"));
        }
        QVERIFY(window.closeSession(0));
        QVERIFY(!window.isSessionOpen(0));
        QVERIFY(!configuration->isVisible());
    }
    void mainCloseCancelsSafelyThenClosesAllOwnedWindows() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        int closes = 0;
        class Port final : public Endpoint {
        public:
            EndpointEvents events;
            int *closes;
            void open() override { events.opened(true, {}); }
            void close() override { ++*closes; }
            bool send(quint64, const QByteArray &) override { return true; }
        };
        Workbench window(catalog, nullptr, [&](const QJsonObject &, EndpointEvents events) {
            auto port = std::make_unique<Port>(); port->events = std::move(events); port->closes = &closes;
            return port;
        });
        QWidget unrelated; unrelated.show();
        auto config = window.sessionConfiguration(0);
        config.protocol["parameterRows"] = QJsonArray{QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 0}}};
        const int index = window.addSession(config, QStringLiteral("关闭流程验收"));
        window.show(); window.floatSession(index);
        auto *view = window.parameterView(index);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); table->selectAll();
        for (const auto &text : {QStringLiteral("配置…"), QStringLiteral("读取重试配置…"),
                QStringLiteral("按功能码操作…"), QStringLiteral("绘制选中参数")})
            button(*view, text)->click();
        button(*window.sessionView(index), QStringLiteral("连接配置…"))->click();
        QList<QPointer<QWidget>> ownedWindows;
        for (auto *child : window.findChildren<QWidget *>())
            if (child->isWindow() && child->isVisible()) ownedWindows.append(child);
        QVERIFY(ownedWindows.size() >= 6);
        QVERIFY(window.session(index)->start(config));
        QTRY_COMPARE(window.session(index)->state(), SessionState::Running);
        auto *pages = accessible<QTabWidget>(*view, QStringLiteral("参数页面")); pages->setCurrentIndex(1);
        auto *fileTable = accessible<QTableWidget>(*view, QStringLiteral("参数文件内容"));
        QVERIFY(fileTable && fileTable->rowCount()); fileTable->item(0, 0)->setText(QStringLiteral("未保存名称"));
        QVERIFY(window.session(index)->hasUnsavedOrBusy());
        QMessageBox::StandardButton answer = QMessageBox::Cancel;
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, &timer, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                if (auto *choice = box->button(answer)) choice->click();
        }); timer.start();
        QVERIFY(!window.close()); QVERIFY(window.isVisible());
        QCOMPARE(window.session(index)->state(), SessionState::Running); QCOMPARE(closes, 0);
        for (const auto &child : ownedWindows) QVERIFY(child && child->isVisible());
        QCOMPARE(fileTable->item(0, 0)->text(), QStringLiteral("未保存名称"));
        answer = QMessageBox::Discard;
        QVERIFY(window.close()); timer.stop();
        QTRY_VERIFY(!window.isVisible()); QCOMPARE(closes, 1);
        QCOMPARE(window.session(index)->state(), SessionState::Stopped);
        for (const auto &child : ownedWindows) QTRY_VERIFY(!child || !child->isVisible());
        QVERIFY(unrelated.isVisible());
    }
    void formalWorkbenchCombinesReadFileTrendsAndWorkspace() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QList<QByteArray> writes;
        class Port final : public Endpoint {
          public:
            EndpointEvents events;
            QList<QByteArray> *writes;
            QObject *clock;
            void open() override { events.opened(true, {}); }
            void close() override {}
            bool send(quint64 token, const QByteArray &bytes) override {
                writes->append(bytes);
                events.sent(token, true, {});
                QByteArray response;
                if (bytes[1] == char(3)) {
                    const int count = quint8(bytes[4]) * 256 + quint8(bytes[5]);
                    response.append(bytes[0]);
                    response.append(char(3));
                    response.append(char(count * 2));
                    for (int i = 0; i < count; ++i) {
                        const int value = 554 + writes->size() + i;
                        response.append(char(value >> 8));
                        response.append(char(value));
                    }
                    quint16 crc = 0xffff;
                    for (const auto byte : response) {
                        crc ^= quint8(byte);
                        for (int bit = 0; bit < 8; ++bit)
                            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
                    }
                    response.append(char(crc));
                    response.append(char(crc >> 8));
                } else
                    response = bytes;
                auto received = events.received;
                QTimer::singleShot(10, clock, [received, response] { received(response); });
                return true;
            }
        };
        QObject clock;
        Workbench window(catalog, nullptr, [&](const QJsonObject &, EndpointEvents events) {
            auto port = std::make_unique<Port>();
            port->events = std::move(events);
            port->writes = &writes;
            port->clock = &clock;
            return port;
        });
        auto config = window.sessionConfiguration(0);
        config.endpoint["port"] = "COM120";
        config.protocol["parameterRows"] = QJsonArray{
            QJsonObject{{"unit", 17},
                        {"area", "holding"},
                        {"address", 107},
                        {"name", QStringLiteral("出口温度")},
                        {"displayUnit", QStringLiteral("℃")},
                        {"group", QStringLiteral("运行状态")}},
            QJsonObject{{"unit", 17},
                        {"area", "holding"},
                        {"address", 108},
                        {"name", QStringLiteral("目标温度")},
                        {"displayUnit", QStringLiteral("℃")},
                        {"group", QStringLiteral("运行状态")}},
            QJsonObject{{"unit", 18}, {"area", "holding"}, {"address", 0}, {"name", QStringLiteral("压力")}}};
        const auto parameterName = [](const SessionConfig &config) {
            for (const auto &row : config.protocol["parameterRows"].toArray())
                if (row.toObject()["unit"].toInt() == 17 && row.toObject()["address"].toInt() == 107)
                    return row.toObject()["name"].toString();
            return QString();
        };
        const int index = window.addSession(config, QStringLiteral("温控总线"));
        QVERIFY(window.closeSession(0));
        window.resize(1360, 900);
        window.show();
        window.raise();
        window.activateWindow();
        auto *view = window.parameterView(index);
        QVERIFY(view);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QVERIFY(window.session(index)->start(config));
        QTRY_COMPARE(window.session(index)->state(), SessionState::Running);
        table->selectAll();
        button(*view, QStringLiteral("绘制选中参数"))->click();
        QMainWindow *plot = nullptr;
        for (auto *w : QApplication::topLevelWidgets())
            if (w->windowTitle().contains(QStringLiteral("参数趋势")))
                plot = qobject_cast<QMainWindow *>(w);
        QVERIFY(plot);
        auto *qml = plot->findChild<QQuickWidget *>();
        QVERIFY(qml);
        QTRY_COMPARE(qml->status(), QQuickWidget::Ready);
        QVERIFY(writes.isEmpty());
        QTRY_VERIFY(button(*view, QStringLiteral("读取当前组"))->isEnabled());
        button(*view, QStringLiteral("读取当前组"))->click();
        QTRY_COMPARE(table->item(0, 3)->text(), QString("555"));
        QTRY_COMPARE(table->item(1, 3)->text(), QString("556"));
        QTRY_VERIFY(accessible<QLabel>(*plot, QStringLiteral("曲线数据概要"))->text().contains("555"));
        accessible<QComboBox>(*view, QStringLiteral("轮询范围"))->setCurrentText(QStringLiteral("当前组"));
        accessible<QSpinBox>(*view, QStringLiteral("轮询周期"))->setValue(100);
        QTRY_VERIFY(button(*view, QStringLiteral("开始轮询"))->isEnabled());
        button(*view, QStringLiteral("开始轮询"))->click();
        QTRY_VERIFY(writes.size() >= 6);
        button(*view, QStringLiteral("停止会话轮询"))->click();
        QTest::qWait(100);
        for (int i = 0; i < 4; ++i)
            button(*plot, QStringLiteral("放大时间轴"))->click();
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        auto capture = [&](QWidget *widget, const QString &name) {
            widget->show();
            widget->raise();
            widget->activateWindow();
            QTest::qWait(200);
            if (output.isEmpty())
                return true;
            QDir().mkpath(output);
            return widget->grab().save(output + "/" + name + ".png");
        };
        QVERIFY(capture(plot, "formal-qml-trends"));
        plot->hide();
        QVERIFY(capture(&window, "formal-workbench"));
        window.floatSession(index);
        auto *floating = window.sessionWindow(index);
        floating->resize(780, 820);
        QVERIFY(capture(floating, "formal-floating-master"));
        floating->resize(650, 550);
        auto *records = button(*window.sessionView(index), QStringLiteral("通信记录"));
        QVERIFY(records);
        records->click();
        QVERIFY(capture(floating, "formal-narrow-master"));
        window.session(index)->stop();
        const auto sentBeforeFiles = writes.size();
        auto *pages = accessible<QTabWidget>(*view, QStringLiteral("参数页面"));
        pages->setCurrentIndex(1);
        auto *fileTable = accessible<QTableWidget>(*view, QStringLiteral("参数文件内容"));
        QVERIFY(fileTable);
        fileTable->item(0, 0)->setText(QStringLiteral("出口温度（已校准）"));
        QTemporaryDir dir;
        const auto definition = dir.filePath("parameters.json"), workspace = dir.filePath("workspace.json");
        accessible<QLineEdit>(*view, QStringLiteral("编辑文件路径"))->setText(definition);
        button(*view, QStringLiteral("保存文件"))->click();
        QTRY_VERIFY(QFile::exists(definition));
        QTRY_VERIFY(!window.session(index)->hasUnsavedOrBusy());
        QCOMPARE(parameterName(window.sessionConfiguration(index)), QStringLiteral("出口温度"));
        button(*view, QStringLiteral("应用到目标设备"))->click();
        QTRY_COMPARE(parameterName(window.sessionConfiguration(index)), QStringLiteral("出口温度（已校准）"));
        QVERIFY(capture(floating, "formal-narrow-file"));
        auto *scroll = window.sessionView(index)->findChild<QScrollArea *>("sessionContent");
        QVERIFY(scroll);
        scroll->ensureWidgetVisible(button(*view, QStringLiteral("应用到目标设备")));
        QTest::qWait(50);
        QVERIFY(capture(floating, "formal-narrow-file-actions"));
        records->click();
        auto second = config;
        second.endpoint["port"] = "COM121";
        window.addSession(second, QStringLiteral("独立压力总线"));
        window.tileSessions(Qt::Horizontal);
        QVERIFY(capture(&window, "formal-two-sessions"));
        const auto saved = window.saveWorkspace(workspace);
        QTRY_COMPARE(window.workspaceOperation(saved).state, QString("success"));
        const auto restored = window.openWorkspace(workspace);
        QTRY_COMPARE(window.workspaceOperation(restored).state, QString("success"));
        QCOMPARE(window.sessionCount(), 3);
        QCOMPARE(window.session(1)->state(), SessionState::Stopped);
        QCOMPARE(parameterName(window.sessionConfiguration(1)), QStringLiteral("出口温度（已校准）"));
        QCOMPARE(writes.size(), sentBeforeFiles);
        QVERIFY(window.close());
    }
    void advancedDraftSurvivesCancelAndProtectsSessionClose() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Workbench window(catalog);
        window.show();
        auto *view = window.parameterView(0);
        button(*view, QStringLiteral("按功能码操作…"))->click();
        auto *value = accessible<QLineEdit>(*view, QStringLiteral("设备写入值"));
        QVERIFY(value);
        value->selectAll();
        QTest::keyClicks(value, "42");
        QVERIFY2(window.session(0)->hasUnsavedOrBusy(), "功能码写入草稿需要纳入会话修改保护");
        auto answer = [](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, [choice] {
                for (auto *w : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(w))
                        box->done(choice);
            });
        };
        auto *dialog = qobject_cast<QDialog *>(value->window());
        QVERIFY(dialog);
        answer(QMessageBox::Cancel);
        QTest::keyClick(dialog, Qt::Key_Escape);
        QVERIFY(dialog->isVisible());
        answer(QMessageBox::Cancel);
        QVERIFY(!dialog->close());
        QCOMPARE(value->text(), QString("42"));
        answer(QMessageBox::Cancel);
        QVERIFY(!window.closeSession(0));
        QVERIFY(window.isSessionOpen(0));
        answer(QMessageBox::Discard);
        QVERIFY(dialog->close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!window.session(0)->hasUnsavedOrBusy());
        QVERIFY(window.closeSession(0));
    }
    void advancedBulkWriteRequiresExplicitCommand_data() {
        QTest::addColumn<QString>("changedField");
        for (const auto *field : {"address", "unit", "function", "value", "unchanged"})
            QTest::newRow(field) << QString(field);
    }
    void advancedBulkWriteRequiresExplicitCommand() {
        QFETCH(QString, changedField);
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QList<QByteArray> writes;
        EndpointEvents events;
        class Port final : public Endpoint {
          public:
            EndpointEvents *events;
            QList<QByteArray> *writes;
            void open() override { events->opened(true, {}); }
            void close() override {}
            bool send(quint64 token, const QByteArray &bytes) override {
                writes->append(bytes);
                events->sent(token, true, {});
                return true;
            }
        };
        Workbench window(catalog, nullptr, [&](const QJsonObject &, EndpointEvents callbacks) {
            events = std::move(callbacks);
            auto p = std::make_unique<Port>();
            p->events = &events;
            p->writes = &writes;
            return p;
        });
        window.show();
        auto *view = window.parameterView(0);
        auto *advanced = button(*view, QStringLiteral("按功能码操作…"));
        QVERIFY(advanced);
        advanced->click();
        QVERIFY(writes.isEmpty());
        auto *unit = accessible<QSpinBox>(*view, QStringLiteral("功能码操作设备地址"));
        auto *address = accessible<QSpinBox>(*view, QStringLiteral("功能码操作PDU地址"));
        auto *action = accessible<QComboBox>(*view, QStringLiteral("设备写入功能"));
        auto *values = accessible<QLineEdit>(*view, QStringLiteral("设备写入值"));
        QVERIFY(unit && address && action && values);
        unit->setValue(17);
        address->setValue(1);
        action->setCurrentIndex(3);
        QVERIFY(window.session(0)->start(window.sessionConfiguration(0)));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        values->selectAll();
        QTest::keyClicks(values, "[10,258]");
        QVERIFY(writes.isEmpty());
        auto *send = button(*view, QStringLiteral("执行设备写入（不会自动重试）"));
        QVERIFY(send);
        QTRY_VERIFY(send->isEnabled());
        send->click();
        QTRY_COMPARE(writes.size(), 1);
        QCOMPARE(writes.first().toHex(), QByteArray("11100001000204000a0102c6f0"));
        // 已发送目标之后改动目标，新输入不能被旧目标的成功确认清除。
        if (changedField == "address")
            address->setValue(2);
        else if (changedField == "unit")
            unit->setValue(18);
        else if (changedField == "function")
            action->setCurrentIndex(2);
        else if (changedField == "value") {
            values->selectAll();
            QTest::keyClicks(values, "[20,21]");
        }
        QByteArray response = writes.first().left(6);
        quint16 crc = 0xffff;
        for (const auto byte : response) {
            crc ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
        }
        response.append(char(crc));
        response.append(char(crc >> 8));
        events.received(response);
        QTRY_COMPARE(window.session(0)->queryTrace().operations.last().update.state, QString("success"));
        QTest::qWait(80);
        QCOMPARE(window.session(0)->hasUnsavedOrBusy(), changedField != "unchanged");
    }
    void newParameterPageKeepsReadRetryConfiguration() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Workbench window(catalog);
        window.show();
        auto *view = window.parameterView(0);
        QVERIFY(view);
        auto *settings = button(*view, QStringLiteral("读取重试配置…"));
        QVERIFY2(settings, "新参数页必须保留读取超时和重试入口");
        QVERIFY(!button(window, QStringLiteral("兼容操作页")));
        const QJsonArray polls{
            QJsonObject{{"unit", 17}, {"action", "read-holding"}, {"start", 107}, {"count", 1}, {"intervalMs", 500}}};
        QVERIFY(window.session(0)->configure({{"polls", polls}}));
        settings->click();
        auto *retries = accessible<QSpinBox>(*view, QStringLiteral("额外读取重试"));
        auto *timeout = accessible<QSpinBox>(*view, QStringLiteral("应答超时"));
        auto *gap = accessible<QSpinBox>(*view, QStringLiteral("重试间隔"));
        QVERIFY(retries && timeout && gap);
        retries->setValue(0);
        timeout->setValue(321);
        gap->setValue(45);
        button(*view, QStringLiteral("应用读取配置"))->click();
        const auto config = window.sessionConfiguration(0).protocol;
        QCOMPARE(config["polls"].toArray(), polls);
        QCOMPARE(config["extraReadRetries"].toInt(), 0);
        QCOMPARE(config["responseTimeoutMs"].toInt(), 321);
        QCOMPARE(config["retryIntervalMs"].toInt(), 45);
        QCOMPARE(window.session(0)->state(), SessionState::Stopped);
        QVERIFY(window.session(0)->traces().isEmpty());
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    applyApplicationStyle(app);
    UiAcceptanceTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ui_acceptance_tests.moc"
