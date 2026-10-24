#pragma once
#include "debugger/sdk.h"
class QCanBusDevice;
// 外部 Qt 后端工厂；测试可注入受控 QCanBusDevice，仍经过正式转换与会话入口。
using CanDeviceFactory = std::function<QCanBusDevice *(const QJsonObject &, QString *)>;
std::unique_ptr<debugger::Endpoint> createCanEndpoint(const QJsonObject &, debugger::EndpointEvents, CanDeviceFactory = {});
