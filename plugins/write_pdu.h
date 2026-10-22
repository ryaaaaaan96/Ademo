#pragma once
#include <QByteArray>
#include <QJsonObject>

namespace debugger {
// 供主站编码及从站执行复用的写入语义；不持有通道或运行状态。
struct WritePdu {
    int function = 0;
    int start = 0;
    int count = 0;
    QByteArray pdu;
    QByteArray acknowledgement;
};
bool isWriteAction(const QString &action);
QString encodeWritePdu(const QString &action, const QJsonObject &parameters, WritePdu &output);
}
