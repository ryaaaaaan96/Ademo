#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

using namespace debugger;
namespace {
QPushButton *button(QWidget &root, const QString &text) {
    for (auto *b : root.findChildren<QPushButton *>())
        if (b->text() == text && b->isVisibleTo(&root)) return b;
    return nullptr;
}
template<class T> T *accessible(QWidget &root, const QString &name) {
    for (auto *w : root.findChildren<T *>()) if (w->accessibleName() == name) return w;
    return nullptr;
}
struct Wire {
    EndpointEvents events;
    QList<QByteArray> requests;
    bool closed = false;
    QObject lifetime;
    class Port final : public Endpoint {
    public:
        explicit Port(Wire &wire) : wire_(wire) {}
        void open() override { wire_.events.opened(true, {}); }
        void close() override { wire_.closed = true; }
        bool send(quint64 token, const QByteArray &bytes) override {
            wire_.requests.append(bytes);
            wire_.events.sent(token, true, {});
            QTimer::singleShot(10, &wire_.lifetime, [this] {
                wire_.events.received(QByteArray::fromHex("110302022b38f8"));
            });
            return true;
        }
    private:
        Wire &wire_;
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &, EndpointEvents e) {
            events = std::move(e); return std::make_unique<Port>(*this);
        };
    }
};
}
class ParameterWindowTests : public QObject {
    Q_OBJECT
private slots:
    void readFromParameterRowUsesRealRtuSession() {
        auto catalog = std::make_shared<PluginCatalog>();
        catalog->discover(QCoreApplication::applicationDirPath() + "/plugins");
        Wire wire;
        Workbench window(catalog, nullptr, wire.factory());
        window.resize(1100, 850); window.show();
        auto *configure = button(window, QStringLiteral("配置…")); QVERIFY(configure);
        QTest::mouseClick(configure, Qt::LeftButton);
        auto *add = button(window, QStringLiteral("添加参数"));
        QVERIFY2(add, "新窗口应提供参数行入口");
        auto *unit = accessible<QSpinBox>(window, QStringLiteral("设备地址"));
        auto *address = accessible<QSpinBox>(window, QStringLiteral("参数 PDU 地址"));
        QVERIFY(unit && address);
        unit->setValue(1); address->setValue(0);
        QTest::mouseClick(add, Qt::LeftButton);
        unit->setValue(17); address->setValue(107);
        QTest::mouseClick(add, Qt::LeftButton);
        accessible<QDialog>(window, QStringLiteral("参数配置窗口"))->close();
        auto *table = accessible<QTableWidget>(window, QStringLiteral("参数表"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("PDU 107"));
        QVERIFY(button(window, QStringLiteral("连接配置…")));
        QTest::mouseClick(button(window, QStringLiteral("启动会话")), Qt::LeftButton);
        QTRY_COMPARE(window.session(0)->state(), SessionState::Running);
        auto *read = button(*table, QStringLiteral("读取"));
        QVERIFY(read);
        QTRY_VERIFY(read->isEnabled());
        QTest::mouseClick(read, Qt::LeftButton);
        QTRY_COMPARE(wire.requests.size(), 1);
        QCOMPARE(wire.requests.first().toHex(), QByteArray("1103006b0001f746"));
        QTRY_COMPARE(table->item(0, 3)->text(), QString("555"));
        QVERIFY(!table->item(0, 6)->text().isEmpty());
        auto *records = accessible<QPlainTextEdit>(window, QStringLiteral("本会话通信记录"));
        QVERIFY(records);
        QTRY_VERIFY(records->toPlainText().contains("local-sent"));
        QTRY_VERIFY(records->toPlainText().contains("rx"));
        QVERIFY(!button(window, QStringLiteral("兼容操作页")));
        auto *parameters = window.parameterView(0);
        QVERIFY(parameters);
        QCOMPARE(parameters->devices().size(), 2);
        QVERIFY(parameters->selectDevice("1"));
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("未读取"));
        QVERIFY(parameters->selectDevice("17"));
        QCOMPARE(table->item(0, 3)->text(), QString("555"));
        QCOMPARE(wire.requests.size(), 1);
        QTest::qWait(60); // 等待真实页面切换完成后，再操作当前显示的读取控件。
        read = button(*table, QStringLiteral("读取"));
        QVERIFY(read);
        const auto output = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!output.isEmpty()) QVERIFY(window.grab().save(output + "/parameter-first-read.png"));
        QTest::mouseClick(button(window, QStringLiteral("停止并释放端点")), Qt::LeftButton);
        QCOMPARE(window.session(0)->state(), SessionState::Stopped);
        QVERIFY(wire.closed);
        QTRY_VERIFY(!read->isEnabled());
    }
};
QTEST_MAIN(ParameterWindowTests)
#include "parameter_window_tests.moc"
