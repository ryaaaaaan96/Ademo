#pragma once
#include "debugger/sdk.h"
namespace debugger {
struct ModbusTrendParameter { QString id, name, displayUnit, area; int unit, address; };
QWidget *openParameterTrends(const SessionAccess &, const QList<ModbusTrendParameter> &, QWidget *view);
}
