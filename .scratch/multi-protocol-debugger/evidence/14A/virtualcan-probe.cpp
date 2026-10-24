#include <QCoreApplication>
#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cstdio>
int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const auto bytes = message.toUtf8(); std::fprintf(stdout, "%s\n", bytes.constData()); std::fflush(stdout);
    });
    QCoreApplication app(argc, argv);
    QString error;
    auto *rx = QCanBus::instance()->createDevice("virtualcan", "can9", &error);
    auto *tx = QCanBus::instance()->createDevice("virtualcan", "can9", &error);
    if (!rx || !tx) { qCritical() << error; return 1; }
    rx->setConfigurationParameter(QCanBusDevice::CanFdKey, false);
    tx->setConfigurationParameter(QCanBusDevice::CanFdKey, true);
    auto describe = [](const char *stage, const QCanBusFrame &f) {
        const QJsonObject o{{"stage", stage}, {"id", int(f.frameId())}, {"type", int(f.frameType())},
            {"extended", f.hasExtendedFrameFormat()}, {"fd", f.hasFlexibleDataRateFormat()},
            {"valid", f.isValid()}, {"payloadHex", QString::fromLatin1(f.payload().toHex())},
            {"payloadSize", int(f.payload().size())}, {"errors", int(f.error())},
            {"timestampSeconds", QString::number(f.timeStamp().seconds())},
            {"timestampMicros", QString::number(f.timeStamp().microSeconds())}, {"echo", f.hasLocalEcho()}};
        qInfo().noquote() << QJsonDocument(o).toJson(QJsonDocument::Compact);
    };
    QObject::connect(rx, &QCanBusDevice::framesReceived, rx, [=] {
        while (rx->framesAvailable()) describe("received", rx->readFrame());
    });
    QObject::connect(rx, &QCanBusDevice::errorOccurred, rx, [=](QCanBusDevice::CanBusError e) { qWarning() << "rxError" << e << rx->errorString(); });
    QObject::connect(tx, &QCanBusDevice::errorOccurred, tx, [=](QCanBusDevice::CanBusError e) { qWarning() << "txError" << e << tx->errorString(); });
    if (!rx->connectDevice() || !tx->connectDevice()) return 2;
    QTimer::singleShot(500, &app, [=] {
        QList<QCanBusFrame> frames;
        frames << QCanBusFrame(0x123, QByteArray::fromHex("010203"));
        QCanBusFrame extended(0x123, QByteArray::fromHex("abcdef")); extended.setExtendedFrameFormat(true); frames << extended;
        frames << QCanBusFrame(0x124, {});
        QCanBusFrame rtr(0x125, QByteArray::fromHex("deadbeef")); rtr.setFrameType(QCanBusFrame::RemoteRequestFrame); frames << rtr;
        QCanBusFrame rtr0(0x126, {}); rtr0.setFrameType(QCanBusFrame::RemoteRequestFrame); frames << rtr0;
        QCanBusFrame err(QCanBusFrame::ErrorFrame); err.setError(QCanBusFrame::BusOffError); frames << err;
        QCanBusFrame errData(QCanBusFrame::ErrorFrame); errData.setError(QCanBusFrame::BusError); errData.setPayload(QByteArray::fromHex("aabb")); frames << errData;
        QCanBusFrame fd(0x127, QByteArray(12, 'x')); frames << fd;
        for (const auto &f : frames) { describe("sent-attempt", f); qInfo() << "accepted" << tx->writeFrame(f); }
    });
    QTimer::singleShot(2200, &app, &QCoreApplication::quit);
    const auto result = app.exec(); tx->disconnectDevice(); rx->disconnectDevice(); delete tx; delete rx; return result;
}
