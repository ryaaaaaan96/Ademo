#include "can_endpoint.h"
#include <QCanBus>
#include <QRegularExpression>
using namespace debugger;
class CanPlugin final : public QObject, public TransportPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerTransportIid)
    Q_INTERFACES(debugger::TransportPlugin)
public:
    Descriptor describe() const override {
        return {"can", QStringLiteral("CAN（Qt SerialBus）"), buildKey(), {}, {}, {"frame"},
            {{"backend", QStringLiteral("CAN 后端"), QCanBus::instance()->plugins(), "virtualcan"},
             {"interface", QStringLiteral("接口（虚拟测试 can0..can9）"), {}, "can0"}}};
    }
    QString validate(const QJsonObject &config) const override {
        const auto backend = config.value("backend").toString();
        const auto interface = config.value("interface").toString();
        if (!QCanBus::instance()->plugins().contains(backend)) return QStringLiteral("未安装所选 Qt CAN 后端");
        if (interface.isEmpty() || interface.trimmed() != interface) return QStringLiteral("CAN 接口名称不能为空或包含首尾空白");
        if (backend == "virtualcan" && !QRegularExpression("^can[0-9]$").match(interface).hasMatch())
            return QStringLiteral("虚拟 CAN 测试接口使用 can0..can9");
        return {};
    }
    QString endpointKey(const QJsonObject &config) const override { return "can:" + config.value("backend").toString() + ":" + config.value("interface").toString(); }
    std::unique_ptr<Endpoint> createEndpoint(const QJsonObject &config, EndpointEvents events) override { return createCanEndpoint(config, std::move(events)); }
};
#include "can.moc"
