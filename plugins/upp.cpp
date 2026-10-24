#include "upp_session.h"
using namespace debugger;
class UppPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"upp", QStringLiteral("UPP CAN 接收解析（暂定规则）"), buildKey(), {"observer"}, {"can"}, {},
            {{"direction", QStringLiteral("报文方向"), {"auto", "request", "response"}, "auto"},
             {"fragmentTimeoutMs", QStringLiteral("片间超时(ms)"), {}, "100"},
             {"packetTimeoutMs", QStringLiteral("整包超时(ms)"), {}, "1000"},
             {"requestCanIds", QStringLiteral("请求CAN ID(十六进制逗号分隔)"), {}, ""},
             {"responseCanIds", QStringLiteral("响应CAN ID(十六进制逗号分隔)"), {}, ""},
             {"soeModels", QStringLiteral("明确为SOE的模型号(十进制逗号分隔)"), {}, ""}}, {"frame"}};
    }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override { return createUppSession(std::move(services)); }
    QWidget *createView(SessionAccess access, QWidget *parent) override { return createUppView(std::move(access), parent); }
    ParameterView *createParameterView(SessionAccess access, QWidget *parent) override { return createUppView(std::move(access), parent); }
};
#include "upp.moc"
