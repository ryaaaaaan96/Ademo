#include "debugger/host.h"
#include <QApplication>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QMessageBox>
#include <QLabel>
#include <QFile>
#include <QJsonDocument>
#include <QSpinBox>
#include <QTemporaryDir>
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
struct Wire {
    EndpointEvents events;
    QList<QPair<quint64, QByteArray>> requests;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 token, const QByteArray &bytes) override {
            wire_.requests.append({token, bytes}); return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
};
SessionConfig configuration() {
    return {"modbus.rtu", "serial", "master", {{"port", "COM120"}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}},
        {{"parameterRows", QJsonArray{QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107}}}}}, {}};
}
QLineEdit *editValue(QTableWidget &table, const QString &text) {
    const auto point = table.visualItemRect(table.item(0, 3)).center();
    QTest::mouseClick(table.viewport(), Qt::LeftButton, {}, point);
    QTest::mouseDClick(table.viewport(), Qt::LeftButton, {}, point);
    QCoreApplication::processEvents();
    for (auto *edit : table.findChildren<QLineEdit *>()) if (edit->isVisible()) {
        edit->selectAll(); QTest::keyClicks(edit, text); return edit;
    }
    return nullptr;
}
}
class ParameterWriteTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void editingAndPendingSurviveReadsAndOnlySetWrites() {
        Wire wire; qint64 time = 0;
        DebugSession session(catalog, nullptr, wire.factory(), [&] { return time; });
        auto config = configuration(); QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show();
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QVERIFY(table);
        QVERIFY(session.start(config)); QTRY_COMPARE(session.state(), SessionState::Running);
        auto *editor = editValue(*table, "123");
        QVERIFY2(editor, "双击值应能编辑待提交内容");
        QCOMPARE(wire.requests.size(), 0);
        const auto read = session.submit("read-holding", {{"unit", 17}, {"start", 107}, {"count", 1}});
        time += 5000; session.poll(); QCoreApplication::processEvents(); QCOMPARE(wire.requests.size(), 1);
        wire.events.sent(wire.requests.last().first, true, {});
        wire.events.received(QByteArray::fromHex("110302022b38f8"));
        QCoreApplication::processEvents(); time += 5000; session.poll();
        QTRY_COMPARE(session.operation(read).update.state, QString("success"));
        QTest::qWait(80); QCOMPARE(editor->text(), QString("123"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(table->item(0, 3)->text(), QString("123"));
        QTRY_VERIFY(table->item(0, 3)->toolTip().contains(QStringLiteral("待提交")));
        QVERIFY(table->item(0, 3)->toolTip().contains("555"));
        auto *set = button(*table, QStringLiteral("设置")); QVERIFY(set); QTRY_VERIFY(set->isEnabled());
        QTest::mouseClick(set, Qt::LeftButton); time += 5000; session.poll(); QCoreApplication::processEvents();
        QCOMPARE(wire.requests.size(), 2);
        QCOMPARE(wire.requests.last().second.left(6).toHex(), QByteArray("1106006b007b"));
        wire.events.sent(wire.requests.last().first, true, {}); QTest::qWait(80);
        QVERIFY(table->item(0, 3)->toolTip().contains(QStringLiteral("待提交")));
        wire.events.received(wire.requests.last().second); QCoreApplication::processEvents(); time += 5000; session.poll();
        QTRY_VERIFY(!table->item(0, 3)->toolTip().contains(QStringLiteral("待提交")));
        QCOMPARE(table->item(0, 3)->text(), QString("555")); // 写确认不冒充读回值。
        QCOMPARE(wire.requests.size(), 2);
    }
    void pendingProtectsDeviceSelectionConfigurationAndSessionClose() {
        DebugSession session(catalog);
        auto config = configuration(); auto rows = config.protocol["parameterRows"].toArray();
        rows.append(QJsonObject{{"unit", 18}, {"area", "holding"}, {"address", 107}});
        config.protocol["parameterRows"] = rows; QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show();
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        auto *editor = editValue(*table, "123"); QVERIFY(editor);
        QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(60);
        QVERIFY2(session.hasUnsavedOrBusy(), "会话关闭与工作区替换必须看见待提交值");
        auto cancel = [] { QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click();
        }); };
        cancel(); QVERIFY(!view->selectDevice("18")); QCOMPARE(view->selectedDevice(), QString("17"));
        cancel(); QVERIFY(!session.configure({})); QCOMPARE(session.config().protocol, config.protocol);
        cancel(); QVERIFY(!session.resolveUnsaved()); QVERIFY(session.hasUnsavedOrBusy());
        QCOMPARE(table->item(0, 3)->text(), QString("123"));
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Discard)->click();
        });
        QVERIFY(session.resolveUnsaved()); QVERIFY(!session.hasUnsavedOrBusy());
        QVERIFY(view->selectDevice("18")); QCOMPARE(table->item(0, 3)->text(), QStringLiteral("未读取"));
    }
    void invalidInputHasVisibleFeedbackAndDoesNotSend() {
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        auto config = configuration(); QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show(); QVERIFY(session.start(config));
        QTRY_COMPARE(session.state(), SessionState::Running);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        auto *editor = editValue(*table, "65536"); QVERIFY(editor); QTest::keyClick(editor, Qt::Key_Return);
        auto *set = button(*table, QStringLiteral("设置")); QTRY_VERIFY(set->isEnabled()); QTest::mouseClick(set, Qt::LeftButton);
        const auto hasFeedback = [&] {
            for (auto *label : view->findChildren<QLabel *>())
                if (label->isVisible() && label->text().contains(QStringLiteral("无效输入"))) return true;
            return false;
        };
        QVERIFY2(hasFeedback(), "无效待提交值应有可见错误提示");
        QCOMPARE(wire.requests.size(), 0); QVERIFY(session.hasUnsavedOrBusy());
        QCOMPARE(table->item(0, 3)->text(), QString("65536"));
    }
    void replacementDoesNotResurrectDiscardedPendingValues() {
        DebugSession session(catalog); auto config = configuration(); QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show(); auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        auto *editor = editValue(*table, "123"); QVERIFY(editor); QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(60);
        QTemporaryDir dir; const auto path = dir.filePath("other-device.json");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"kind", "modbus-parameter-definition"}, {"version", 1},
            {"parameters", QJsonArray{QJsonObject{{"area", "input"}, {"address", 0}}}}}).toJson()); file.close();
        accessible<QSpinBox>(*view, QStringLiteral("设备地址"))->setValue(18);
        accessible<QLineEdit>(*view, QStringLiteral("参数文件"))->setText(path);
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Discard)->click();
        }); answer.start(10);
        QTest::mouseClick(button(*view, QStringLiteral("加载并应用到设备")), Qt::LeftButton);
        QTRY_COMPARE(view->selectedDevice(), QString("18")); answer.stop();
        QVERIFY2(!session.hasUnsavedOrBusy(), "应用其他设备定义不能复活已明确放弃的待提交值");
        QVERIFY(view->selectDevice("17")); QCOMPARE(table->item(0, 3)->text(), QStringLiteral("未读取"));
    }
    void writeFailuresRemainPendingWithoutRetry_data() {
        QTest::addColumn<QString>("failure");
        QTest::newRow("exception") << QString("exception");
        QTest::newRow("timeout") << QString("timeout");
        QTest::newRow("disconnect") << QString("transport-error");
        QTest::newRow("stop") << QString("cancelled");
    }
    void writeFailuresRemainPendingWithoutRetry() {
        QFETCH(QString, failure);
        Wire wire; qint64 time = 0; DebugSession session(catalog, nullptr, wire.factory(), [&] { return time; });
        auto config = configuration(); config.protocol["extraReadRetries"] = 3; QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show(); QVERIFY(session.start(config)); QTRY_COMPARE(session.state(), SessionState::Running);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        auto *editor = editValue(*table, "123"); QVERIFY(editor); QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(20);
        auto *set = button(*table, QStringLiteral("设置")); QTRY_VERIFY(set->isEnabled()); QTest::mouseClick(set, Qt::LeftButton);
        time += 5000; session.poll(); QCoreApplication::processEvents(); QCOMPARE(wire.requests.size(), 1);
        wire.events.sent(wire.requests.last().first, true, {}); QCoreApplication::processEvents();
        if (failure == "exception") wire.events.received(QByteArray::fromHex("118602c264"));
        else if (failure == "transport-error") wire.events.failed("link lost");
        else if (failure == "cancelled") session.stop();
        QCoreApplication::processEvents(); time += 2000000; session.poll();
        QTRY_VERIFY(table->item(0, 3)->toolTip().contains(failure));
        QVERIFY(table->item(0, 3)->toolTip().contains(QStringLiteral("待提交")));
        if (failure != "exception") QVERIFY(table->item(0, 3)->toolTip().contains(QStringLiteral("不确定")));
        QCOMPARE(table->item(0, 3)->text(), QString("123"));
        time += 5000000; session.poll(); QTest::qWait(80); QCOMPARE(wire.requests.size(), 1);
        QVERIFY(session.hasUnsavedOrBusy());
    }
    void independentViewGuardsRecheckNewChangesAndExpireWithOwner() {
        DebugSession session(catalog); auto access = session.access();
        QObject first; auto second = std::make_unique<QObject>(); bool a = true, b = true;
        access.registerViewState(&first, [&] { return LocalState{false, a, false, {}}; }, [&] { a = false; b = true; return true; });
        access.registerViewState(second.get(), [&] { return LocalState{false, b, false, {}}; }, [&] {
            b = false; a = true; return true;
        });
        QVERIFY(!session.resolveUnsaved()); QVERIFY(session.hasUnsavedOrBusy());
        access.registerViewState(&first, [&] { return LocalState{false, a, false, {}}; }, [&] { a = false; return true; });
        access.registerViewState(second.get(), [&] { return LocalState{false, b, false, {}}; }, [&] { b = false; return true; });
        QVERIFY(session.resolveUnsaved()); QVERIFY(!session.hasUnsavedOrBusy());
        access.registerViewState(second.get(), [] { return LocalState{false, true, false, {}}; }, [] { return false; });
        QVERIFY(session.hasUnsavedOrBusy()); second.reset(); QVERIFY(!session.hasUnsavedOrBusy());
    }
    void coilWriteUsesBooleanAndInputRegistersStayReadOnly() {
        Wire wire; qint64 time = 0; DebugSession session(catalog, nullptr, wire.factory(), [&] { return time; });
        auto config = configuration(); config.protocol["parameterRows"] = QJsonArray{
            QJsonObject{{"unit", 17}, {"area", "coils"}, {"address", 172}},
            QJsonObject{{"unit", 17}, {"area", "input"}, {"address", 107}}};
        QVERIFY(session.configure(config.protocol));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 600); view->show(); QVERIFY(session.start(config)); QTRY_COMPARE(session.state(), SessionState::Running);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QCOMPARE(table->rowCount(), 2);
        QVERIFY(!(table->item(1, 3)->flags() & Qt::ItemIsEditable));
        QVERIFY(!button(*table->cellWidget(1, 5), QStringLiteral("设置")));
        auto *editor = editValue(*table, "true"); QVERIFY(editor); QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(20);
        auto *set = button(*table, QStringLiteral("设置")); QTRY_VERIFY(set->isEnabled()); QTest::mouseClick(set, Qt::LeftButton);
        time += 5000; session.poll(); QCoreApplication::processEvents(); QCOMPARE(wire.requests.size(), 1);
        QCOMPARE(wire.requests.first().second.toHex(), QByteArray("110500acff004e8b"));
        // 在旧写入确认前产生新草稿，旧确认不能清掉它。
        editor = editValue(*table, "false"); QVERIFY(editor); QTest::keyClick(editor, Qt::Key_Return); QTest::qWait(20);
        wire.events.sent(wire.requests.last().first, true, {}); wire.events.received(wire.requests.last().second);
        QCoreApplication::processEvents(); time += 5000; session.poll(); QTest::qWait(80);
        QCOMPARE(table->item(0, 3)->text(), QString("false")); QVERIFY(session.hasUnsavedOrBusy());
        QVERIFY(table->item(0, 3)->toolTip().contains("success")); QCOMPARE(wire.requests.size(), 1);
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(view->grab().save(output + "/pending-value.png"));
    }
};
QTEST_MAIN(ParameterWriteTests)
#include "parameter_write_tests.moc"
