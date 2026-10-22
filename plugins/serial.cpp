#include "debugger/sdk.h"
#include <QSerialPort>
#include <QObject>

using namespace debugger;
namespace {
QString portName(const QJsonObject &config)
{
    auto name = config.value("port").toString().trimmed();
    if (name.startsWith("\\\\.\\")) name.remove(0, 4);
    return name.toUpper();
}
class SerialEndpoint final : public Endpoint {
public:
    SerialEndpoint(QJsonObject config, EndpointEvents events)
        : config_(std::move(config)), events_(std::move(events)) {
        port_.setReadBufferSize(65536);
        port_.setWriteBufferSize(256);
        QObject::connect(&port_, &QSerialPort::readyRead, &port_, [this] {
            while (port_.bytesAvailable() > 0) {
                const auto bytes = port_.read(4096);
                if (bytes.isEmpty()) break;
                events_.received(bytes);
            }
        });
        QObject::connect(&port_, &QSerialPort::bytesWritten, &port_, [this](qint64 count) {
            if (!sendId_) return;
            written_ += count;
            if (written_ == outgoing_.size() && accepted_ == outgoing_.size()) finishSend(true, {});
            else pumpWrite();
        });
        QObject::connect(&port_, &QSerialPort::errorOccurred, &port_, [this](QSerialPort::SerialPortError error) {
            if (!opening_ && error != QSerialPort::NoError) {
                finishSend(false, port_.errorString());
                events_.failed(QStringLiteral("串口错误：") + port_.errorString());
            }
        });
    }
    void open() override {
        opening_ = true;
        port_.setPortName(portName(config_));
        bool configured = port_.setBaudRate(config_.value("baud").toString().toInt());
        configured = port_.setDataBits(static_cast<QSerialPort::DataBits>(config_.value("dataBits").toString().toInt())) && configured;
        const auto parity = config_.value("parity").toString();
        configured = port_.setParity(parity == "even" ? QSerialPort::EvenParity
                                     : parity == "odd" ? QSerialPort::OddParity : QSerialPort::NoParity) && configured;
        configured = port_.setStopBits(config_.value("stopBits").toString() == "2"
                                      ? QSerialPort::TwoStop : QSerialPort::OneStop) && configured;
        configured = port_.setFlowControl(QSerialPort::NoFlowControl) && configured;
        const bool ok = configured && port_.open(QIODevice::ReadWrite);
        opening_ = false;
        events_.opened(ok, ok ? QString() : QStringLiteral("无法打开串口 %1：%2").arg(portName(config_), port_.errorString()));
    }
    void close() override {
        opening_ = true;
        finishSend(false, QStringLiteral("串口已关闭；已交给后端的字节不能撤回"));
        port_.close();
    }
    bool send(quint64 id, const QByteArray &bytes) override {
        if (!port_.isOpen() || sendId_ || !id || bytes.isEmpty() || bytes.size() > 256) return false;
        sendId_ = id;
        outgoing_ = bytes;
        accepted_ = written_ = 0;
        pumpWrite();
        return true; // 队列接纳，不是本地完成或设备确认。
    }
private:
    void pumpWrite() {
        if (!sendId_ || accepted_ == outgoing_.size()) return;
        const auto count = port_.write(outgoing_.constData() + accepted_, outgoing_.size() - accepted_);
        if (count < 0) finishSend(false, port_.errorString());
        else accepted_ += count;
    }
    void finishSend(bool ok, const QString &reason) {
        if (!sendId_) return;
        const auto id = sendId_;
        sendId_ = 0;
        outgoing_.clear();
        events_.sent(id, ok, reason);
    }
    QSerialPort port_;
    QJsonObject config_;
    EndpointEvents events_;
    bool opening_ = false;
    quint64 sendId_ = 0;
    QByteArray outgoing_;
    qint64 accepted_ = 0;
    qint64 written_ = 0;
};
}
class serialPlugin final : public QObject, public TransportPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerTransportIid)
    Q_INTERFACES(debugger::TransportPlugin)
public:
    Descriptor describe() const override {
        return {"serial", QStringLiteral("串口"), buildKey(), {}, {}, {"byte-stream"},
                {{"port", QStringLiteral("串口名称"), {}, "COM1"},
                 {"baud", QStringLiteral("波特率"), {"9600", "19200", "38400", "57600", "115200"}, "9600"},
                 {"dataBits", QStringLiteral("数据位"), {"8"}, "8"},
                 {"parity", QStringLiteral("校验"), {"none", "even", "odd"}, "even"},
                 {"stopBits", QStringLiteral("停止位"), {"1", "2"}, "1"}}};
    }
    QString validate(const QJsonObject &config) const override {
        const auto name = portName(config);
        if (!name.startsWith("COM") || name.mid(3).toInt() <= 0
            || name != "COM" + QString::number(name.mid(3).toInt()))
            return QStringLiteral("请输入有效的 Windows 串口名称，例如 COM3");
        const auto fields = describe().fields;
        for (const auto &field : fields) {
            if (!field.choices.isEmpty() && !field.choices.contains(config.value(field.key).toString()))
                return QStringLiteral("无效的串口参数：") + field.label;
        }
        return {};
    }
    QString endpointKey(const QJsonObject &config) const override { return "serial:" + portName(config); }
    std::unique_ptr<Endpoint> createEndpoint(const QJsonObject &config, EndpointEvents events) override {
        return std::make_unique<SerialEndpoint>(config, std::move(events));
    }
};
#include "serial.moc"
