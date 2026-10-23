#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QProcess>
#include <QPointer>
#include <QMessageBox>
#include <QLabel>
#include <iostream>
#include <QTableWidget>
#include <QTest>

using namespace debugger;
namespace {
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *widget : root.findChildren<T *>())
        if (widget->accessibleName() == name && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *widget : root.findChildren<QPushButton *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
SessionConfig sceneConfig() { return {"ui.contract.scene", "serial", "preview", {{"port", "COM241"}, {"baud", "9600"},
    {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}, {}}; }
QString fixtureDirectory() { return QCoreApplication::applicationDirPath() + "/ui-contract-fixtures"; }
void answerNext(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, [answer] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(answer)->click();
    });
}
struct Wire {
    QList<QByteArray> requests;
    EndpointEvents events;
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
    DebugSession::EndpointFactory factory() { return [this](const QJsonObject &, EndpointEvents value) {
        events = std::move(value); return std::make_unique<Port>(*this);
    }; }
};
}
class PluginUiContractTests : public QObject {
    Q_OBJECT
private slots:
    void overlappingCatalogKeepsItsPluginAfterFirstCatalogCloses() {
        auto first = std::make_shared<PluginCatalog>(); first->discover(fixtureDirectory());
        {
            DebugSession session(first);
            std::unique_ptr<ParameterView> view(first->createParameterView("ui.contract.scene", nullptr, session.access()));
            QVERIFY(view);
        }
        auto second = std::make_shared<PluginCatalog>(); second->discover(fixtureDirectory());
        QVERIFY(first->protocol("ui.contract.scene")); QVERIFY(second->protocol("ui.contract.scene"));
        QPointer<QObject> loadedRoot(dynamic_cast<QObject *>(second->protocol("ui.contract.scene")));
        QVERIFY(loadedRoot);
        std::weak_ptr<PluginCatalog> firstLifetime = first;
        first.reset();
        QTRY_VERIFY(firstLifetime.expired()); // 首个视图租约延迟到 GUI 返回后释放。
        QVERIFY2(loadedRoot, "仍被第二目录使用的插件根对象不能随第一目录销毁");
        QVERIFY(second->protocol("ui.contract.scene"));
        DebugSession session(second);
        std::unique_ptr<ParameterView> view(second->createParameterView("ui.contract.scene", nullptr, session.access()));
        QVERIFY(view); QCOMPARE(view->devices().size(), 2);
    }
    void independentPluginProvidesItsOwnColumnsAndDevices() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/ui-contract-fixtures");
        QVERIFY2(catalog->protocol("ui.contract.scene"), "独立 DLL 必须经插件发现提供参数界面");
        DebugSession session(catalog);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("ui.contract.scene", nullptr, session.access()));
        QVERIFY(view);
        QCOMPARE(view->devices().size(), 2);
        QCOMPARE(view->selectedDevice(), QString("lamp-a"));
        view->show();
        QTableWidget *table = nullptr;
        for (auto *candidate : view->findChildren<QTableWidget *>())
            if (candidate->accessibleName() == QStringLiteral("场景参数表")) table = candidate;
        QVERIFY(table);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("场景"));
        QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("本次预览模式"));
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(view->selectDevice("lamp-b"));
        QCOMPARE(view->selectedDevice(), QString("lamp-b"));
        QVERIFY(!view->selectDevice("missing"));
        QCOMPARE(view->selectedDevice(), QString("lamp-b"));
    }
    void specializedControlSendsOnlyItsWindowOperation() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/ui-contract-fixtures");
        Wire wire, otherWire;
        Workbench window(catalog, nullptr, [&wire, &otherWire](const QJsonObject &endpoint, EndpointEvents events) {
            return endpoint["port"] == "COM242" ? otherWire.factory()(endpoint, std::move(events))
                : wire.factory()(endpoint, std::move(events));
        });
        QVERIFY(catalog->protocol("modbus.rtu"));
        QVERIFY(window.session(0)->start({"modbus.rtu", "serial", "master", {{"port", "COM242"}, {"baud", "9600"},
            {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}, {}}));
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        const auto otherTraceCount = window.session(0)->traces().size();
        const int target = window.addSession(sceneConfig(), QStringLiteral("场景测试会话"));
        window.resize(1100, 800); window.show(); window.selectSession(target);
        auto *view = window.parameterView(target); QVERIFY(view);
        auto *preview = button(*view, QStringLiteral("预览场景")); QVERIFY2(preview, "插件行内动作应挂在真实参数表上");
        auto *mode = accessible<QComboBox>(*view, QStringLiteral("本次预览模式")); QVERIFY(mode);
        mode->setCurrentText("blink");
        QVERIFY(wire.requests.isEmpty());
        QTest::mouseClick(button(*window.sessionView(target), QStringLiteral("启动会话")), Qt::LeftButton);
        QTRY_COMPARE(window.session(target)->state(), SessionState::Running);
        QTest::mouseClick(preview, Qt::LeftButton);
        QTRY_COMPARE(wire.requests.size(), 1);
        QCOMPARE(wire.requests.first(), QByteArray("PREVIEW lamp-a blink\n"));
        wire.events.received(QByteArray("ACK lamp-a\n"));
        QTRY_COMPARE(window.session(target)->operation(1).update.state, QString("success"));
        QCOMPARE(window.session(0)->state(), SessionState::Running);
        QCOMPARE(window.session(0)->traces().size(), otherTraceCount);
        QVERIFY(otherWire.requests.isEmpty());
        auto *records = accessible<QPlainTextEdit>(*window.sessionView(target), QStringLiteral("本会话通信记录"));
        QVERIFY(records); QTRY_VERIFY(records->toPlainText().contains("rx"));
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) QVERIFY(window.sessionWindow(target)->grab().save(evidence + "/scene-operation.png"));
        window.dockSession(target); window.floatSession(target);
        QCOMPARE(window.session(target)->state(), SessionState::Running);
        QVERIFY(window.closeSession(target));
        QCOMPARE(window.session(target)->state(), SessionState::Stopped);
        QCOMPARE(window.session(0)->state(), SessionState::Running);
    }
    void pluginFileEditorSavesThenExplicitlyAppliesOnlyItsTarget() {
        QTemporaryDir directory; const auto path = directory.filePath("scene.json");
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/ui-contract-fixtures");
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        std::unique_ptr<ParameterView> view(catalog->createParameterView("ui.contract.scene", nullptr, session.access()));
        view->resize(950, 650); view->show();
        auto *pages = accessible<QTabWidget>(*view, QStringLiteral("场景页面"));
        QVERIFY2(pages, "独立插件应挂载自身文件内容页"); pages->setCurrentIndex(1);
        auto *title = accessible<QLineEdit>(*view, QStringLiteral("文件场景名称")); QVERIFY(title);
        title->setText(QStringLiteral("报警灯"));
        accessible<QComboBox>(*view, QStringLiteral("文件播放模式"))->setCurrentText("blink");
        accessible<QLineEdit>(*view, QStringLiteral("文件场景分组"))->setText(QStringLiteral("生产报警"));
        QVERIFY(session.hasUnsavedOrBusy());
        accessible<QLineEdit>(*view, QStringLiteral("编辑文件路径"))->setText(path);
        QTest::mouseClick(button(*view, QStringLiteral("保存文件")), Qt::LeftButton);
        QTRY_VERIFY(QFile::exists(path)); QTRY_VERIFY(!session.hasUnsavedOrBusy());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto document = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(document["kind"].toString(), QString("ui-contract-scene"));
        QCOMPARE(document["title"].toString(), QStringLiteral("报警灯"));
        QCOMPARE(document["mode"].toString(), QString("blink"));
        QVERIFY(!session.config().protocol.contains("scenes")); QVERIFY(wire.requests.isEmpty());
        QTest::mouseClick(button(*view, QStringLiteral("应用到目标设备")), Qt::LeftButton);
        QTRY_COMPARE(session.config().protocol["scenes"].toObject()["lamp-a"].toObject()["title"].toString(), QStringLiteral("报警灯"));
        QVERIFY(!session.config().protocol["scenes"].toObject().contains("lamp-b"));
        QVERIFY(wire.requests.isEmpty());
        pages->setCurrentIndex(0);
        auto *group = accessible<QComboBox>(*view, QStringLiteral("场景分组")); QVERIFY(group);
        QTRY_COMPARE(group->currentText(), QStringLiteral("生产报警"));
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("场景参数表"));
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("报警灯"));
        group->setCurrentText(QStringLiteral("诊断"));
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("会话状态"));
        QVERIFY(view->selectDevice("lamp-b")); pages->setCurrentIndex(1);
        QCOMPARE(title->text(), QStringLiteral("就绪场景"));
        QVERIFY(view->selectDevice("lamp-a"));
        QCOMPARE(title->text(), QStringLiteral("报警灯"));
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) QVERIFY(view->grab().save(evidence + "/scene-file-editor.png"));
    }
    void restartDiscoversInstalledDllAndRejectsWrongContracts() {
        auto product = std::make_shared<PluginCatalog>();
        product->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(!product->protocol("ui.contract.scene"));
        QTemporaryDir directory;
        QVERIFY(QFile::copy(fixtureDirectory() + "/libserial_plugin.dll", directory.filePath("serial.dll")));
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--catalog-probe", directory.path(), "--rescan"});
        QVERIFY(process.waitForReadyRead(10000)); QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArray("ready"));
        QVERIFY(QFile::copy(fixtureDirectory() + "/libui_contract_plugin.dll", directory.filePath("scene.dll")));
        process.write("rescan\n");
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 3);
        process.start(QCoreApplication::applicationFilePath(), {"--catalog-probe", directory.path()});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
        auto after = std::make_shared<PluginCatalog>(); after->discover(fixtureDirectory());
        QVERIFY(after->protocol("ui.contract.scene")); QVERIFY(!after->protocol("incompatible"));
        const auto diagnostics = after->diagnostics().join('\n');
        QVERIFY(diagnostics.contains("libincompatible_plugin.dll"));
        QVERIFY(diagnostics.contains("libui_contract_stale_plugin.dll"));
        QVERIFY(directory.remove());
    }
    void cancellingDraftKeepsTargetAndViewLeaseSurvivesOwnerDestruction() {
        auto catalog = std::make_shared<PluginCatalog>(); catalog->discover(fixtureDirectory());
        std::weak_ptr<PluginCatalog> weak = catalog;
        auto session = std::make_unique<DebugSession>(catalog);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("ui.contract.scene", nullptr, session->access()));
        view->show();
        accessible<QTabWidget>(*view, QStringLiteral("场景页面"))->setCurrentIndex(1);
        auto *title = accessible<QLineEdit>(*view, QStringLiteral("文件场景名称")); title->setText(QStringLiteral("未保存场景"));
        answerNext(QMessageBox::Cancel);
        QVERIFY(!view->selectDevice("lamp-b"));
        QCOMPARE(view->selectedDevice(), QString("lamp-a")); QCOMPARE(title->text(), QStringLiteral("未保存场景"));
        QVERIFY(session->hasUnsavedOrBusy());
        catalog.reset(); session.reset(); QVERIFY(!weak.expired());
        QTimer::singleShot(0, [&view] { view.reset(); });
        ParameterView *calling = view.get();
        QVERIFY(!calling->selectDevice("lamp-b"));
        QVERIFY(!view); QTRY_VERIFY(weak.expired());
    }
    void specializedDiagnosticTracksSessionState() {
        auto catalog = std::make_shared<PluginCatalog>(); catalog->discover(fixtureDirectory());
        Wire wire; DebugSession session(catalog, nullptr, wire.factory());
        std::unique_ptr<ParameterView> view(catalog->createParameterView("ui.contract.scene", nullptr, session.access()));
        view->show();
        accessible<QComboBox>(*view, QStringLiteral("场景分组"))->setCurrentText(QStringLiteral("诊断"));
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("场景参数表"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("停止"));
        QVERIFY(session.start(sceneConfig()));
        QTRY_COMPARE(table->item(0, 1)->text(), QStringLiteral("运行中"));
        session.stop(); QTRY_COMPARE(table->item(0, 1)->text(), QStringLiteral("停止"));
    }
};
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    if (application.arguments().contains("--catalog-probe")) {
        auto catalog = std::make_shared<PluginCatalog>(); catalog->discover(application.arguments().value(2));
        if (application.arguments().contains("--rescan")) {
            std::cout << "ready" << std::endl;
            std::string command; std::getline(std::cin, command);
            catalog->discover(application.arguments().value(2));
        }
        if (!catalog->protocol("ui.contract.scene")) return 3;
        DebugSession session(catalog);
        std::unique_ptr<ParameterView> view(catalog->createParameterView("ui.contract.scene", nullptr, session.access()));
        return view && view->devices().size() == 2 ? 0 : 4;
    }
    PluginUiContractTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "plugin_ui_contract_tests.moc"
