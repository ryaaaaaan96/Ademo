#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>

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
// 成功返回 0；其余为 Modbus 异常码（01/02/03）。不修改模拟数据。
int decodeWritePdu(const QByteArray &pdu, WritePdu &output, QJsonArray &values);
}
