#include <QApplication>
#include <QCanBus>
#include <QObject>
#include <QSerialPort>
#include <QTcpSocket>
#include <QWidget>
#include <iostream>

class MocProbe final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MocProbe probe;
    QWidget widget;
    QSerialPort serial;
    QTcpSocket socket;
    auto *canBus = QCanBus::instance();
    if (!canBus || sizeof(void *) != 8)
        return 1;

    std::cout << "Qt=" << qVersion()
              << " C++=" << __cplusplus
              << " PointerBits=" << sizeof(void *) * 8
              << " MOC=" << probe.metaObject()->className()
              << " CANPlugins=" << canBus->plugins().join(',').toStdString()
              << '\n';
    return 0;
}

#include "main.moc"
