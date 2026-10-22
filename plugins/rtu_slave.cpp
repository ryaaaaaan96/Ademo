#include "rtu_slave.h"
#include "write_pdu.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <utility>

namespace debugger {
namespace {
const QStringList areaNames{"coils", "discrete", "holding", "input"};
bool integer(const QJsonValue &value, int minimum, int maximum) {
    return value.isDouble() && value.toInt(minimum - 1) >= minimum && value.toInt(minimum - 1) <= maximum;
}
quint16 crc16(const QByteArray &bytes) {
    quint16 crc = 0xffff;
    for (auto byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
int word(const QByteArray &bytes, int offset) { return (quint8(bytes[offset]) << 8) | quint8(bytes[offset + 1]); }
void appendWord(QByteArray &bytes, int value) { bytes.append(char(value >> 8)); bytes.append(char(value)); }
int updateDevice(SlaveDevice &device, const WritePdu &write, const QJsonArray &written)
{
    const auto name = write.function == 5 || write.function == 15 ? "coils" : "holding";
    auto areas = device.document.value("areas").toObject();
    auto area = areas.value(name).toObject();
    auto values = area.value("values").toArray();
    const int offset = write.start - area.value("start").toInt();
    if (offset < 0 || offset + write.count > values.size()) return 2;
    // 先在值快照上完成全部修改，再一次替换目标设备文档。
    for (int i = 0; i < write.count; ++i) values[offset + i] = written[i];
    area["values"] = values; areas[name] = area;
    auto next = device.document; next["areas"] = areas;
    device.document = std::move(next);
    device.dirty = true;
    ++device.revision;
    return 0;
}
}
QString validateSlaveDocument(const QJsonObject &document)
{
    if (!integer(document.value("version"), 1, 1)) return QStringLiteral("version 必须为整数 1");
    if (!document.value("areas").isObject()) return QStringLiteral("缺少 areas 对象");
    const auto areas = document.value("areas").toObject();
    for (int index = 0; index < areaNames.size(); ++index) {
        const auto name = areaNames[index];
        if (!areas.value(name).isObject()) return QStringLiteral("缺少数据区 %1").arg(name);
        const auto area = areas.value(name).toObject();
        if (!integer(area.value("start"), 0, 65535) || !area.value("values").isArray())
            return QStringLiteral("%1 需有效 start（0..65535）和 values 数组").arg(name);
        const auto values = area.value("values").toArray();
        if (values.size() > 65536 || area.value("start").toInt() + values.size() > 65536)
            return QStringLiteral("%1 的 PDU 地址范围越界").arg(name);
        for (const auto &value : values)
            if (index < 2 ? !value.isBool() : !integer(value, 0, 65535))
                return QStringLiteral("%1 值类型无效，位区仅接受布尔值，寄存器仅接受 0..65535 整数").arg(name);
    }
    return {};
}
QString RtuSlave::prepare(const SessionConfig &config)
{
    const int baud = config.endpoint.value("baud").toString().toInt();
    if (baud <= 0) return QStringLiteral("无效波特率");
    const bool noParity = config.endpoint.value("parity") == "none";
    if (config.endpoint.value("stopBits").toString() != (noParity ? "2" : "1"))
        return QStringLiteral("RTU 使用 11 位字符：无校验需 2 个停止位，有校验需 1 个停止位");
    t15_ = baud > 19200 ? 750 : (16500000LL + baud - 1) / baud;
    t35_ = baud > 19200 ? 1750 : (38500000LL + baud - 1) / baud;
    const auto bindings = config.protocol.value("devices").toArray();
    if (bindings.isEmpty() || bindings.size() > 32) return QStringLiteral("从站需配置 1..32 个设备及外部 JSON 文件");
    QMap<int, SlaveDevice> loaded;
    QSet<QString> paths;
    qint64 totalBytes = 0;
    for (const auto &binding : bindings) {
        if (!binding.isObject()) return QStringLiteral("设备绑定必须为对象");
        const auto object = binding.toObject();
        const int unit = object.value("unit").toInt(-1);
        const auto path = object.value("path").toString();
        const auto prefix = QStringLiteral("设备 %1，文件 %2：").arg(unit).arg(path);
        if (!integer(object.value("unit"), 1, 247) || loaded.contains(unit)) return prefix + QStringLiteral("Unit 必须为唯一的 1..247 整数");
        if (path.isEmpty() || !QDir::isAbsolutePath(path)) return prefix + QStringLiteral("启动文件需非空绝对路径；工作区相对引用应先解析");
        QFileInfo info(path);
        if (!info.exists() || !info.isFile()) return prefix + QStringLiteral("文件不存在或不是普通文件");
        const auto canonical = info.canonicalFilePath().toCaseFolded();
        if (paths.contains(canonical)) return prefix + QStringLiteral("每个设备必须绑定独立文件");
        paths.insert(canonical);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return prefix + file.errorString();
        if (file.size() > 4 * 1024 * 1024) return prefix + QStringLiteral("文件超过 4 MiB 上限");
        const auto bytes = file.read(4 * 1024 * 1024 + 1);
        if (file.error() != QFileDevice::NoError || !file.atEnd()) return prefix + QStringLiteral("读取失败或文件超过 4 MiB 上限");
        totalBytes += bytes.size();
        if (totalBytes > 16 * 1024 * 1024) return prefix + QStringLiteral("会话文件总量超过 16 MiB 上限");
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) return prefix + QStringLiteral("JSON 无效：") + parseError.errorString();
        const auto error = validateSlaveDocument(document.object());
        if (!error.isEmpty()) return prefix + error;
        loaded.insert(unit, {unit, info.absoluteFilePath(), document.object(), false});
        loaded[unit].savedDocument = document.object();
    }
    devices_ = std::move(loaded);
    configuration_ = config.protocol;
    return {};
}
QJsonObject RtuSlave::snapshot() const
{
    QJsonArray devices;
    for (const auto &device : devices_)
        devices.append(QJsonObject{{"unit", device.unit}, {"path", device.path}, {"data", device.document}, {"dirty", device.dirty}, {"revision", QString::number(device.revision)}});
    return {{"role", "slave"}, {"devices", devices}};
}
void RtuSlave::activate() { active_ = true; lastActivity_ = services_.nowUs(); }
void RtuSlave::stop() { active_ = false; buffer_.clear(); sources_.clear(); sourceRanges_.clear(); response_.clear(); pendingSend_ = 0; responseSources_.clear(); sentSources_.clear(); }
void RtuSlave::onInput(const TraceRecord &record)
{
    if (!active_ || record.bytes.isEmpty()) return;
    if (!buffer_.isEmpty() && record.idleBeforeUs) {
        if (*record.idleBeforeUs >= t35_) finishFrame();
        else if (*record.idleBeforeUs > t15_) broken_ = true;
    }
    lastInput_ = record.monotonicUs;
    lastActivity_ = qMax(lastActivity_, lastInput_);
    if (sources_.size() < 256) { sources_.append(record.id); sourceRanges_.append({record.id, 0, int(record.bytes.size())}); }
    if (buffer_.size() + record.bytes.size() > 256) oversized_ = true;
    buffer_.append(record.bytes.left(qMax(0, 256 - int(buffer_.size()))));
}
void RtuSlave::finishFrame()
{
    if (buffer_.isEmpty()) return;
    Message message;
    message.sources = sources_;
    message.sourceRanges = sourceRanges_;
    message.status = "invalid";
    if (broken_ || oversized_ || buffer_.size() < 4 || crc16(buffer_) != 0)
        message.detail = QStringLiteral("请求帧 CRC、长度或可信 t1.5 间隙无效，不应答");
    else {
        const int unit = quint8(buffer_[0]), function = quint8(buffer_[1]);
        message.fields = {{"unit", unit}, {"function", function}};
        message.status = "unmatched";
        message.detail = QStringLiteral("未知设备或广播读取，不应答");
        const bool writing = function == 5 || function == 6 || function == 15 || function == 16;
        if ((unit && devices_.contains(unit)) || (!unit && writing)) {
            int exception = 0;
            QByteArray response;
            const bool busy = !response_.isEmpty() || pendingSend_;
            if (busy) {
                message.status = "rejected";
                message.detail = QStringLiteral("上次从站应答尚未完成，本请求未执行");
            } else if (writing) {
                WritePdu write;
                QJsonArray values;
                exception = decodeWritePdu(buffer_.mid(1, buffer_.size() - 3), write, values);
                if (!unit) {
                    QJsonArray updated, rejected;
                    if (!exception) for (auto &device : devices_) {
                        if (updateDevice(device, write, values)) rejected.append(device.unit);
                        else updated.append(device.unit);
                    }
                    message.status = exception ? "invalid" : "broadcast-written";
                    message.detail = QStringLiteral("广播设备写入不应答；各设备独立校验并完整提交，文件不会自动保存");
                    message.fields["updatedUnits"] = updated; message.fields["rejectedUnits"] = rejected;
                    if (exception) message.fields["exceptionCode"] = exception;
                } else {
                    if (!exception) exception = updateDevice(devices_[unit], write, values);
                    if (!exception) {
                        response.append(char(unit)); response.append(write.acknowledgement);
                        message.fields["start"] = write.start; message.fields["count"] = write.count;
                        message.fields["revision"] = QString::number(devices_[unit].revision);
                    }
                }
            } else if (function < 1 || function > 4) exception = 1;
            else if (buffer_.size() != 8) exception = 3;
            else {
                const int start = word(buffer_, 2), count = word(buffer_, 4);
                if (count < 1 || count > (function <= 2 ? 2000 : 125) || start + count > 65536) exception = 3;
                else {
                    const auto area = devices_[unit].document.value("areas").toObject().value(areaNames[function - 1]).toObject();
                    const auto values = area.value("values").toArray();
                    const int offset = start - area.value("start").toInt();
                    if (offset < 0 || offset + count > values.size()) exception = 2;
                    else {
                        response.append(char(unit)); response.append(char(function));
                        response.append(char(function <= 2 ? (count + 7) / 8 : 2 * count));
                        if (function <= 2) for (int i = 0; i < count; i += 8) {
                            int byte = 0;
                            for (int bit = 0; bit < 8 && i + bit < count; ++bit)
                                if (values[offset + i + bit].toBool()) byte |= 1 << bit;
                            response.append(char(byte));
                        } else for (int i = 0; i < count; ++i) appendWord(response, values[offset + i].toInt());
                        message.fields["start"] = start; message.fields["count"] = count;
                    }
                }
            }
            if (unit && !busy) {
                if (exception) { response.append(char(unit)); response.append(char(function | 0x80)); response.append(char(exception)); message.fields["exceptionCode"] = exception; }
                const auto crc = crc16(response); response.append(char(crc)); response.append(char(crc >> 8));
                response_ = response;
                responseSources_ = sourceRanges_;
                message.status = exception ? "exception" : "response-ready";
                message.detail = writing && !exception ? QStringLiteral("设备写入已完整生效，未保存到文件；应答等待统一通道发送")
                    : QStringLiteral("请求已验证，应答等待帧间静默及统一通道发送");
            }
        }
    }
    services_.publishMessage(std::move(message));
    buffer_.clear(); sources_.clear(); sourceRanges_.clear(); broken_ = oversized_ = false;
}
void RtuSlave::onSent(quint64 id, bool ok, const QString &reason, qint64 observedUs)
{
    if (pendingSend_ != id) return;
    pendingSend_ = 0;
    lastActivity_ = qMax(lastActivity_, observedUs);
    Message message;
    message.sourceRanges = std::exchange(sentSources_, {});
    for (const auto &source : message.sourceRanges) message.sources.append(source.recordId);
    message.fields["localSendId"] = QString::number(id);
    message.status = ok ? "response-sent" : "response-send-failed";
    message.detail = ok ? QStringLiteral("从站应答本地发送完成；不代表主站已接收") : QStringLiteral("从站应答本地发送失败，不自动重发：") + reason;
    services_.publishMessage(std::move(message));
}
void RtuSlave::tick()
{
    if (!active_) return;
    const auto now = services_.nowUs();
    bool complete = buffer_.size() >= 8;
    if (buffer_.size() >= 2 && (quint8(buffer_[1]) == 15 || quint8(buffer_[1]) == 16))
        complete = oversized_ || (buffer_.size() >= 7 && buffer_.size() >= 9 + quint8(buffer_[6]));
    if (!buffer_.isEmpty() && now - lastInput_ >= t35_
        && (complete || (buffer_.size() >= 4 && quint8(buffer_[1]) != 15 && quint8(buffer_[1]) != 16
            && (quint8(buffer_[1]) < 1 || quint8(buffer_[1]) > 6)))) finishFrame();
    if (!response_.isEmpty() && !pendingSend_ && now - lastActivity_ >= t35_) {
        const auto bytes = std::exchange(response_, {});
        sentSources_ = std::exchange(responseSources_, {});
        pendingSend_ = services_.sendPassive ? services_.sendPassive(bytes) : 0;
        if (!pendingSend_) onSent(0, false, QStringLiteral("统一通道拒绝被动应答"), now);
    }
}
}
