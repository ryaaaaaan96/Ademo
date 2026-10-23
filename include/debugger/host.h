#pragma once
#include "sdk.h"
#include "trace_query.h"
#include "workspace.h"
#include "recording.h"
#include <QObject>
#include <QPointer>
#include <QPluginLoader>
#include <QSet>
#include <QElapsedTimer>
#include <QTimer>
#include <QMainWindow>
#include <optional>
#include <vector>
class QTreeWidget;
class QDockWidget;
class QLabel;
class QShowEvent;
class QCloseEvent;

namespace debugger {
// 注册表由会话和视图共同持有，确保 DLL 晚于其创建的对象销毁。
class PluginCatalog : public std::enable_shared_from_this<PluginCatalog> {
public:
    ~PluginCatalog();
    void discover(const QString &directory);
    QList<ProtocolPlugin *> protocols() const { return protocols_; }
    QList<TransportPlugin *> transports() const { return transports_; }
    QStringList diagnostics() const { return diagnostics_; }
    ProtocolPlugin *protocol(const QString &) const;
    TransportPlugin *transport(const QString &) const;
    QWidget *createView(const QString &, QWidget *parent, SessionAccess = {});
    ParameterView *createParameterView(const QString &, QWidget *parent, SessionAccess = {});
    bool acquire(const QString &key);
    void release(const QString &key);
private:
    std::vector<std::unique_ptr<QPluginLoader>> loaders_;
    QList<ProtocolPlugin *> protocols_;
    QList<TransportPlugin *> transports_;
    QStringList diagnostics_;
    QSet<QString> occupied_;
};
enum class SessionState { Stopped, Starting, Running, Error };
QString stateText(SessionState);
class DebugSession : public QObject {
    Q_OBJECT
public:
    using EndpointFactory = std::function<std::unique_ptr<Endpoint>(const QJsonObject &, EndpointEvents)>;
    using BackgroundExecutor = std::function<void(std::function<void()>)>;
    explicit DebugSession(std::shared_ptr<PluginCatalog>, QObject *parent = nullptr,
                          EndpointFactory endpointFactory = {}, std::function<qint64()> clock = {},
                          BackgroundExecutor preparationExecutor = {});
    ~DebugSession() override;
    bool start(const SessionConfig &);
    bool reconnect();
    bool configureRecovery(QJsonObject);
    bool canReconnect() const { return recoverable_ && protocol_ && !preparing_ && state_ == SessionState::Error; }
    void stop();
    SessionState state() const { return state_; }
    QString diagnostic() const { return diagnostic_; }
    quint64 generation() const { return generation_; }
    SessionConfig config() const { return config_; }
    bool configure(QJsonObject protocol);
    QJsonObject snapshot() const;
    bool resolveUnsaved();
    bool hasUnsavedOrBusy() const;
    quint64 submit(const QString &action, const QJsonObject &parameters);
    OperationSnapshot operation(quint64) const;
    QList<TraceRecord> traces(quint64 operationId = 0) const;
    QList<Message> messages(quint64 operationId = 0) const;
    std::optional<TraceRecord> trace(quint64 id) const;
    quint64 expiredTraceCount() const { return expiredTraces_; }
    bool configureTrace(TraceLimits, QString *error = nullptr);
    TraceLimits traceLimits() const { return traceLimits_; }
    TraceSnapshot queryTrace(const TraceFilter & = {}) const;
    std::shared_future<ExportResult> exportTrace(const TraceFilter &, const QString &path, const QString &format) const;
    QString sessionId() const { return sessionId_; }
    bool startRecording(RecordingOptions, QString *error = nullptr, std::function<void()> beforeBatch = {});
    RecordingStats recordingStats() const;
    std::shared_future<RecordingStats> stopRecording();
    SessionAccess access();
    void poll(); // 可控时钟测试入口；产品由精确 Qt 定时器驱动。
signals:
    void changed();
private:
    bool prepared(quint64 run, const QString &error);
    bool openConnection();
    void connectionFailed(const QString &);
    void opened(quint64, bool, const QString &);
    void fail(const QString &);
    void retire(bool retainLocal = false);
    bool ownsProtocol(quint64 run) const;
    bool background(quint64 run, std::function<QJsonObject()>, std::function<void(QJsonObject)>);
    qint64 nowUs() const;
    quint64 appendTrace(const QString &, const QByteArray & = {}, quint64 operationId = 0,
                        const QString &detail = {}, qint64 observedUs = -1, QJsonObject metadata = {},
                        std::optional<qint64> idleBeforeUs = {});
    void received(quint64 run, ReceiveChunk, qint64 observedUs);
    void sent(quint64 run, quint64 id, bool ok, const QString &, qint64 observedUs);
    bool send(quint64 run, quint64 id, const QByteArray &);
    quint64 sendPassive(quint64 run, const QByteArray &);
    quint64 allocateSendToken() { return (quint64(1) << 63) | nextSendToken_++; }
    void publish(OperationUpdate);
    void publishMessage(Message);
    void recordTrace(const TraceRecord &);
    void recordOperation(const OperationSnapshot &);
    void recordSessionConfiguration();
    std::shared_ptr<PluginCatalog> catalog_;
    std::shared_ptr<ProtocolSession> protocol_;
    std::unique_ptr<Endpoint> endpoint_;
    SessionConfig config_;
    SessionState state_ = SessionState::Stopped;
    QString diagnostic_;
    QString key_;
    quint64 generation_ = 0;
    quint64 protocolRun_ = 0;
    bool recoverable_ = false;
    bool activated_ = false;
    qint64 reconnectDueUs_ = -1;
    std::function<bool()> discardGuard_;
    bool resolvingUnsaved_ = false;
    EndpointFactory endpointFactory_;
    std::function<qint64()> clock_;
    QElapsedTimer elapsed_;
    QTimer pollTimer_;
    QTimer notificationTimer_;
    QList<TraceRecord> traces_;
    QList<Message> messages_;
    QList<OperationSnapshot> operations_;
    quint64 nextTraceId_ = 1;
    quint64 nextMessageId_ = 1;
    quint64 nextOperationId_ = 1;
    quint64 expiredTraces_ = 0;
    bool preparing_ = false;
    BackgroundExecutor preparationExecutor_;
    QSet<quint64> passiveSends_;
    QMap<quint64, qint64> passiveDeadlines_;
    quint64 expiredMessages_ = 0;
    quint64 expiredOperations_ = 0;
    TraceLimits traceLimits_;
    struct SendAttempt { quint64 token = 0; int attempt = 0; bool completed = false; };
    QMap<quint64, SendAttempt> sendAttempts_;
    quint64 nextSendToken_ = 1;
    QString sessionId_;
    std::unique_ptr<DiskRecorder> recorder_;
};
class SessionPane;
class SessionDock;
enum class DockDragPhase;
class Workbench : public QMainWindow {
public:
    explicit Workbench(std::shared_ptr<PluginCatalog>, QWidget *parent = nullptr,
                       DebugSession::EndpointFactory endpointFactory = {}, std::function<qint64()> clock = {},
                       WorkspaceController::Executor workspaceExecutor = {});
    ~Workbench() override;
    int addSession(const SessionConfig & = {}, const QString &title = {});
    bool closeSession(int index);
    int sessionCount() const;
    DebugSession *session(int index) const;
    ParameterView *parameterView(int index) const;
    QWidget *sessionView(int index) const;
    QMainWindow *sessionWindow(int index) const;
    QString sessionTitle(int index) const;
    bool isSessionOpen(int index) const;
    bool isSessionFloating(int index) const;
    void floatSession(int index);
    void dockSession(int index);
    void tileSessions(Qt::Orientation orientation);
    bool selectDevice(int index, const QString &deviceId);
    SessionConfig sessionConfiguration(int index) const;
    int currentSessionIndex() const;
    void selectSession(int index);
    quint64 openWorkspace(const QString &path);
    quint64 saveWorkspace(const QString &path);
    WorkspaceOperation workspaceOperation(quint64 id) const;
protected:
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    friend class WorkspaceController;
    std::shared_ptr<PluginCatalog> catalog_;
    DebugSession::EndpointFactory factory_;
    std::function<qint64()> clock_;
    void clearSessions();
    void refreshNavigation();
    void dragSession(SessionDock *, QPoint, DockDragPhase);
    QList<SessionDock *> docks_;
    QTreeWidget *navigation_ = nullptr;
    QDockWidget *navigationDock_ = nullptr;
    QLabel *dockPreview_ = nullptr;
    int currentSession_ = -1;
    QString navigationSignature_;
    bool presented_ = false;
    int nextSessionNumber_ = 1;
    std::unique_ptr<WorkspaceController> workspace_;
};
}

