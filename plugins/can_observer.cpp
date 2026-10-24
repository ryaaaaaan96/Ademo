#include "debugger/sdk.h"
#include "can_observer_view.h"
#include <QLabel>
using namespace debugger;
namespace {
class Observation final : public ProtocolSession {
    ProtocolServices services_;
public:
    explicit Observation(ProtocolServices services) : services_(std::move(services)) {}
    QString prepare(const SessionConfig &) override { return {}; }
    void activate() override {} void stop() override {}
    void submit(const OperationRequest &request) override { if (services_.publish) services_.publish({request.id, "rejected", QStringLiteral("原始帧观察不提供业务发送或读取操作"), {}}); }
    void onInput(const TraceRecord &) override {} void onSent(quint64, bool, const QString &, qint64) override {} void tick() override {}
};
}
class CanObserverPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override { return {"can.raw", QStringLiteral("CAN 原始帧观察"), buildKey(), {"observer"}, {"can"}, {}, {}, {"frame"}}; }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override { return std::make_unique<Observation>(std::move(services)); }
    QWidget *createView(SessionAccess access, QWidget *parent) override { return createCanObserverView(std::move(access), parent); }
    ParameterView *createParameterView(SessionAccess access, QWidget *parent) override { return createCanObserverView(std::move(access), parent); }
};
#include "can_observer.moc"
