#include "debugger/sdk.h"
#include <QLabel>
#include <QObject>

using namespace debugger;
class RtuSession final : public ProtocolSession {
public:
    QString prepare(const SessionConfig &config) override {
        return config.role == "master" ? QString() : QStringLiteral("当前仅支持 RTU 主站会话");
    }
    void activate() override {}
    void stop() override {}
};
class RtuView final : public QLabel {
public:
    explicit RtuView(QWidget *parent)
        : QLabel(QStringLiteral("RTU 主站\n配置串口后启动会话。\n当前交付：端点启停；寄存器读写由后续 02 实施。"), parent) {
        setObjectName("protocolView");
        setWordWrap(true);
    }
    ~RtuView() override = default;
};
class modbusPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override {
        return {"modbus.rtu", QStringLiteral("Modbus RTU"), buildKey(), {"master"},
                {"serial"}, {"lifecycle"}, {}};
    }
    std::unique_ptr<ProtocolSession> createSession() override { return std::make_unique<RtuSession>(); }
    QWidget *createView(QWidget *parent) override {
        return new RtuView(parent);
    }
};
#include "modbus.moc"
