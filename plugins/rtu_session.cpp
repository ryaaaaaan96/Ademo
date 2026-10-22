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
    return {};
}
void RtuSession::activate()
{
    active_ = true;
    lastActivityUs_ = services_.nowUs(); // 包括启动时的 t3.5 静默。
}
void RtuSession::stop()
{
    active_ = false;
    if (request_.id) finish("cancelled", QStringLiteral("会话停止，读取已取消"));
    buffer_.clear();
    sources_.clear();
}
void RtuSession::submit(const OperationRequest &request)
{
    const int unit = request.parameters.value("unit").toInt(-1);
    const int start = request.parameters.value("start").toInt(-1);
    const int count = request.parameters.value("count").toInt(-1);
    QString error;
    if (!active_) error = QStringLiteral("会话尚未启动");
    else if (request_.id) error = QStringLiteral("总线已有在途读取，请等待结束");
    else if (uncertain_) error = QStringLiteral("上次交互未确认，无法可靠区分迟到响应；请停止并重启会话");
    else if (request.action != "read-holding") error = QStringLiteral("当前仅支持读取保持寄存器（03）");
    else if (unit < 1 || unit > 247) error = QStringLiteral("从站地址必须为 1..247，不支持广播读取");
    else if (start < 0 || start > 65535 || count < 1 || count > 125 || start + count > 65536)
        error = QStringLiteral("PDU 起始地址为 0..65535，数量为 1..125，范围不得越界");
    if (!error.isEmpty()) { services_.publish({request.id, "invalid", error, {}}); return; }
    request_ = request;
    unit_ = unit;
    start_ = start;
    count_ = count;
    phase_ = Phase::WaitingIdle;
    deadlineUs_ = services_.nowUs() + qMax(timeoutUs_, 1000000LL);
    services_.publish({request.id, "queued", QStringLiteral("读取已接受，等待 RTU 帧间静默"), {}});
}
void RtuSession::finish(const QString &state, const QString &detail, QJsonObject result)
{
    const auto id = request_.id;
    request_ = {};
    phase_ = Phase::Idle;
    services_.publish({id, state, detail, std::move(result)});
}
void RtuSession::onInput(const TraceRecord &record)
{
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
    if (sources_.size() < 256) sources_.append(record.id);
    if (buffer_.size() + record.bytes.size() > 256) oversizedFrame_ = true;
    buffer_.append(record.bytes.left(qMax(0, 256 - int(buffer_.size()))));
}
void RtuSession::finishFrame()
{
    if (!haveInput_) return;
    Message message;
    bool published = false;
    message.sources = sources_;
    message.status = "invalid";
    if (oversizedFrame_) message.detail = QStringLiteral("RTU 帧超过 256 字节");
    else if (brokenFrame_) message.detail = QStringLiteral("帧内间隙超过 t1.5，丢弃不完整 RTU 帧");
    else if (buffer_.size() < 5) message.detail = QStringLiteral("RTU 响应长度不足");
    else if (crc16(buffer_) != 0) message.detail = QStringLiteral("CRC 校验失败");
    else {
        const auto unit = quint8(buffer_[0]);
        const auto function = quint8(buffer_[1]);
        message.fields = {{"unit", unit}, {"function", function}};
        if (!request_.id || phase_ != Phase::AwaitingResponse || unit != unit_
            || (function != 3 && function != 0x83)) {
            message.status = "unmatched";
            message.detail = QStringLiteral("未确认归属的响应：地址/功能码不匹配或当前没有等待应答的读取");
        } else if (lastInputUs_ > deadlineUs_) {
            message.status = "late";
            message.detail = QStringLiteral("响应晚于本次应答期限，不作为设备确认");
        } else if (function == 0x83 && buffer_.size() == 5 && quint8(buffer_[2]) != 0) {
            message.operationId = request_.id;
            message.status = "exception";
            const int code = quint8(buffer_[2]);
            message.fields["exceptionCode"] = code;
            message.detail = QStringLiteral("Modbus 异常响应，异常码 0x%1").arg(code, 2, 16, QChar('0'));
            services_.publishMessage(message);
            finish("exception", message.detail, {{"exceptionCode", code}});
            published = true;
        } else if (function == 3 && buffer_.size() == 5 + 2 * count_ && quint8(buffer_[2]) == 2 * count_) {
            QJsonArray values;
            for (int i = 0; i < count_; ++i)
                values.append((quint8(buffer_[3 + 2 * i]) << 8) | quint8(buffer_[4 + 2 * i]));
            message.operationId = request_.id;
            message.status = "success";
            message.detail = QStringLiteral("已验证功能码 03 的设备应答");
            message.fields["values"] = values;
            services_.publishMessage(message);
            finish("success", message.detail, {{"unit", unit_}, {"start", start_}, {"values", values}});
            published = true;
        } else message.detail = QStringLiteral("响应长度、字节计数或异常格式不符合当前读取");
    }
    if (!published) services_.publishMessage(message);
    buffer_.clear();
    sources_.clear();
    brokenFrame_ = oversizedFrame_ = haveInput_ = false;
}
bool RtuSession::candidateComplete() const
{
    if (oversizedFrame_) return true;
    if (buffer_.size() < 5) return false;
    const auto function = quint8(buffer_[1]);
    if (function == 3 || function == 4) return buffer_.size() >= 5 + quint8(buffer_[2]);
    return true;
}
void RtuSession::onSent(quint64 id, bool ok, const QString &reason, qint64 observedUs)
{
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
    if (!active_) return;
    const auto now = services_.nowUs();
    // 观测块时间无法证明物理字节间隙：没有可信静默元数据时保留不完整候选帧。
    if (haveInput_ && phase_ != Phase::Sending && now - lastInputUs_ >= interFrameUs_
        && (phase_ == Phase::WaitingIdle || phase_ == Phase::Idle || trustedTiming_ || candidateComplete())) finishFrame();
    if (phase_ == Phase::WaitingIdle && now >= deadlineUs_) {
        finish("timeout", QStringLiteral("等待总线空闲超时，本次读取未发送"));
        return;
    }
    if (phase_ == Phase::WaitingIdle && !haveInput_ && now - lastActivityUs_ >= interFrameUs_) {
        QByteArray request;
        request.append(char(unit_));
        request.append(char(3));
        appendWord(request, start_);
        appendWord(request, count_);
        const auto crc = crc16(request);
        request.append(char(crc & 0xff));
        request.append(char(crc >> 8));
        phase_ = Phase::Sending;
        deadlineUs_ = now + timeoutUs_;
        if (services_.send(request_.id, request))
            services_.publish({request_.id, "submitted", QStringLiteral("通道已接受发送，等待本地发送完成"), {}});
        else finish("transport-error", QStringLiteral("统一通道拒绝发送"));
    }
    if (request_.id && (phase_ == Phase::Sending || phase_ == Phase::AwaitingResponse) && now >= deadlineUs_) {
        // 期限内最后一块已到达时，最多再等一个 t3.5 完成边界判定。
        if (phase_ == Phase::AwaitingResponse && haveInput_ && lastInputUs_ <= deadlineUs_
            && now < lastInputUs_ + interFrameUs_) return;
        if (haveInput_) finishFrame();
        if (!request_.id) return;
        uncertain_ = true;
        if (phase_ == Phase::Sending) finish("transport-error", QStringLiteral("本地发送超时，未获得后端完成确认"));
        else finish("timeout", QStringLiteral("等待设备应答超时；请停止并重启会话后重新读取"));
    }
}
}
