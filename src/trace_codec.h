#pragma once
#include "debugger/sdk.h"
namespace debugger::tracecodec {
QJsonObject encode(const TraceRecord &);
QJsonObject encode(const Message &);
QJsonObject encode(const OperationSnapshot &);
TraceRecord decodeTrace(const QJsonObject &);
Message decodeMessage(const QJsonObject &);
OperationSnapshot decodeOperation(const QJsonObject &);
}
