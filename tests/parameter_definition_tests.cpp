#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <future>

using namespace debugger;
namespace {
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *widget : root.findChildren<T *>()) if (widget->accessibleName() == name) return widget;
    return nullptr;
}
QPushButton *button(QWidget &root, const QString &text) {
    if (text == QStringLiteral("添加参数") || text == QStringLiteral("加载并应用到设备"))
        for (auto *toggle : root.findChildren<QPushButton *>())
            if (toggle->text() == QStringLiteral("参数来源与地址调试") && !toggle->isChecked()) toggle->click();
    for (auto *widget : root.findChildren<QPushButton *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
bool save(const QString &path, const QJsonObject &document) {
    QFile file(path); const auto bytes = QJsonDocument(document).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject definition() {
    return {{"kind", "modbus-parameter-definition"}, {"version", 1}, {"parameters", QJsonArray{
        QJsonObject{{"area", "holding"}, {"address", 107}, {"name", QStringLiteral("温度")},
                    {"type", "uint16"}, {"unit", "°C"}, {"description", QStringLiteral("出口温度")}, {"group", QStringLiteral("测量")}},
        QJsonObject{{"area", "input"}, {"address", 8}, {"type", "uint16"}}
    }}};
}
struct Wire {
    EndpointEvents events;
    QList<QByteArray> requests;
    class Port final : public Endpoint {
        Wire &wire_;
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override {}
        bool send(quint64 token, const QByteArray &bytes) override {
            wire_.requests.append(bytes); wire_.events.sent(token, true, {}); return true;
        }
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents events) {
            this->events = std::move(events); return std::make_unique<Port>(*this);
        };
    }
};
SessionConfig configuration(DebugSession &session) {
    return {"modbus.rtu", "serial", "master", {{"port", "COM120"}, {"baud", "115200"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, session.config().protocol, {}};
}
}
class ParameterDefinitionTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void fileGroupsAndMetadataReadFromExplicitTarget() {
        QTemporaryDir dir; const auto path = dir.filePath("parameters.json"); QVERIFY(save(path, definition()));
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        QVERIFY(view); view->resize(1100, 500); view->show();
        auto *load = button(*view, QStringLiteral("加载并应用到设备"));
        QVERIFY2(load, "参数定义须有明确目标的加载应用入口");
        accessible<QSpinBox>(*view, QStringLiteral("设备地址"))->setValue(17);
        accessible<QLineEdit>(*view, QStringLiteral("参数文件"))->setText(path);
        QTest::mouseClick(load, Qt::LeftButton);
        QTRY_COMPARE(view->selectedDevice(), QString("17"));
        auto *group = accessible<QComboBox>(*view, QStringLiteral("参数分组")); QVERIFY(group);
        QVERIFY(group->findText(QStringLiteral("默认组")) >= 0);
        QVERIFY(group->findText(QStringLiteral("测量")) >= 0);
        group->setCurrentText(QStringLiteral("测量"));
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QVERIFY(table);
        QCOMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QStringLiteral("温度"));
        QCOMPARE(table->item(0, 1)->text(), QString("uint16"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("出口温度")); QCOMPARE(table->item(0, 4)->text(), QString("°C"));
        QVERIFY(wire.requests.isEmpty()); QVERIFY(session.start(configuration(session)));
        QTRY_COMPARE(session.state(), SessionState::Running);
        auto *read = button(*table, QStringLiteral("读取")); QVERIFY(read); QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton); QTRY_COMPARE(wire.requests.size(), 1);
        QCOMPARE(wire.requests.first().toHex(), QByteArray("1103006b0001f746"));
        wire.events.received(QByteArray::fromHex("110302022b38f8"));
        QTRY_COMPARE(table->item(0, 3)->text(), QString("555"));
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(view->grab().save(output + "/parameter-groups-read.png"));
        group->setCurrentText(QStringLiteral("默认组"));
        QCOMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QStringLiteral("PDU 8"));
        QCOMPARE(wire.requests.size(), 1); QCOMPARE(session.state(), SessionState::Running);
    }
    void addressRangeIsAtomicAndReadsSelectedArea() {
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1100, 500); view->show();
        auto *count = accessible<QSpinBox>(*view, QStringLiteral("参数数量"));
        QVERIFY2(count, "地址调试应能按起址和数量生成范围");
        auto *unit = accessible<QSpinBox>(*view, QStringLiteral("设备地址"));
        auto *address = accessible<QSpinBox>(*view, QStringLiteral("参数 PDU 地址"));
        auto *area = accessible<QComboBox>(*view, QStringLiteral("参数数据区"));
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        unit->setValue(18); area->setCurrentText(QStringLiteral("输入寄存器")); address->setValue(65534); count->setValue(3);
        QTest::mouseClick(button(*view, QStringLiteral("添加参数")), Qt::LeftButton);
        QCOMPARE(table->rowCount(), 0); QVERIFY(session.config().protocol.value("parameterRows").toArray().isEmpty());
        count->setValue(2); QTest::mouseClick(button(*view, QStringLiteral("添加参数")), Qt::LeftButton);
        QCOMPARE(table->rowCount(), 2); QCOMPARE(table->item(1, 0)->text(), QStringLiteral("PDU 65535"));
        QVERIFY(session.start(configuration(session))); QTRY_COMPARE(session.state(), SessionState::Running);
        auto *read = button(*table->cellWidget(1, 5), QStringLiteral("读取")); QVERIFY(read); QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton); QTRY_COMPARE(wire.requests.size(), 1);
        QCOMPARE(wire.requests.first().left(6).toHex(), QByteArray("1204ffff0001"));
        session.stop(); QTRY_VERIFY(button(*view, QStringLiteral("添加参数"))->isEnabled());
        unit->setValue(19); address->setValue(0); count->setValue(257);
        QTest::mouseClick(button(*view, QStringLiteral("添加参数")), Qt::LeftButton);
        QCOMPARE(view->selectedDevice(), QString("19")); QCOMPARE(table->rowCount(), 257);
        QCOMPARE(wire.requests.size(), 1);
    }
    void workspaceKeepsAppliedParametersAndRelocatesSourceReference() {
        QTemporaryDir dir; QDir root(dir.path()); QVERIFY(root.mkdir("definitions")); QVERIFY(root.mkdir("workspaces"));
        const auto source = root.filePath("definitions/device.json"); QVERIFY(save(source, definition()));
        const auto target = root.filePath("workspaces/workspace.json");
        DebugSession session(catalog);
        QVERIFY(session.configure({{"parameterRows", QJsonArray{QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107},
            {"name", QStringLiteral("已应用温度")}}}}, {"parameterFiles", QJsonObject{{"17", source}}}}));
        Workbench workbench(catalog); workbench.addSession(configuration(session));
        auto operation = workbench.saveWorkspace(target); QVERIFY(operation);
        QTRY_COMPARE(workbench.workspaceOperation(operation).state, QString("success"));
        QFile file(target); QVERIFY(file.open(QIODevice::ReadOnly)); const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        const auto protocol = saved["sessions"].toArray()[1].toObject()["configuration"].toObject()["protocol"].toObject();
        QCOMPARE(protocol["parameterFiles"].toObject()["17"].toString(), QString("../definitions/device.json"));
        QVERIFY(QFile::remove(source)); // 主站参数定义不是从站启动数据，不用缺失来源覆盖已应用声明。
        Wire wire; Workbench restored(catalog, nullptr, wire.factory());
        operation = restored.openWorkspace(target); QTRY_COMPARE(restored.workspaceOperation(operation).state, QString("success"));
        QCOMPARE(restored.sessionConfiguration(1).protocol["parameterFiles"].toObject()["17"].toString(), source);
        QCOMPARE(restored.session(1)->state(), SessionState::Stopped); QVERIFY(wire.requests.isEmpty());
        auto *table = accessible<QTableWidget>(*restored.parameterView(1), QStringLiteral("参数表")); QVERIFY(table);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("已应用温度"));
    }
    void invalidFilesPreserveTargetAndOtherDevices() {
        QTemporaryDir dir; const auto path = dir.filePath("parameters.json"); QVERIFY(save(path, definition()));
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1100, 550); view->show();
        auto *unit = accessible<QSpinBox>(*view, QStringLiteral("设备地址"));
        auto *load = button(*view, QStringLiteral("加载并应用到设备"));
        auto *pathInput = accessible<QLineEdit>(*view, QStringLiteral("参数文件")); pathInput->setText(path);
        unit->setValue(17); QTest::mouseClick(load, Qt::LeftButton); QTRY_VERIFY(load->isEnabled());
        unit->setValue(18); QTest::mouseClick(load, Qt::LeftButton); QTRY_VERIFY(load->isEnabled());
        QCOMPARE(view->devices().size(), 2); QCOMPARE(view->selectedDevice(), QString("18"));
        const auto applied = session.config().protocol;
        QList<QJsonObject> invalid;
        auto wrongKind = definition(); wrongKind["kind"] = "slave-data"; invalid.append(wrongKind);
        auto wrongVersion = definition(); wrongVersion["version"] = 1.5; invalid.append(wrongVersion);
        for (const auto &replacement : QList<QJsonObject>{
            {{"area", "holding"}, {"address", 65536}}, {{"area", "holding"}, {"address", 1.5}},
            {{"area", "coils"}, {"address", 0}, {"type", "uint16"}},
            {{"area", "unknown"}, {"address", 0}}, {{"area", "holding"}, {"address", 0}, {"group", 42}}}) {
            auto document = definition(); document["parameters"] = QJsonArray{replacement}; invalid.append(document);
        }
        auto duplicate = definition(); auto parameters = duplicate["parameters"].toArray(); parameters.append(parameters.first());
        duplicate["parameters"] = parameters; invalid.append(duplicate);
        for (const auto &document : invalid) {
            QVERIFY(save(path, document)); QTest::mouseClick(load, Qt::LeftButton); QTRY_VERIFY(load->isEnabled());
            QCOMPARE(session.config().protocol, applied); QCOMPARE(view->selectedDevice(), QString("18"));
        }
        QVERIFY(QFile::remove(path)); QTest::mouseClick(load, Qt::LeftButton); QTRY_VERIFY(load->isEnabled());
        QCOMPARE(session.config().protocol, applied); QVERIFY(wire.requests.isEmpty());
        QVERIFY(view->selectDevice("17")); auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        accessible<QComboBox>(*view, QStringLiteral("参数分组"))->setCurrentText(QStringLiteral("测量"));
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("温度"));
    }
    void closingDuringFileLoadRetainsPluginUntilCompletion() {
        QTemporaryDir dir; const auto path = dir.filePath("parameters.json"); QVERIFY(save(path, definition()));
        auto localCatalog = std::make_shared<PluginCatalog>(); localCatalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        std::weak_ptr<PluginCatalog> lease = localCatalog;
        auto session = std::make_unique<DebugSession>(localCatalog);
        std::unique_ptr<ParameterView> view(localCatalog->createParameterView("modbus.rtu", nullptr, session->access()));
        view->show(); accessible<QLineEdit>(*view, QStringLiteral("参数文件"))->setText(path);
        QTest::mouseClick(button(*view, QStringLiteral("加载并应用到设备")), Qt::LeftButton);
        view.reset(); session.reset(); localCatalog.reset();
        QVERIFY2(!lease.expired(), "后台文件代码和待回调仍须保持插件 DLL 租约");
        QTRY_VERIFY(lease.expired());
    }
    void fileApplyConfirmationCanDestroyParameterView() {
        QTemporaryDir dir; const auto path = dir.filePath("parameters.json"); QVERIFY(save(path, definition()));
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        const QJsonObject original{{"parameterRows", QJsonArray{QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107}}}}};
        QVERIFY(session.configure(original));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 700); view->show();
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QVERIFY(table);
        table->editItem(table->item(0, 3));
        auto *editor = table->findChild<QLineEdit *>(); QVERIFY(editor);
        QTest::keyClicks(editor, "123"); QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(session.hasUnsavedOrBusy());
        accessible<QLineEdit>(*view, QStringLiteral("参数文件"))->setText(path);
        bool confirmed = false;
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, &timer, [&] {
            if (qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                confirmed = true; timer.stop(); view.reset();
            }
        }); timer.start();
        QTest::mouseClick(button(*view, QStringLiteral("加载并应用到设备")), Qt::LeftButton);
        QTRY_VERIFY(!view); QVERIFY(confirmed);
        QCOMPARE(session.config().protocol, original); QVERIFY(wire.requests.isEmpty());
        QVERIFY(!session.hasUnsavedOrBusy());
    }
    void sourcePickerBlocksQueuedWorkspaceReplacementAndReleasesOnCancel() {
        QTemporaryDir dir; const auto path = dir.filePath("empty-workspace.json");
        QVERIFY(save(path, {{"version", 1}, {"sessions", QJsonArray{}}}));
        QList<std::function<void()>> jobs;
        Workbench workbench(catalog, nullptr, {}, {}, [&](auto job) { jobs.append(std::move(job)); });
        workbench.resize(1300, 850); workbench.show();
        QPointer<ParameterView> view = workbench.parameterView(0); QVERIFY(view);
        auto *session = workbench.session(0); const auto original = session->config().protocol;
        auto *toggle = button(*view, QStringLiteral("参数来源与地址调试")); QVERIFY(toggle); toggle->setChecked(true);
        const auto operation = workbench.openWorkspace(path); QVERIFY(operation); QCOMPARE(jobs.size(), 1);
        bool pickerShown = false;
        QTimer timer; timer.setInterval(5);
        connect(&timer, &QTimer::timeout, &timer, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                pickerShown = true;
                if (!jobs.isEmpty()) { auto job = jobs.takeFirst(); job(); }
                else if (workbench.workspaceOperation(operation).state != "working") { dialog->reject(); timer.stop(); }
            }
        }); timer.start();
        auto *browse = button(*view, QStringLiteral("选择参数文件…")); QVERIFY(browse);
        QTest::mouseClick(browse, Qt::LeftButton);
        QVERIFY(pickerShown); QCOMPARE(workbench.workspaceOperation(operation).state, QString("cancelled"));
        QVERIFY(view); QCOMPARE(workbench.sessionCount(), 1); QCOMPARE(workbench.session(0), session);
        QCOMPARE(session->config().protocol, original); QVERIFY(!session->hasUnsavedOrBusy());
        // 取消选择后不遗留 busy，下一次用户明确打开工作区可以完成。
        const auto retry = workbench.openWorkspace(path); QVERIFY(retry); QCOMPARE(jobs.size(), 1);
        auto job = jobs.takeFirst(); job();
        QTRY_COMPARE(workbench.workspaceOperation(retry).state, QString("success"));
        QVERIFY(!view); QCOMPARE(workbench.sessionCount(), 0);
    }
    void pendingConfirmationRejectsReentrantConfiguration() {
        DebugSession session(catalog);
        const QJsonObject original{{"parameterRows", QJsonArray{
            QJsonObject{{"unit", 17}, {"area", "holding"}, {"address", 107}},
            QJsonObject{{"unit", 18}, {"area", "holding"}, {"address", 107}}}}};
        QVERIFY(session.configure(original));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 700); view->show();
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数表")); QVERIFY(table);
        table->editItem(table->item(0, 3));
        auto *editor = table->findChild<QLineEdit *>(); QVERIFY(editor);
        QTest::keyClicks(editor, "123"); QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(session.hasUnsavedOrBusy());
        bool guardShown = false, nestedGuard = false, configured = true;
        QTimer observer; observer.setInterval(5);
        connect(&observer, &QTimer::timeout, &observer, [&] {
            auto *outer = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!outer) return;
            guardShown = true; observer.stop();
            QTimer nestedCloser; nestedCloser.setInterval(5);
            connect(&nestedCloser, &QTimer::timeout, &nestedCloser, [&] {
                if (auto *inner = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); inner && inner != outer) {
                    nestedGuard = true; inner->button(QMessageBox::Cancel)->click();
                }
            }); nestedCloser.start();
            configured = session.configure({}); nestedCloser.stop();
            outer->button(QMessageBox::Cancel)->click();
        }); observer.start();
        QVERIFY(!view->selectDevice("18"));
        QVERIFY(guardShown); QVERIFY(!nestedGuard); QVERIFY(!configured);
        QCOMPARE(view->selectedDevice(), QString("17")); QCOMPARE(session.config().protocol, original);
        QCOMPARE(table->item(0, 3)->text(), QString("123"));
        // 正常取消退出后仍可明确放弃草稿，不能遗留 busy。
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Discard)->click();
        });
        QVERIFY(view->selectDevice("18")); QVERIFY(!session.hasUnsavedOrBusy());
    }
    void fileJobsAreBoundedAndReturnValuesOnTheSessionThread() {
        DebugSession session(catalog); auto access = session.access(); QVERIFY(access.background);
        auto release = std::make_shared<std::promise<void>>(); auto gate = release->get_future().share();
        int completed = 0; bool correctThread = true; bool correctResult = true;
        const auto job = [gate] { gate.wait(); return QJsonObject{{"value", 42}}; };
        const auto done = [&](QJsonObject value) {
            correctThread = correctThread && QThread::currentThread() == session.thread();
            correctResult = correctResult && value.value("value").toInt() == 42; ++completed;
        };
        bool accepted = true; for (int i = 0; i < 4; ++i) accepted = access.background(job, done) && accepted;
        const bool overflow = access.background(job, done);
        release->set_value(); // 先释放实际后台任务，任何断言失败也不留下阻塞线程。
        QVERIFY(accepted); QVERIFY(!overflow); QTRY_COMPARE(completed, 4); QVERIFY(correctThread && correctResult);
        QVERIFY(access.background([]() -> QJsonObject { throw std::runtime_error("read failed"); },
            [&](QJsonObject result) { correctResult = result.value("error").toString() == "read failed"; ++completed; }));
        QTRY_COMPARE(completed, 5); QVERIFY(correctResult);
    }
};
QTEST_MAIN(ParameterDefinitionTests)
#include "parameter_definition_tests.moc"
