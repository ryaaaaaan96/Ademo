#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

namespace debugger {
struct ParameterAddress { int unit; QString area; int address; };
// 只合并同设备同区的连续地址，并按 Modbus 读请求上限拆分；不扩大用户选区。
QList<QJsonObject> parameterReadRanges(QList<ParameterAddress>);
}
