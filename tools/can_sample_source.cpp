#include <QCoreApplication>
#include <QCanBus>
#include <QCanBusDevice>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setApplicationName("can_sample_source");
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("独立虚拟 CAN 样例源，仅软件测试，不连接物理设备")); parser.addHelpOption();
    parser.addOption({"interface", QStringLiteral("虚拟测试通道 can0..can9"), "channel", "can0"});
    parser.addOption({"count", QStringLiteral("发送帧数，1..100000"), "count", "20"});
    parser.addOption({"interval", QStringLiteral("样例发送间隔毫秒，10..60000"), "ms", "500"});
    parser.addOption({"profile", QStringLiteral("样例类型 raw 或 upp；UPP是暂定规则软件向量"), "name", "raw"});
    parser.addOption({"can-id", QStringLiteral("UPP样例CAN ID，十六进制；默认5"), "id", "5"});
    parser.addOption({"extended", QStringLiteral("UPP样例使用扩展CAN帧")});
    // QCommandLineParser::process/showHelp 在无控制台的 Windows 上会弹窗；工具始终只写标准流。
    if (!parser.parse(app.arguments())) { fprintf(stderr, "%s\n", qPrintable(parser.errorText())); return 2; }
    if (parser.isSet("help") || app.arguments().contains("--help-all")) { const auto help = parser.helpText().toUtf8(); fwrite(help.constData(), 1, help.size(), stdout); return 0; }
    bool countOk, intervalOk; const int count = parser.value("count").toInt(&countOk), interval = parser.value("interval").toInt(&intervalOk);
    if (!countOk || !intervalOk || count < 1 || count > 100000 || interval < 10 || interval > 60000 || !QRegularExpression("^can[0-9]$").match(parser.value("interface")).hasMatch()) return 2;
    const auto profile = parser.value("profile"); bool idOk;
    const auto canId = parser.value("can-id").toUInt(&idOk, 16); const bool extended = parser.isSet("extended");
    if ((profile != "raw" && profile != "upp") || !idOk || canId > (extended ? 0x1fffffff : 0x7ff)) return 2;
    // 原文结构 + 独立计算的Modbus CRC；不是设备抓包或协议方黄金向量。
    const QList<QByteArray> uppFragments{QByteArray::fromHex("05500701204889"), QByteArray::fromHex("05500f0120323041"), QByteArray::fromHex("41ffffffffb77a")};
    QString error; std::unique_ptr<QCanBusDevice> device(QCanBus::instance()->createDevice("virtualcan", parser.value("interface"), &error));
    if (!device) { fprintf(stderr, "%s\n", qPrintable(error)); return 3; }
    QTimer timer; timer.setInterval(interval); int sent = 0;
    QObject::connect(device.get(), &QCanBusDevice::stateChanged, &app, [&](auto state) { if (state == QCanBusDevice::ConnectedState) timer.start(); });
    QObject::connect(device.get(), &QCanBusDevice::errorOccurred, &app, [&](auto code) { if (code != QCanBusDevice::NoError) { fprintf(stderr, "%s\n", qPrintable(device->errorString())); app.exit(4); } });
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        QCanBusFrame frame;
        if (profile == "upp") { frame = QCanBusFrame(canId, uppFragments[sent % uppFragments.size()]); frame.setExtendedFrameFormat(extended); }
        else switch (sent % 4) {
        case 0: frame = QCanBusFrame(0x123, QByteArray::fromHex("0102030405060708")); break;
        case 1: frame = QCanBusFrame(0x1abcde3, QByteArray::fromHex("deadbeef")); frame.setExtendedFrameFormat(true); break;
        case 2: frame = QCanBusFrame(0x456, {}); break;
        case 3: frame.setFrameType(QCanBusFrame::RemoteRequestFrame); frame.setFrameId(0x321); frame.setPayload(QByteArray(4, '\0')); break;
        }
        if (!device->writeFrame(frame)) { app.exit(5); return; }
        const QJsonObject log{{"source", "virtual-can-sample-only"}, {"profile", profile}, {"index", ++sent}, {"canId", int(frame.frameId())}, {"extended", frame.hasExtendedFrameFormat()}, {"qtFrameType", int(frame.frameType())}, {"qtPayloadHex", QString::fromLatin1(frame.payload().toHex())}};
        const auto line = QJsonDocument(log).toJson(QJsonDocument::Compact); fprintf(stdout, "%s\n", line.constData()); fflush(stdout);
        if (sent == count) { timer.stop(); QTimer::singleShot(100, &app, &QCoreApplication::quit); }
    });
    if (!device->connectDevice()) return 6;
    QTimer::singleShot(5000, &app, [&] { if (device->state() != QCanBusDevice::ConnectedState) app.exit(7); });
    const int result = app.exec(); device->disconnectDevice(); return result;
}
