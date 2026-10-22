#include "debugger/host.h"

namespace debugger {
bool DebugSession::configureRecovery(QJsonObject policy)
{
    if ((policy.contains("autoReconnect") && !policy.value("autoReconnect").isBool())
        || (policy.contains("restorePolling") && !policy.value("restorePolling").isBool())
        || (policy.contains("intervalMs") && (policy.value("intervalMs").toInt(-1) < 10
                                             || policy.value("intervalMs").toInt(-1) > 60000))) {
        diagnostic_ = QStringLiteral("自动重连/恢复轮询必须为布尔值，重连间隔必须为 10..60000 ms 整数");
        return false;
    }
    config_.recovery = std::move(policy);
    reconnectDueUs_ = recoverable_ && config_.recovery.value("autoReconnect").toBool()
        ? nowUs() + qint64(config_.recovery.value("intervalMs").toInt(1000)) * 1000 : -1;
    if (reconnectDueUs_ >= 0 && !clock_) pollTimer_.start();
    if (state_ == SessionState::Error && reconnectDueUs_ < 0) pollTimer_.stop();
    if (!notificationTimer_.isActive()) notificationTimer_.start();
    return true;
}
void DebugSession::connectionFailed(const QString &reason)
{
    // 连接代次失效，但运行数据/后台文件任务仍归属于同一协议实例。
    ++generation_;
    reconnectDueUs_ = -1;
    for (auto &operation : operations_) if (!operation.finished && !operation.localOnly) {
        operation.finished = true;
        operation.update = {operation.request.id, "transport-error", reason
            + (operation.sendAccepted ? QStringLiteral("；已提交的操作未获确认，设备可能已执行；不会自动补发") : QString()),
            {{"uncertain", operation.sendAccepted}}};
    }
    if (protocol_ && !preparing_) protocol_->connectionLost(reason);
    sendAttempts_.clear();
    passiveSends_.clear();
    passiveDeadlines_.clear();
    if (endpoint_) endpoint_->close();
    endpoint_.reset();
    if (!key_.isEmpty()) catalog_->release(key_);
    key_.clear();
    state_ = SessionState::Error;
    recoverable_ = protocol_ && !preparing_;
    diagnostic_ = reason;
    appendTrace("connection-lost", {}, 0, reason);
    if (recoverable_ && config_.recovery.value("autoReconnect").toBool()) {
        reconnectDueUs_ = nowUs() + qint64(config_.recovery.value("intervalMs").toInt(1000)) * 1000;
        diagnostic_ += QStringLiteral("；将按间隔自动重连，仅恢复配置的轮询");
        if (!clock_) pollTimer_.start();
    } else pollTimer_.stop();
    emit changed();
}
bool DebugSession::reconnect()
{
    if (!canReconnect()) return false;
    ++generation_;
    reconnectDueUs_ = -1;
    appendTrace("reconnect-request", {}, 0, QStringLiteral("重建连接，保留当前运行数据，不重新读取启动文件"));
    return openConnection();
}
}
