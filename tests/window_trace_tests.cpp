#include "debugger/host.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace debugger;
namespace {
struct Wire {
    QMap<QString, EndpointEvents> ports;
    class Port final : public Endpoint {
    public:
        explicit Port(EndpointEvents events) : events_(std::move(events)) {}
        void open() override { events_.opened(true, {}); }
        void close() override {}
        bool send(quint64, const QByteArray &) override { return true; }
    private:
        EndpointEvents events_;
    };
    DebugSession::EndpointFactory factory() {
        return [this](const QJsonObject &endpoint, EndpointEvents events) {
            ports[endpoint.value("port").toString()] = events;
            return std::make_unique<Port>(std::move(events));
        };
    }
    void receive(const QString &port, const QByteArray &bytes) {
        ports[port].received(bytes); QCoreApplication::processEvents();
    }
};
SessionConfig config(const QString &port) {
    return {"trace.fixture", "serial", "master", {{"port", port}, {"baud", "9600"},
        {"dataBits", "8"}, {"parity", "even"}, {"stopBits", "1"}}, {}};
}
template<class T> T *accessible(QWidget *root, const QString &name) {
    for (auto *widget : root->findChildren<T *>()) if (widget->accessibleName() == name) return widget;
    return nullptr;
}
QPushButton *button(QWidget *root, const QString &text) {
    for (auto *widget : root->findChildren<QPushButton *>()) if (widget->text() == text && widget->isVisible()) return widget;
    return nullptr;
}
bool page(QWidget *root, const QString &title) {
    for (auto *tabs : root->findChildren<QTabWidget *>()) for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabText(i) == title) { tabs->setCurrentIndex(i); QCoreApplication::processEvents(); return true; }
    return false;
}
template<class T> T *labeled(QWidget *root, const QString &label) {
    for (auto *widget : root->findChildren<QLabel *>()) if (widget->text() == label)
        if (auto *editor = qobject_cast<T *>(widget->buddy())) return editor;
    return nullptr;
}
QByteArray readFile(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}
class WindowTraceTests : public QObject {
    Q_OBJECT
    std::shared_ptr<PluginCatalog> catalog_;
private slots:
    void initTestCase() {
        catalog_ = std::make_shared<PluginCatalog>();
        catalog_->discover(QCoreApplication::applicationDirPath() + "/trace-fixtures");
        QVERIFY(catalog_->protocol("trace.fixture")); QVERIFY(catalog_->transport("serial"));
    }
    void eachWindowQueriesAndExportsItsEvidenceAfterMoving() {
        QTemporaryDir directory; Wire wire; Workbench workbench(catalog_, nullptr, wire.factory());
        workbench.show();
        const int second = workbench.addSession(config("COM902"), QStringLiteral("第二条连接"));
        auto *first = workbench.session(0), *other = workbench.session(second);
        QVERIFY(first->start(config("COM901"))); QVERIFY(other->start(config("COM902")));
        QTRY_COMPARE(first->state(), SessionState::Running); QTRY_COMPARE(other->state(), SessionState::Running);
        first->submit("two-attempts", {}); wire.receive("COM901", "abcdef"); wire.receive("COM901", "gh");
        wire.receive("COM901", "unknown-A"); wire.receive("COM902", "unknown-B");
        workbench.tileSessions(Qt::Horizontal);
        auto *view = workbench.sessionView(0), *otherView = workbench.sessionView(second);
        auto *summary = accessible<QPlainTextEdit>(view, QStringLiteral("本会话通信记录"));
        QVERIFY(summary); QTRY_VERIFY(summary->toPlainText().contains("75 6e 6b 6e 6f 77 6e 2d 41"));
        QVERIFY(!summary->toPlainText().contains("75 6e 6b 6e 6f 77 6e 2d 42"));
        QVERIFY(page(view, QStringLiteral("回溯与导出")));
        auto *query = button(view, QStringLiteral("查询证据")); QVERIFY(query); QTest::mouseClick(query, Qt::LeftButton);
        auto *output = accessible<QPlainTextEdit>(view, QStringLiteral("回溯查询结果")); QVERIFY(output);
        QVERIFY(output->toPlainText().contains(QStringLiteral("错误消息")));
        QVERIFY(output->toPlainText().contains("75 6e 6b 6e 6f 77 6e 2d 41"));
        QVERIFY(!output->toPlainText().contains("75 6e 6b 6e 6f 77 6e 2d 42"));
        workbench.floatSession(0); workbench.selectSession(second);
        auto *path = accessible<QLineEdit>(view, QStringLiteral("近期证据导出路径")); QVERIFY(path);
        path->setText(directory.filePath("first.jsonl"));
        auto *exportButton = button(view, QStringLiteral("后台导出当前筛选")); QVERIFY(exportButton);
        QTest::mouseClick(exportButton, Qt::LeftButton); QTRY_VERIFY(exportButton->isEnabled());
        const auto exported = readFile(path->text()); QVERIFY(exported.contains(first->sessionId().toUtf8()));
        QVERIFY(exported.contains("756e6b6e6f776e2d41")); QVERIFY(!exported.contains("756e6b6e6f776e2d42"));
        QVERIFY(page(otherView, QStringLiteral("回溯与导出")));
        QTest::mouseClick(button(otherView, QStringLiteral("查询证据")), Qt::LeftButton);
        auto *otherOutput = accessible<QPlainTextEdit>(otherView, QStringLiteral("回溯查询结果")); QVERIFY(otherOutput);
        QVERIFY(otherOutput->toPlainText().contains("75 6e 6b 6e 6f 77 6e 2d 42"));
        QVERIFY(!otherOutput->toPlainText().contains(QStringLiteral("错误消息")));
        QCOMPARE(first->state(), SessionState::Running); QCOMPARE(other->state(), SessionState::Running);
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QDir().mkpath(evidence); QVERIFY(view->grab().save(evidence + "/window-trace-query.png"));
            QVERIFY(output->grab().save(evidence + "/window-trace-result.png"));
        }
    }
    void historyDefaultsToOwningWindowAndExportsUnknownData() {
        QTemporaryDir directory; Wire wire; Workbench workbench(catalog_, nullptr, wire.factory());
        workbench.show();
        const int second = workbench.addSession(config("COM912"));
        auto *first = workbench.session(0), *other = workbench.session(second);
        QVERIFY(first->start(config("COM911"))); QVERIFY(other->start(config("COM912")));
        QTRY_COMPARE(first->state(), SessionState::Running); QTRY_COMPARE(other->state(), SessionState::Running);
        auto *view = workbench.sessionView(0); QVERIFY(page(view, QStringLiteral("磁盘与历史")));
        auto *historySession = labeled<QComboBox>(view, QStringLiteral("会话 UUID（必须明确选择）"));
        QVERIFY(historySession); QCOMPARE(historySession->currentText(), first->sessionId());
        auto *recordDirectory = labeled<QLineEdit>(view, QStringLiteral("目录")); QVERIFY(recordDirectory);
        recordDirectory->setText(directory.path());
        QTest::mouseClick(button(view, QStringLiteral("开启后台磁盘记录")), Qt::LeftButton);
        QTRY_COMPARE(first->recordingStats().state, QString("running"));
        first->submit("two-attempts", {}); wire.receive("COM911", "abcdef"); wire.receive("COM911", "gh");
        wire.receive("COM911", "archive-A");
        QTest::mouseClick(button(view, QStringLiteral("停止记录并后台排空")), Qt::LeftButton);
        QTRY_COMPARE(first->recordingStats().state, QString("stopped"));
        RecordingOptions options; options.directory = directory.path();
        QVERIFY(other->startRecording(options)); QTRY_COMPARE(other->recordingStats().state, QString("running"));
        wire.receive("COM912", "archive-B"); auto flush = other->stopRecording();
        QTRY_VERIFY(flush.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
        workbench.selectSession(second); workbench.floatSession(0);
        auto *list = button(view, QStringLiteral("读取可用历史会话列表")); QVERIFY(list);
        QTest::mouseClick(list, Qt::LeftButton); QTRY_VERIFY(list->isEnabled());
        QCOMPARE(historySession->count(), 2); QCOMPARE(historySession->currentText(), first->sessionId());
        auto *query = button(view, QStringLiteral("后台查询历史")); QVERIFY(query);
        QTest::mouseClick(query, Qt::LeftButton); QTRY_VERIFY(query->isEnabled());
        auto *result = accessible<QPlainTextEdit>(view, QStringLiteral("历史查询结果")); QVERIFY(result);
        QVERIFY(result->toPlainText().contains(first->sessionId()));
        QVERIFY(result->toPlainText().contains("61 72 63 68 69 76 65 2d 41"));
        QVERIFY(!result->toPlainText().contains("61 72 63 68 69 76 65 2d 42"));
        auto *path = accessible<QLineEdit>(view, QStringLiteral("历史证据导出路径")); QVERIFY(path);
        path->setText(directory.filePath("history.jsonl"));
        auto *exportButton = button(view, QStringLiteral("后台导出已查询的历史快照")); QVERIFY(exportButton);
        QTest::mouseClick(exportButton, Qt::LeftButton); QTRY_VERIFY(exportButton->isEnabled());
        const auto bytes = readFile(path->text()); QVERIFY(bytes.contains(first->sessionId().toUtf8()));
        QVERIFY(bytes.contains("617263686976652d41")); QVERIFY(!bytes.contains("617263686976652d42"));
        QVERIFY(bytes.contains("\"retention\""));
        QCOMPARE(first->state(), SessionState::Running); QCOMPARE(other->state(), SessionState::Running);
        const auto evidence = qEnvironmentVariable("DEBUGGER_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QDir().mkpath(evidence); QVERIFY(view->grab().save(evidence + "/window-history-export.png"));
            QVERIFY(result->grab().save(evidence + "/window-history-result.png"));
        }
        recordDirectory->setText(directory.filePath("missing-directory"));
        QTest::mouseClick(query, Qt::LeftButton);
        QVERIFY(!exportButton->isEnabled()); // 新查询不能沿用已不再显示的旧目标快照。
        QTRY_VERIFY(query->isEnabled());
        QVERIFY(!exportButton->isEnabled());
    }
    void transmissionAndExpiredSourcesStayExplicit() {
        QTemporaryDir directory; Wire wire; Workbench workbench(catalog_, nullptr, wire.factory());
        workbench.show(); auto *session = workbench.session(0); QVERIFY(session->start(config("COM921")));
        QTRY_COMPARE(session->state(), SessionState::Running);
        const auto operation = session->submit("two-attempts", {}); QVERIFY(operation);
        auto *view = workbench.sessionView(0); QVERIFY(page(view, QStringLiteral("回溯与导出")));
        auto *query = button(view, QStringLiteral("查询证据")); QVERIFY(query); QTest::mouseClick(query, Qt::LeftButton);
        auto *output = accessible<QPlainTextEdit>(view, QStringLiteral("回溯查询结果")); QVERIFY(output);
        QVERIFY(output->toPlainText().contains(QStringLiteral("已提交传输：是")));
        QVERIFY(output->toPlainText().contains(QStringLiteral("本地发送完成：否")));
        wire.receive("COM921", "abcdef"); wire.receive("COM921", "gh");
        QVERIFY(session->configureTrace({1, 256, 128})); wire.receive("COM921", QByteArray(16384, 'z'));
        QTest::mouseClick(query, Qt::LeftButton);
        QVERIFY(output->toPlainText().contains(QStringLiteral("协议结果：success")));
        QVERIFY(output->toPlainText().contains("expired")); QVERIFY(output->toPlainText().contains(QStringLiteral("缺少原始字节")));
        auto *path = accessible<QLineEdit>(view, QStringLiteral("近期证据导出路径")); QVERIFY(path);
        path->setText(directory.filePath("expired.jsonl"));
        auto *exportButton = button(view, QStringLiteral("后台导出当前筛选")); QVERIFY(exportButton);
        QTest::mouseClick(exportButton, Qt::LeftButton); QTRY_VERIFY(exportButton->isEnabled());
        QVERIFY(readFile(path->text()).contains("\"availability\":\"expired\""));
        QVERIFY(page(view, QStringLiteral("通信概要")));
        auto *summary = accessible<QPlainTextEdit>(view, QStringLiteral("本会话通信记录")); QVERIFY(summary);
        QTRY_VERIFY(summary->toPlainText().contains(QStringLiteral("正常过期")));
        QVERIFY(summary->toPlainText().contains(QStringLiteral("完整字节见导出")));
        QVERIFY(summary->toPlainText().size() < 2000); // 单条大输入不把整个包复制到概要文档。
        QCOMPARE(session->state(), SessionState::Running);
    }
};
QTEST_MAIN(WindowTraceTests)
#include "window_trace_tests.moc"
