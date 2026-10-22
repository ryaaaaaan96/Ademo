#include "debugger/sdk.h"
using namespace debugger;
// 只用于公开会话的记录契约测试，永不放入产品插件目录。
class TraceFixtureSession final : public ProtocolSession {
public:
    explicit TraceFixtureSession(ProtocolServices services) : services_(std::move(services)) {}
    QString prepare(const SessionConfig &) override { return {}; }
    void activate() override {}
    void stop() override {}
    void submit(const OperationRequest &request) override {
        operation_ = request.id;
        services_.send(operation_, "attempt1"); services_.send(operation_, "attempt2");
    }
    void onInput(const TraceRecord &record) override {
        if (record.bytes == "abcdef") {
            first_ = record.id;
            Message a{0, operation_, {record.id}, "success", QStringLiteral("第一条消息"), {}};
            a.sourceRanges = {{record.id, 0, 2}}; services_.publishMessage(a);
            Message b{0, operation_, {record.id}, "invalid", QStringLiteral("错误消息，\"原始\"\n仍可回溯"), {}};
            b.sourceRanges = {{record.id, 2, 2}}; services_.publishMessage(b);
        } else if (record.bytes == "gh") {
            Message c{0, operation_, {first_, record.id}, "success", QStringLiteral("跨块消息"), {}};
            c.sourceRanges = {{first_, 4, 2}, {record.id, 0, 2}}; services_.publishMessage(c);
            services_.publish({operation_, "success", QStringLiteral("测试交互完成"), {}});
        }
    }
    void onSent(quint64, bool, const QString &, qint64) override {}
    void tick() override {}
private:
    ProtocolServices services_;
    quint64 operation_ = 0;
    quint64 first_ = 0;
};
class TraceFixturePlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override { return {"trace.fixture", "trace.fixture", buildKey(), {"master"}, {"serial"}, {}, {}}; }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override { return std::make_unique<TraceFixtureSession>(std::move(services)); }
    QWidget *createView(SessionAccess, QWidget *) override { return nullptr; }
};
#include "trace_fixture_plugin.moc"
