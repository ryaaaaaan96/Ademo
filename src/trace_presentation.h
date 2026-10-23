#pragma once
#include "debugger/sdk.h"

namespace debugger {
inline QString operationSummary(const OperationSnapshot &operation)
{
    return QStringLiteral("操作 #%1 %2；已提交传输：%3；本地发送完成：%4；%5结果：%6（%7）；%8\n")
        .arg(operation.request.id).arg(operation.request.action,
            operation.sendAccepted ? QStringLiteral("是") : QStringLiteral("否"),
            operation.localSent ? QStringLiteral("是") : QStringLiteral("否"),
            operation.localOnly ? QStringLiteral("本地操作") : QStringLiteral("协议"), operation.update.state,
            operation.finished ? QStringLiteral("已结束") : QStringLiteral("进行中"), operation.update.detail);
}
}
