#include "rtu_session.h"
#include <QJsonArray>

namespace debugger {
namespace {
quint16 crc16(const QByteArray &bytes)
{
    quint16 crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
void appendWord(QByteArray &bytes, int word)
{
    bytes.append(char(word >> 8));
    bytes.append(char(word & 0xff));
}
}
RtuSession::RtuSession(ProtocolServices services) : services_(std::move(services)) {}
QString RtuSession::prepare(const SessionConfig &config)
{
    if (config.role == "slave") {
        slave_ = std::make_unique<RtuSlave>(services_);
        return slave_->prepare(config);
    }
    if (config.role != "master") return QStringLiteral("当前仅支持 RTU 主站会话");
    const bool noParity = config.endpoint.value("parity") == "none";
    if (config.endpoint.value("stopBits").toString() != (noParity ? "2" : "1"))
        return QStringLiteral("RTU 使用 11 位字符：无校验需 2 个停止位，有校验需 1 个停止位");
    const int baud = config.endpoint.value("baud").toString().toInt();
    if (baud <= 0) return QStringLiteral("无效波特率");
    interCharacterUs_ = baud > 19200 ? 750 : (16500000LL + baud - 1) / baud;
    interFrameUs_ = baud > 19200 ? 1750 : (38500000LL + baud - 1) / baud;
    const int timeout = config.protocol.contains("responseTimeoutMs")
        ? config.protocol.value("responseTimeoutMs").toInt(-1) : 1000;
    if (timeout < 1 || timeout > 60000) return QStringLiteral("应答超时必须为 1..60000 ms");
    timeoutUs_ = qint64(timeout) * 1000;
    return configureSettings(config.protocol);
}
QString RtuSession::configureSettings(const QJsonObject &settings)
{
    const int retries = settings.value("extraReadRetries").toInt(2);
    const int timeout = settings.value("responseTimeoutMs").toInt(1000);
    const int interval = settings.value("retryIntervalMs").toInt(100);
    if ((settings.contains("extraReadRetries") && settings.value("extraReadRetries").toInt(-1) < 0)
        || retries > 10 || timeout < 1 || timeout > 60000 || interval < 0 || interval > 60000
        || (settings.contains("retryIntervalMs") && settings.value("retryIntervalMs").toInt(-1) < 0)
        || (settings.contains("responseTimeoutMs") && settings.value("responseTimeoutMs").toInt(-1) < 1))
        return QStringLiteral("额外读取重试必须为 0..10，应答超时为 1..60000 ms，重试间隔为 0..60000 ms");
    if (settings.contains("polls") && !settings.value("polls").isArray()) return QStringLiteral("轮询配置必须是数组");
    const auto tasks = settings.value("polls").toArray();
    if (tasks.size() > 64) return QStringLiteral("一个会话最多配置 64 个轮询任务");
    const QStringList actions{"read-coils", "read-discrete", "read-holding", "read-input"};
    QList<PollTask> polls;
    for (const auto &value : tasks) {
        const auto task = value.toObject();
        const auto action = task.value("action").toString();
        const int function = actions.indexOf(action) + 1;
        const int unit = task.value("unit").toInt(-1), start = task.value("start").toInt(-1);
        const int count = task.value("count").toInt(-1), cycle = task.value("intervalMs").toInt(-1);
        if (!function || unit < 1 || unit > 247 || start < 0 || start > 65535 || count < 1
            || count > (function <= 2 ? 2000 : 125) || start + count > 65536 || cycle < 10 || cycle > 3600000)
            return QStringLiteral("轮询只能读取四区：设备/地址/数量须有效，周期为 10..3600000 ms；不允许设备写入");
        polls.append({action, {{"unit", unit}, {"start", start}, {"count", count}}, qint64(cycle) * 1000});
    }
    extraRetries_ = retries;
    timeoutUs_ = qint64(timeout) * 1000;
    retryIntervalUs_ = qint64(interval) * 1000;
    polls_ = std::move(polls);
    nextPoll_ = 0;
    pollingPaused_ = false;
    return {};
}
void RtuSession::activate()
{
    if (slave_) { slave_->activate(); return; }
    active_ = true;
    lastActivityUs_ = services_.nowUs(); // 包括启动时的 t3.5 静默。
}
void RtuSession::stop()
{
    if (slave_) { slave_->stop(); return; }
    active_ = false;
    for (const auto &request : pending_) services_.publish({request.id, "cancelled", QStringLiteral("会话停止，排队操作已取消"), {}});
    pending_.clear();
    polls_.clear();
    pollOperations_.clear();
    if (request_.id) finish("cancelled", QStringLiteral("会话停止，操作已取消"));
    buffer_.clear();
    sources_.clear();
    sourceRanges_.clear();
}
void RtuSession::connectionLost(const QString &reason)
{
    if (slave_) { slave_->stop(); return; } // 只清连接IO，模拟设备与后台文件任务留在原实例。
    active_ = false;
    if (request_.id && !write_.function) {
        if (uncertainReads_.size() >= 64 && !uncertainReads_.contains(responseShape())) allReadsUncertain_ = true;
        else uncertainReads_[responseShape()] = request_.id;
    }
    if (request_.id) finish("transport-error", reason);
    for (const auto &request : pending_) services_.publish({request.id, "transport-error", reason, {}});
    pending_.clear();
    pollOperations_.clear();
    buffer_.clear(); sources_.clear(); sourceRanges_.clear();
    brokenFrame_ = oversizedFrame_ = haveInput_ = false;
    phase_ = Phase::Idle;
}
void RtuSession::connectionRestored(bool resumePolling)
{
    if (slave_) { slave_->activate(); return; }
    active_ = true;
    uncertain_ = false;
    pollingPaused_ = !resumePolling;
    lastActivityUs_ = services_.nowUs();
    for (auto &task : polls_) task.dueUs = lastActivityUs_;
}
void RtuSession::submit(const OperationRequest &request)
{
    if (slave_) { slave_->submit(request); return; }
    if (request.action == "configure-polling") {
        const auto error = !active_ ? QStringLiteral("会话尚未启动") : request_.id || !pending_.isEmpty()
            ? QStringLiteral("请等待在途与排队操作结束后调整会话配置") : configureSettings(request.parameters);
        services_.publish({request.id, error.isEmpty() ? "success" : "invalid",
            error.isEmpty() ? QStringLiteral("已更新会话读取重试和轮询配置") : error, request.parameters});
        if (error.isEmpty() && services_.publishConfiguration) services_.publishConfiguration(request.parameters);
        return;
    }
    const int unit = request.parameters.value("unit").toInt(-1);
    const int start = request.parameters.value("start").toInt(-1);
    const int count = request.parameters.value("count").toInt(-1);
    const QStringList actions{"read-coils", "read-discrete", "read-holding", "read-input"};
    const QStringList areas{"coils", "discrete", "holding", "input"};
    const int function = actions.indexOf(request.action) + 1;
    const int maximum = function <= 2 ? 2000 : 125;
    const bool writing = isWriteAction(request.action);
    WritePdu write;
    QString error;
    if (!active_) error = QStringLiteral("会话尚未启动");
    else if (uncertain_) error = QStringLiteral("上次交互未确认，无法可靠区分迟到响应；请停止并重启会话");
    else if (!function && !writing) error = QStringLiteral("不支持的 Modbus 操作");
    else if (unit < 1 || unit > 247) error = QStringLiteral("从站地址必须为 1..247；当前交互需要设备应答，不支持广播");
    else if (writing) error = encodeWritePdu(request.action, request.parameters, write);
    else if (start < 0 || start > 65535 || count < 1 || count > maximum || start + count > 65536)
        error = QStringLiteral("PDU 起始地址为 0..65535，数量为 1..%1，范围不得越界").arg(maximum);
    if (!error.isEmpty()) { services_.publish({request.id, "invalid", error, {}}); return; }
    if (request_.id || phase_ == Phase::Quarantine || !pending_.isEmpty()) {
        if (pending_.size() >= 64) {
            services_.publish({request.id, "invalid", QStringLiteral("总线排队已满（64 项），本次操作未发送"), {}});
        } else {
            pending_.append(request);
            services_.publish({request.id, "queued", QStringLiteral("已进入统一总线队列，等待前一操作完成"), {}});
        }
        return;
    }
    begin(request);
}
void RtuSession::begin(const OperationRequest &request)
{
    const QStringList actions{"read-coils", "read-discrete", "read-holding", "read-input"};
    const QStringList areas{"coils", "discrete", "holding", "input"};
    const int function = actions.indexOf(request.action) + 1;
    WritePdu write;
    if (isWriteAction(request.action)) encodeWritePdu(request.action, request.parameters, write);
    request_ = request;
    unit_ = request.parameters.value("unit").toInt();
    start_ = request.parameters.value("start").toInt();
    count_ = request.parameters.value("count").toInt();
    function_ = function;
    area_ = function ? areas[function - 1] : QString{};
    write_ = std::move(write);
    writeSubmitted_ = false;
    attempt_ = 1;
    freshnessUncertain_ = !write_.function && (allReadsUncertain_ || uncertainReads_.contains(responseShape()));
    phase_ = Phase::WaitingIdle;
    deadlineUs_ = services_.nowUs() + qMax(timeoutUs_, 1000000LL);
    services_.publish({request.id, "queued", QStringLiteral("操作已接受，等待 RTU 帧间静默；写入不会自动重试"), {}});
}
QString RtuSession::responseShape() const
{
    return QStringLiteral("%1/%2/%3").arg(unit_).arg(function_).arg(count_);
}
void RtuSession::finish(const QString &state, const QString &detail, QJsonObject result)
{
    const auto id = request_.id;
    const auto poll = pollOperations_.contains(id) ? pollOperations_.take(id) : qsizetype(-1);
    result["attempts"] = attempt_;
    if (!write_.function && state != "success" && state != "exception") {
        auto fields = result;
        fields["unit"] = unit_;
        fields["area"] = area_;
        fields["start"] = start_;
        fields["state"] = state;
        fields["poll"] = poll >= 0;
        fields["operationResult"] = true;
        Message message;
        message.operationId = id;
        message.status = state;
        message.detail = detail;
        message.fields = fields;
        services_.publishMessage(message);
    }
    if (poll >= 0 && poll < polls_.size()) polls_[poll].dueUs = services_.nowUs() + polls_[poll].intervalUs;
    request_ = {};
    phase_ = Phase::Idle;
    const bool unconfirmedWrite = write_.function && writeSubmitted_ && state != "success" && state != "exception";
    if (unconfirmedWrite) result["uncertain"] = true;
    services_.publish({id, state, unconfirmedWrite ? detail + QStringLiteral("；设备写入结果不确定，不自动重试，请核对设备后决定是否再次执行") : detail, std::move(result)});
}
void RtuSession::readTimeout()
{
    Message diagnostic;
    diagnostic.operationId = request_.id;
    diagnostic.status = "attempt-timeout";
    diagnostic.detail = QStringLiteral("读取尝试 %1 超时；迟到响应不能证明属于后续尝试").arg(attempt_);
    diagnostic.fields = {{"attempt", attempt_}, {"unit", unit_}};
    services_.publishMessage(diagnostic);
    if (attempt_ <= extraRetries_) {
        ++attempt_;
        phase_ = Phase::RetryWait;
        retryReadyUs_ = services_.nowUs() + retryIntervalUs_;
        deadlineUs_ = retryReadyUs_ + qMax(timeoutUs_, 1000000LL);
        services_.publish({request_.id, "retry-wait", QStringLiteral("等待读取重试间隔；下一次为尝试 %1/%2")
            .arg(attempt_).arg(extraRetries_ + 1), {{"attempt", attempt_}, {"extraReadRetries", extraRetries_}}});
    } else {
        if (uncertainReads_.size() >= 64 && !uncertainReads_.contains(responseShape())) allReadsUncertain_ = true;
        else uncertainReads_[responseShape()] = request_.id;
        finish("timeout", QStringLiteral("所有读取尝试均未获得有效应答，进入有界隔离等待后继续总线调度"));
        phase_ = Phase::Quarantine;
        retryReadyUs_ = services_.nowUs() + timeoutUs_ + retryIntervalUs_;
    }
}
void RtuSession::onInput(const TraceRecord &record)
{
    if (slave_) { slave_->onInput(record); return; }
    if (record.bytes.isEmpty()) return;
    if (haveInput_ && record.idleBeforeUs) {
        const auto gap = *record.idleBeforeUs;
        if (gap >= interFrameUs_) finishFrame();
        else if (gap > interCharacterUs_) brokenFrame_ = true;
    }
    trustedTiming_ = haveInput_ ? trustedTiming_ && record.idleBeforeUs.has_value()
                               : record.idleBeforeUs.has_value();
    haveInput_ = true;
    lastInputUs_ = record.monotonicUs;
    lastActivityUs_ = qMax(lastActivityUs_, lastInputUs_);
    if (sources_.size() < 256) {
        sources_.append(record.id);
        sourceRanges_.append({record.id, 0, int(record.bytes.size())});
    }
    if (buffer_.size() + record.bytes.size() > 256) oversizedFrame_ = true;
    buffer_.append(record.bytes.left(qMax(0, 256 - int(buffer_.size()))));
}
void RtuSession::finishFrame()
{
    if (!haveInput_) return;
    Message message;
    bool published = false;
    message.sources = sources_;
    message.sourceRanges = sourceRanges_;
    message.status = "invalid";
    if (oversizedFrame_) message.detail = QStringLiteral("RTU 帧超过 256 字节");
    else if (brokenFrame_) message.detail = QStringLiteral("帧内间隙超过 t1.5，丢弃不完整 RTU 帧");
    else if (buffer_.size() < 5) message.detail = QStringLiteral("RTU 响应长度不足");
    else if (crc16(buffer_) != 0) message.detail = QStringLiteral("CRC 校验失败");
    else {
        const auto unit = quint8(buffer_[0]);
        const auto function = quint8(buffer_[1]);
        const int expected = write_.function ? write_.function : function_;
        message.fields = {{"unit", unit}, {"function", function}};
        if (!request_.id || phase_ != Phase::AwaitingResponse || unit != unit_
            || (function != expected && function != (expected | 0x80))) {
            message.status = phase_ == Phase::RetryWait || phase_ == Phase::Quarantine ? "late" : "unmatched";
            message.detail = QStringLiteral("未确认归属的响应：地址/功能码不匹配或当前没有等待应答的操作；不归入新的尝试");
        } else if (lastInputUs_ > deadlineUs_) {
            message.status = "late";
            message.detail = QStringLiteral("响应晚于本次应答期限，不作为设备确认");
        } else if (function == (expected | 0x80) && buffer_.size() == 5 && quint8(buffer_[2]) != 0) {
            message.operationId = !write_.function && freshnessUncertain_ ? 0 : request_.id;
            message.status = "exception";
            const int code = quint8(buffer_[2]);
            message.fields["exceptionCode"] = code;
            message.fields["freshnessUncertain"] = !write_.function && freshnessUncertain_;
            if (!write_.function && freshnessUncertain_) message.fields["candidateOperationId"] = QString::number(request_.id);
            message.fields["operationResult"] = true;
            message.fields["state"] = "exception";
            message.fields["area"] = area_;
            message.fields["start"] = start_;
            message.detail = QStringLiteral("Modbus 异常响应，异常码 0x%1").arg(code, 2, 16, QChar('0'));
            if (!write_.function && freshnessUncertain_) message.detail += QStringLiteral("；先前同形读取曾超时，不能证明属于当前操作");
            services_.publishMessage(message);
            finish("exception", message.detail, {{"exceptionCode", code}, {"freshnessUncertain", !write_.function && freshnessUncertain_}});
            published = true;
        } else if (write_.function && function == write_.function && buffer_.size() == 8
                   && buffer_.mid(1, 5) == write_.acknowledgement) {
            message.operationId = request_.id;
            message.status = "success";
            message.detail = QStringLiteral("设备已确认写入，功能码 0x%1").arg(function, 2, 16, QChar('0'));
            message.fields["start"] = write_.start;
            message.fields["count"] = write_.count;
            services_.publishMessage(message);
            finish("success", message.detail, {{"unit", unit_}, {"start", write_.start},
                   {"count", write_.count}, {"function", function}, {"uncertain", false}});
            published = true;
        } else if (!write_.function && function_ <= 2 && function == function_ && count_ % 8
                   && buffer_.size() == 5 + (count_ + 7) / 8
                   && (quint8(buffer_[buffer_.size() - 3]) & (0xff << (count_ % 8)))) {
            message.detail = QStringLiteral("位响应最后字节的未用高位必须为 0");
        } else if (!write_.function && function == function_ && buffer_.size() == 5 + (function_ <= 2 ? (count_ + 7) / 8 : 2 * count_)
                   && quint8(buffer_[2]) == (function_ <= 2 ? (count_ + 7) / 8 : 2 * count_)) {
            QJsonArray values;
            for (int i = 0; i < count_; ++i) {
                if (function_ <= 2) values.append(bool(quint8(buffer_[3 + i / 8]) & (1 << (i % 8))));
                else values.append((quint8(buffer_[3 + 2 * i]) << 8) | quint8(buffer_[4 + 2 * i]));
            }
            message.operationId = freshnessUncertain_ ? 0 : request_.id;
            message.status = "success";
            message.detail = QStringLiteral("已验证功能码 %1 的设备应答").arg(function_, 2, 16, QChar('0'));
            message.fields["values"] = values;
            message.fields["area"] = area_;
            message.fields["start"] = start_;
            message.fields["operationResult"] = true;
            message.fields["state"] = "success";
            message.fields["attempt"] = attempt_ == 1 ? 1 : 0;
            message.fields["freshnessUncertain"] = freshnessUncertain_;
            if (freshnessUncertain_) message.fields["candidateOperationId"] = QString::number(request_.id);
            if (freshnessUncertain_) {
                message.fields["possiblePreviousOperation"] = QString::number(uncertainReads_.value(responseShape()));
                message.detail += QStringLiteral("；先前同形读取曾超时，有限隔离不能证明当前归属或新采样，候选值仅供核对");
            }
            if (attempt_ > 1) {
                QJsonArray possible;
                for (int i = 1; i <= attempt_; ++i) possible.append(i);
                message.fields["possibleAttempts"] = possible;
                message.detail += QStringLiteral("；RTU 无事务号，不能区分此操作的多次尝试来源");
            }
            services_.publishMessage(message);
            finish("success", message.detail, {{"unit", unit_}, {"start", start_}, {"area", area_},
                {"function", function_}, {"values", values}, {"attemptAttributionUncertain", attempt_ > 1},
                {"freshnessUncertain", freshnessUncertain_}});
            published = true;
        } else message.detail = QStringLiteral("响应长度、字节计数或异常格式不符合当前操作");
    }
    if (!published) services_.publishMessage(message);
    buffer_.clear();
    sources_.clear();
    sourceRanges_.clear();
    brokenFrame_ = oversizedFrame_ = haveInput_ = false;
}
bool RtuSession::candidateComplete() const
{
    if (oversizedFrame_) return true;
    if (buffer_.size() < 5) return false;
    const auto function = quint8(buffer_[1]);
    if (function >= 1 && function <= 4) return buffer_.size() >= 5 + quint8(buffer_[2]);
    if (function == 5 || function == 6 || function == 15 || function == 16) return buffer_.size() >= 8;
    return true;
}
void RtuSession::onSent(quint64 id, bool ok, const QString &reason, qint64 observedUs)
{
    if (slave_) { slave_->onSent(id, ok, reason, observedUs); return; }
    if (request_.id != id || phase_ != Phase::Sending) return;
    lastActivityUs_ = qMax(lastActivityUs_, observedUs);
    if (observedUs >= deadlineUs_) {
        uncertain_ = true;
        finish("transport-error", QStringLiteral("本地发送完成晚于发送期限，不延长本次交互"));
        return;
    }
    if (!ok) {
        uncertain_ = true;
        finish("transport-error", QStringLiteral("本地发送失败：") + reason);
        return;
    }
    phase_ = Phase::AwaitingResponse;
    deadlineUs_ = observedUs + timeoutUs_;
    services_.publish({id, "sent", QStringLiteral("本地发送完成，尚未收到设备应答"), {}});
}
void RtuSession::tick()
{
    if (slave_) { slave_->tick(); return; }
    if (!active_) return;
    const auto now = services_.nowUs();
    // 观测块时间无法证明物理字节间隙：没有可信静默元数据时保留不完整候选帧。
    if (haveInput_ && phase_ != Phase::Sending && now - lastInputUs_ >= interFrameUs_
        && (phase_ == Phase::WaitingIdle || phase_ == Phase::Idle || phase_ == Phase::RetryWait
            || phase_ == Phase::Quarantine || trustedTiming_ || candidateComplete())) finishFrame();
    if (phase_ == Phase::Quarantine && now >= retryReadyUs_) phase_ = Phase::Idle;
    if (phase_ == Phase::Idle && !request_.id) {
        if (!pending_.isEmpty()) begin(pending_.takeFirst());
        else if (!pollingPaused_ && !polls_.isEmpty() && services_.submit) {
            for (qsizetype i = 0; i < polls_.size(); ++i) {
                const auto index = (nextPoll_ + i) % polls_.size();
                auto &task = polls_[index];
                if (now < task.dueUs) continue;
                auto parameters = task.parameters;
                task.dueUs = now + task.intervalUs;
                nextPoll_ = (index + 1) % polls_.size();
                const auto id = services_.submit(task.action, parameters);
                if (id && request_.id == id) pollOperations_[id] = index;
                break;
            }
        }
    }
    if (phase_ == Phase::RetryWait) {
        if (now >= deadlineUs_) {
            finish("timeout", QStringLiteral("等待重试总线空闲超时，未继续发送"));
            phase_ = Phase::Quarantine;
            retryReadyUs_ = now + timeoutUs_ + retryIntervalUs_;
        } else if (now >= retryReadyUs_ && !haveInput_ && now - lastActivityUs_ >= interFrameUs_) {
            phase_ = Phase::WaitingIdle;
            deadlineUs_ = now + qMax(timeoutUs_, 1000000LL);
        }
    }
    if (phase_ == Phase::WaitingIdle && now >= deadlineUs_) {
        finish("timeout", QStringLiteral("等待总线空闲超时，本次操作未发送"));
        return;
    }
    if (phase_ == Phase::WaitingIdle && !haveInput_ && now - lastActivityUs_ >= interFrameUs_) {
        QByteArray request;
        request.append(char(unit_));
        if (write_.function) request.append(write_.pdu);
        else {
            request.append(char(function_));
            appendWord(request, start_);
            appendWord(request, count_);
        }
        const auto crc = crc16(request);
        request.append(char(crc & 0xff));
        request.append(char(crc >> 8));
        phase_ = Phase::Sending;
        deadlineUs_ = now + timeoutUs_;
        if (services_.send(request_.id, request)) {
            writeSubmitted_ = write_.function != 0;
            services_.publish({request_.id, "submitted", QStringLiteral("通道已接受发送，等待本地发送完成（尝试 %1）").arg(attempt_),
                {{"attempt", attempt_}}});
        } else finish("transport-error", QStringLiteral("统一通道拒绝发送，本次操作未提交"));
    }
    if (request_.id && (phase_ == Phase::Sending || phase_ == Phase::AwaitingResponse) && now >= deadlineUs_) {
        // 期限内最后一块已到达时，最多再等一个 t3.5 完成边界判定。
        if (phase_ == Phase::AwaitingResponse && haveInput_ && lastInputUs_ <= deadlineUs_
            && now < lastInputUs_ + interFrameUs_) return;
        if (haveInput_) finishFrame();
        if (!request_.id) return;
        if (phase_ == Phase::AwaitingResponse && !write_.function) readTimeout();
        else {
            uncertain_ = true;
            finish(phase_ == Phase::Sending ? "transport-error" : "timeout", phase_ == Phase::Sending
                ? QStringLiteral("本地发送超时，未获得后端完成确认") : QStringLiteral("等待设备应答超时；请停止并重启会话后再次操作"));
        }
    }
}
}
