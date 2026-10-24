#include "upp_session.h"
using namespace debugger;
class UppPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"upp", QStringLiteral("UPP CAN 交互（暂定规则）"), buildKey(), {"observer", "master"}, {"can"}, {},
            {{"direction", QStringLiteral("报文方向"), {"auto", "request", "response"}, "auto"},
             {"fragmentTimeoutMs", QStringLiteral("片间超时(ms)"), {}, "100"},
             {"packetTimeoutMs", QStringLiteral("整包超时(ms)"), {}, "1000"},
             {"requestCanIds", QStringLiteral("请求CAN ID(十六进制逗号分隔)"), {}, ""},
             {"responseCanIds", QStringLiteral("响应CAN ID(十六进制逗号分隔)"), {}, ""},
             {"soeModels", QStringLiteral("明确为SOE的模型号(十进制逗号分隔)"), {}, ""},
             {"sendGapMs", QStringLiteral("发送片间隔(ms)"), {}, "20"},
             {"responseTimeoutMs", QStringLiteral("应答超时(ms)"), {}, "1000"},
             {"deviceRoutes", QStringLiteral("设备路由JSON(设备号:txId/rxId/extended)"), {}, "{}"}}, {"frame"}};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override { return createUppSession(std::move(services)); }
    QWidget *createView(SessionAccess access, QWidget *parent) override { return createUppView(std::move(access), parent); }
    ParameterView *createParameterView(SessionAccess access, QWidget *parent) override { return createUppView(std::move(access), parent); }
};
#include "upp.moc"
