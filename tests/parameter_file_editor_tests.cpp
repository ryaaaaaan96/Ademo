#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSpinBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTest>

using namespace debugger;
namespace {
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *widget : root.findChildren<T *>()) if (widget->accessibleName() == name && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *widget : root.findChildren<QPushButton *>())
        if (widget->text() == text && widget->isVisibleTo(&root)) return widget;
    return nullptr;
}
QJsonObject configuration() {
    return {{"parameterRows", QJsonArray{QJsonObject{{"unit", 17}, {"area", "holding"},
        {"address", 107}, {"name", QStringLiteral("原名称")}, {"type", "uint16"}}}}};
}
QJsonObject read(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
void answerNext(QMessageBox::StandardButton choice) {
    auto *timer = new QTimer(qApp); timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, choice] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            box->button(choice)->click(); timer->stop(); timer->deleteLater(); return;
        }
    });
    timer->start();
}
QLineEdit *editValue(QTableWidget &table, const QString &text) {
    const auto point = table.visualItemRect(table.item(0, 3)).center();
    QTest::mouseClick(table.viewport(), Qt::LeftButton, {}, point);
    QTest::mouseDClick(table.viewport(), Qt::LeftButton, {}, point);
    QCoreApplication::processEvents();
    for (auto *edit : table.findChildren<QLineEdit *>()) if (edit->isVisibleTo(&table)) {
        edit->selectAll(); QTest::keyClicks(edit, text); return edit;
    }
    return nullptr;
}
struct Wire {
    int opens = 0, writes = 0;
    struct Port : Endpoint {
        Wire &wire; EndpointEvents events;
        Port(Wire &wire, EndpointEvents events) : wire(wire), events(std::move(events)) {}
        void open() override { ++wire.opens; events.opened(true, {}); }
        void close() override {}
        bool send(quint64 id, const QByteArray &) override { ++wire.writes; events.sent(id,true,{}); return true; }
    };
    DebugSession::EndpointFactory factory() { return [this](const QJsonObject &, EndpointEvents events) { return std::make_unique<Port>(*this,std::move(events)); }; }
};
}
class ParameterFileEditorTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog;
private slots:
    void initTestCase() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        catalog = std::make_shared<PluginCatalog>(); catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        QVERIFY(catalog->protocol("modbus.rtu"));
    }
    void newOpenAndSaveAsKeepRunningDeclarationSeparate() {
        QTemporaryDir dir; const auto path = dir.filePath("created.json"), copy = dir.filePath("copy.json");
        DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 700); view->show();
        accessible<QTabWidget>(*view, QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *create = button(*view, QStringLiteral("新建文件")); QVERIFY2(create, "文件页须可新建定义，不要求手写JSON");
        QTest::mouseClick(create, Qt::LeftButton);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数文件内容")); QCOMPARE(table->rowCount(), 0);
        QTest::mouseClick(button(*view, QStringLiteral("添加文件参数")), Qt::LeftButton);
        QCOMPARE(table->rowCount(), 1); table->item(0, 0)->setText(QStringLiteral("压力"));
        accessible<QSpinBox>(*table, QStringLiteral("文件 PDU 地址"))->setValue(42);
        auto *filePath = accessible<QLineEdit>(*view, QStringLiteral("编辑文件路径")); filePath->setText(path);
        QTest::mouseClick(button(*view, QStringLiteral("保存文件")), Qt::LeftButton);
        QTRY_VERIFY(!session.hasUnsavedOrBusy()); QCOMPARE(read(path)["parameters"].toArray()[0].toObject()["address"].toInt(), 42);
        QTimer::singleShot(0, [copy] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QFileDialog *>(widget)) {
                dialog->selectFile(copy); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        });
        QTest::mouseClick(button(*view, QStringLiteral("另存为…")), Qt::LeftButton);
        QTRY_VERIFY(QFile::exists(copy)); QTRY_VERIFY(!session.hasUnsavedOrBusy());
        QCOMPARE(read(path), read(copy)); QCOMPARE(filePath->text(), copy);
        QTest::mouseClick(button(*view, QStringLiteral("新建文件")), Qt::LeftButton);
        QCOMPARE(table->rowCount(), 0);
        answerNext(QMessageBox::Discard);
        filePath->setText(path); QTest::mouseClick(button(*view, QStringLiteral("打开文件")), Qt::LeftButton);
        QTRY_COMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QStringLiteral("压力"));
        QCOMPARE(session.config().protocol, configuration()); QVERIFY(session.traces().isEmpty());
    }
    void editingAndSavingOnlyApplyOnExplicitAction() {
        QTemporaryDir dir; const auto path = dir.filePath("parameters.json");
        DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 700); view->show();
        auto *pages = accessible<QTabWidget>(*view, QStringLiteral("参数页面"));
        QVERIFY2(pages, "每个调试窗口须有独立文件编辑页面");
        pages->setCurrentIndex(1);
        auto *table = accessible<QTableWidget>(*view, QStringLiteral("参数文件内容")); QVERIFY(table);
        QCOMPARE(table->rowCount(), 1); QCOMPARE(table->item(0, 0)->text(), QStringLiteral("原名称"));
        table->item(0, 0)->setText(QStringLiteral("出口温度"));
        QVERIFY(session.hasUnsavedOrBusy());
        accessible<QLineEdit>(*view, QStringLiteral("编辑文件路径"))->setText(path);
        QTest::mouseClick(button(*view, QStringLiteral("保存文件")), Qt::LeftButton);
        QTRY_VERIFY(QFile::exists(path)); QTRY_VERIFY(!session.hasUnsavedOrBusy());
        QCOMPARE(read(path)["kind"].toString(), QString("modbus-parameter-definition"));
        QCOMPARE(read(path)["parameters"].toArray()[0].toObject()["name"].toString(), QStringLiteral("出口温度"));
        QCOMPARE(session.config().protocol["parameterRows"].toArray()[0].toObject()["name"].toString(), QStringLiteral("原名称"));
        QVERIFY(session.traces().isEmpty());
        // 输入另一条待打开/保存的路径，不改变当前文档的已保存来源。
        accessible<QLineEdit>(*view, QStringLiteral("编辑文件路径"))->setText(dir.filePath("not-opened.json"));
        QTest::mouseClick(button(*view, QStringLiteral("应用到目标设备")), Qt::LeftButton);
        QTRY_COMPARE(session.config().protocol["parameterRows"].toArray()[0].toObject()["name"].toString(), QStringLiteral("出口温度"));
        QCOMPARE(session.config().protocol["parameterFiles"].toObject()["17"].toString(), path);
        QVERIFY(session.traces().isEmpty());
        pages->setCurrentIndex(0);
        auto *parameters = accessible<QTableWidget>(*view, QStringLiteral("参数表"));
        QCOMPARE(parameters->item(0, 0)->text(), QStringLiteral("出口温度"));
    }
    void loadedDeviceRebindsFilePageExplicitly() {
        QTemporaryDir dir; const auto path = dir.filePath("device18.json");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"kind\":\"modbus-parameter-definition\",\"version\":1,\"parameters\":[{\"area\":\"input\",\"address\":9,\"name\":\"device18\"}]}"); file.close();
        DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200, 700); view->show();
        accessible<QSpinBox>(*view, QStringLiteral("设备地址"))->setValue(18);
        accessible<QLineEdit>(*view, QStringLiteral("参数文件"))->setText(path);
        QTest::mouseClick(button(*view, QStringLiteral("加载并应用到设备")), Qt::LeftButton);
        QTRY_COMPARE(view->selectedDevice(), QString("18"));
        accessible<QTabWidget>(*view, QStringLiteral("参数页面"))->setCurrentIndex(1);
        QVERIFY(accessible<QLabel>(*view, QStringLiteral("文件目标设备"))->text().contains("18"));
        QCOMPARE(accessible<QTableWidget>(*view, QStringLiteral("参数文件内容"))->item(0, 0)->text(), QString("device18"));
    }
    void invalidOpenFailedSaveAndCancelledSwitchKeepDraft() {
        QTemporaryDir dir; const auto invalid = dir.filePath("invalid.json");
        QFile file(invalid); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken"); file.close();
        auto config = configuration(); auto rows = config["parameterRows"].toArray();
        rows.append(QJsonObject{{"unit",18},{"area","holding"},{"address",3},{"name","other"}}); config["parameterRows"] = rows;
        DebugSession session(catalog); QVERIFY(session.configure(config));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu", nullptr, session.access()));
        view->resize(1200,700); view->show(); accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *table = accessible<QTableWidget>(*view,QStringLiteral("参数文件内容")); table->item(0,0)->setText("draft17");
        answerNext(QMessageBox::Cancel); QVERIFY(!view->selectDevice("18")); QCOMPARE(view->selectedDevice(),QString("17"));
        QCOMPARE(table->item(0,0)->text(),QString("draft17")); QVERIFY(session.hasUnsavedOrBusy());
        auto *path = accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径")); path->setText(invalid);
        QTest::mouseClick(button(*view,QStringLiteral("打开文件")),Qt::LeftButton);
        QTRY_VERIFY(accessible<QLabel>(*view,QStringLiteral("文件状态"))->text().contains(QStringLiteral("打开失败")));
        QCOMPARE(table->item(0,0)->text(),QString("draft17")); QVERIFY(session.hasUnsavedOrBusy());
        path->setText(dir.filePath("absent/failed.json"));
        QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        QTRY_VERIFY(accessible<QLabel>(*view,QStringLiteral("文件状态"))->text().contains(QStringLiteral("保存失败")));
        QCOMPARE(table->item(0,0)->text(),QString("draft17")); QVERIFY(session.hasUnsavedOrBusy());
        answerNext(QMessageBox::Cancel); QVERIFY(!session.resolveUnsaved());
        QCOMPARE(session.config().protocol,config); QVERIFY(session.hasUnsavedOrBusy());
        answerNext(QMessageBox::Discard); QVERIFY(view->selectDevice("18"));
        QCOMPARE(table->item(0,0)->text(),QString("other")); QVERIFY(!session.hasUnsavedOrBusy());
        QVERIFY(view->selectDevice("17")); QCOMPARE(table->item(0,0)->text(),QStringLiteral("原名称"));
    }
    void saveSnapshotDoesNotClearNewEditingAndOverwriteCanCancel() {
        QTemporaryDir dir; const auto path = dir.filePath("snapshot.json");
        DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,session.access()));
        view->resize(1200,700); view->show(); accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *table=accessible<QTableWidget>(*view,QStringLiteral("参数文件内容")); table->item(0,0)->setText("snapshot");
        accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(path);
        QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        table->item(0,0)->setText("new draft");
        QTRY_VERIFY(QFile::exists(path)); QTRY_VERIFY(button(*view,QStringLiteral("保存文件"))->isEnabled());
        QCOMPARE(read(path)["parameters"].toArray()[0].toObject()["name"].toString(),QString("snapshot"));
        QCOMPARE(table->item(0,0)->text(),QString("new draft")); QVERIFY(session.hasUnsavedOrBusy());
        QVERIFY(!button(*view,QStringLiteral("应用到目标设备"))->isEnabled());
        answerNext(QMessageBox::No); QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        QCOMPARE(read(path)["parameters"].toArray()[0].toObject()["name"].toString(),QString("snapshot"));
        QVERIFY(session.hasUnsavedOrBusy());
        answerNext(QMessageBox::Yes); QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        QTRY_VERIFY(!session.hasUnsavedOrBusy());
        QCOMPARE(read(path)["parameters"].toArray()[0].toObject()["name"].toString(),QString("new draft"));
        QCOMPARE(session.config().protocol,configuration());
    }
    void savingFileDoesNotDiscardPendingAndApplyCanCancel() {
        QTemporaryDir dir; DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,session.access()));
        view->resize(1200,700); view->show();
        auto *pages=accessible<QTabWidget>(*view,QStringLiteral("参数页面"));
        auto *parameters=accessible<QTableWidget>(*view,QStringLiteral("参数表"));
        auto *value=editValue(*parameters,"123"); QVERIFY(value); QPointer<QLineEdit> editing(value); QTest::keyClick(value,Qt::Key_Return);
        QTRY_VERIFY(!editing || !editing->isVisible());
        pages->setCurrentIndex(1);
        auto *fileTable=accessible<QTableWidget>(*view,QStringLiteral("参数文件内容")); fileTable->item(0,0)->setText("saved definition");
        accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(dir.filePath("saved.json"));
        QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        QTRY_VERIFY(button(*view,QStringLiteral("应用到目标设备"))->isEnabled());
        QVERIFY(session.hasUnsavedOrBusy());
        answerNext(QMessageBox::Cancel); QTest::mouseClick(button(*view,QStringLiteral("应用到目标设备")),Qt::LeftButton);
        QCOMPARE(session.config().protocol,configuration());
        pages->setCurrentIndex(0); QCOMPARE(parameters->item(0,3)->text(),QString("123"));
        pages->setCurrentIndex(1);
        answerNext(QMessageBox::Discard); QTest::mouseClick(button(*view,QStringLiteral("应用到目标设备")),Qt::LeftButton);
        QVERIFY(!session.hasUnsavedOrBusy());
        QCOMPARE(session.config().protocol["parameterRows"].toArray()[0].toObject()["name"].toString(),QString("saved definition"));
    }
    void changingAreaWithinRegisterTypeRequiresSave() {
        DebugSession session(catalog); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,session.access()));
        view->resize(1200,700); view->show(); accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *area=accessible<QComboBox>(*view,QStringLiteral("文件参数数据区"));
        area->setCurrentIndex(area->findData("input"));
        QVERIFY(session.hasUnsavedOrBusy());
        QVERIFY(!button(*view,QStringLiteral("应用到目标设备"))->isEnabled());
        QCOMPARE(session.config().protocol,configuration());
    }
    void publicViewAccessKeepsPluginCodeAliveUntilCallbacksUnwind() {
        auto isolated=std::make_shared<PluginCatalog>();
        isolated->discover(QCoreApplication::applicationDirPath()+"/plugins");
        std::weak_ptr<PluginCatalog> observed=isolated;
        auto session=std::make_unique<DebugSession>(isolated);
        auto access=session->access();
        session.reset(); isolated.reset();
        QVERIFY2(!observed.expired(),"公开视图访问对象须在同步应用回调展开期间保留DLL");
        access={};
        QVERIFY2(!observed.expired(),"DLL最后释放须延迟到插件返回栈退出当前事件");
        QTRY_VERIFY(observed.expired());
    }
    void closingOneSessionCanCancelFileDraftWithoutChangingOtherSession() {
        Workbench workbench(catalog); workbench.resize(1300,850); workbench.show();
        SessionConfig config{"modbus.rtu","serial","master",{},configuration(),{}};
        const int first=workbench.addSession(config,QStringLiteral("文件编辑 A"));
        const int second=workbench.addSession(config,QStringLiteral("文件编辑 B"));
        workbench.selectSession(first);
        auto *view=workbench.parameterView(first); QVERIFY(view);
        accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *table=accessible<QTableWidget>(*view,QStringLiteral("参数文件内容")); table->item(0,0)->setText("first draft");
        answerNext(QMessageBox::Cancel); QVERIFY(!workbench.sessionWindow(first)->close());
        QVERIFY(workbench.isSessionOpen(first)); QCOMPARE(table->item(0,0)->text(),QString("first draft"));
        QCOMPARE(workbench.session(second)->config().protocol,configuration());
        QVERIFY(!workbench.session(second)->hasUnsavedOrBusy());
        answerNext(QMessageBox::Discard); QVERIFY(workbench.sessionWindow(first)->close());
        QVERIFY(!workbench.isSessionOpen(first)); QVERIFY(workbench.isSessionOpen(second));
        workbench.selectSession(first); QVERIFY(workbench.isSessionOpen(first));
        QCOMPARE(workbench.session(first)->config().protocol,configuration());
        QVERIFY(workbench.session(first)->traces().isEmpty()); QVERIFY(workbench.session(second)->traces().isEmpty());
    }
    void fileSaveWhileRunningDoesNotSendOrApply() {
        QTemporaryDir dir; Wire wire; DebugSession session(catalog,nullptr,wire.factory()); QVERIFY(session.configure(configuration()));
        std::unique_ptr<ParameterView> view(catalog->createParameterView("modbus.rtu",nullptr,session.access()));
        view->resize(1200,700); view->show();
        SessionConfig config{"modbus.rtu","serial","master",{{"port","COM123"},{"baud","115200"},{"dataBits","8"},{"parity","even"},{"stopBits","1"}},configuration(),{}};
        QVERIFY(session.start(config)); QTRY_COMPARE(session.state(),SessionState::Running);
        accessible<QTabWidget>(*view,QStringLiteral("参数页面"))->setCurrentIndex(1);
        auto *table=accessible<QTableWidget>(*view,QStringLiteral("参数文件内容")); table->item(0,0)->setText("offline file edit");
        accessible<QLineEdit>(*view,QStringLiteral("编辑文件路径"))->setText(dir.filePath("offline.json"));
        QTest::mouseClick(button(*view,QStringLiteral("保存文件")),Qt::LeftButton);
        QTRY_VERIFY(!session.hasUnsavedOrBusy()); QCOMPARE(wire.writes,0); QCOMPARE(wire.opens,1);
        QTest::mouseClick(button(*view,QStringLiteral("应用到目标设备")),Qt::LeftButton);
        QCOMPARE(session.config().protocol,configuration()); QCOMPARE(session.state(),SessionState::Running); QCOMPARE(wire.writes,0);
        session.stop(); QTest::mouseClick(button(*view,QStringLiteral("应用到目标设备")),Qt::LeftButton);
        QCOMPARE(session.config().protocol["parameterRows"].toArray()[0].toObject()["name"].toString(),QString("offline file edit"));
        QCOMPARE(wire.writes,0); QCOMPARE(wire.opens,1);
        const auto output=qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if(!output.isEmpty()) QVERIFY(view->grab().save(output+"/file-editor-save-apply.png"));
    }
};
QTEST_MAIN(ParameterFileEditorTests)
#include "parameter_file_editor_tests.moc"
