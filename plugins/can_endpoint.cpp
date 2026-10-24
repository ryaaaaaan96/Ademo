#include "debugger/sdk.h"
#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QTimer>
#include "can_endpoint.h"
using namespace debugger;
namespace {
ReceiveChunk observedFrame(const QCanBusFrame &frame, const QJsonObject &config) {
    const bool remote = frame.frameType() == QCanBusFrame::RemoteRequestFrame;
    const bool error = frame.frameType() == QCanBusFrame::ErrorFrame;
    const bool identityAvailable = frame.isValid() && (remote || frame.frameType() == QCanBusFrame::DataFrame);
    ReceiveChunk input(remote ? QByteArray{} : frame.payload());
    const auto type = remote ? "remote" : error ? "error" : frame.frameType() == QCanBusFrame::DataFrame ? "data" : "unknown";
    const bool supported = (remote || error || frame.frameType() == QCanBusFrame::DataFrame)
        && !frame.hasFlexibleDataRateFormat() && frame.payload().size() <= 8 && frame.isValid();
    input.metadata = {{"unitKind", "can-frame"}, {"frameType", type}, {"qtFrameType", int(frame.frameType())}, {"formatAvailable", identityAvailable}, {"extended", frame.hasExtendedFrameFormat()},
        {"canIdAvailable", identityAvailable}, {"canId", identityAvailable ? QJsonValue(int(frame.frameId())) : QJsonValue()}, {"qtFrameId", int(frame.frameId())},
        {"payloadSemantics", remote ? "no-wire-data; Qt-placeholder-in-metadata" : error ? "backend-error-data" : "CAN-data"},
        {"dataLength", input.bytes.size()}, {"declaredDlc", QJsonValue()}, {"declaredDlcSource", "unavailable-in-Qt-API"},
        {"qtPayloadLength", frame.payload().size()}, {"remoteExpectedLength", remote ? QJsonValue(frame.payload().size()) : QJsonValue()},
        {"remoteLengthSource", remote ? "Qt-payload-placeholder-length" : "not-applicable"},
        {"qtRemotePlaceholderHex", remote ? QString::fromLatin1(frame.payload().toHex()) : QString()},
        {"errorFlags", error ? QJsonValue(int(frame.error())) : QJsonValue()},
        {"fd", frame.hasFlexibleDataRateFormat()}, {"bitrateSwitch", frame.hasBitrateSwitch()},
        {"errorStateIndicator", frame.hasErrorStateIndicator()}, {"localEcho", frame.hasLocalEcho()},
        {"valid", frame.isValid()}, {"classicSupported", supported},
        {"diagnostic", supported ? QString() : QStringLiteral("不支持的 CAN FD、未知或无效帧；原始后端信息保留，未截断")},
        {"backend", config.value("backend")}, {"interface", config.value("interface")},
        {"sourceKind", config.value("backend") == "virtualcan" ? "virtual-can-test" : "can-backend"},
        {"backendTimestampSeconds", QString::number(frame.timeStamp().seconds())},
        {"backendTimestampMicroseconds", QString::number(frame.timeStamp().microSeconds())},
        {"backendTimestampSource", "Qt-backend; clock-and-precision-unconfirmed"}};
    return input;
}
class CanEndpoint final : public QObject, public Endpoint {
public:
    CanEndpoint(QJsonObject config, EndpointEvents events, CanDeviceFactory factory) : config_(std::move(config)), events_(std::move(events)), factory_(std::move(factory)) {}
    ~CanEndpoint() override { close(); }
    void open() override {
        QString error;
        device_ = factory_ ? factory_(config_, &error) : QCanBus::instance()->createDevice(config_.value("backend").toString(), config_.value("interface").toString(), &error);
        if (!device_) { events_.opened(false, error); return; }
        device_->setParent(this);
        connect(device_, &QCanBusDevice::stateChanged, this, [this](QCanBusDevice::CanBusDeviceState state) {
            if (closing_) return;
            if (state == QCanBusDevice::ConnectedState) { opened_ = true; events_.opened(true, {}); }
            else if (state == QCanBusDevice::UnconnectedState && opened_) events_.failed(QStringLiteral("CAN 后端已断开"));
        });
        connect(device_, &QCanBusDevice::errorOccurred, this, [this](QCanBusDevice::CanBusError error) {
            if (closing_ || error == QCanBusDevice::NoError) return;
            if (opened_) events_.failed(device_->errorString());
            else events_.opened(false, device_->errorString());
        });
        connect(device_, &QCanBusDevice::framesReceived, this, [this] { drain(); });
        // 只观察不承诺硬件静默模式；不设置后端未声明支持的监听配置。
        if (!device_->connectDevice()) events_.opened(false, device_->errorString());
    }
    void close() override { if (closing_) return; closing_ = true; if (device_ && device_->state() != QCanBusDevice::UnconnectedState) device_->disconnectDevice(); }
    bool send(quint64, const QByteArray &) override { return false; }
private:
    void drain() {
        if (closing_ || !device_) return;
        int count = 0;
        while (device_->framesAvailable() && count++ < 256) events_.received(observedFrame(device_->readFrame(), config_));
        if (device_->framesAvailable() && !scheduled_) {
            scheduled_ = true;
            QTimer::singleShot(0, this, [this] { scheduled_ = false; drain(); });
        }
    }
    CanDeviceFactory factory_;
    QJsonObject config_;
    EndpointEvents events_;
    QCanBusDevice *device_ = nullptr;
    bool closing_ = false, opened_ = false, scheduled_ = false;
};
}
std::unique_ptr<Endpoint> createCanEndpoint(const QJsonObject &config, EndpointEvents events, CanDeviceFactory factory) { return std::make_unique<CanEndpoint>(config, std::move(events), std::move(factory)); }
