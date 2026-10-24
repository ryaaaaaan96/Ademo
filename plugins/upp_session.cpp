#include "upp_session.h"
#include "upp_pdu.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
using namespace debugger;
namespace {
constexpr auto Profile = "upp-can-provisional-2026-10-24";
constexpr int MaximumAdu = 256;
constexpr int MaximumContexts = 256;
int byte(char c) { return static_cast<unsigned char>(c); }
quint16 crc16(const QByteArray &bytes) {
    quint16 crc = 0xffff;
    for (auto value : bytes) { crc ^= byte(value); for (int bit = 0; bit != 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0); }
    return crc;
}
struct Pending {
    QByteArray bytes;
    QList<TraceRange> sources;
    int length = 0;
    quint32 canId = 0;
    bool extended = false;
    quint64 generation = 0;
    qint64 firstUs = 0, lastUs = 0;
    QString origin;
};
struct RequestContext { QJsonObject pdu; QList<quint64> sources; qint64 expiresUs = 0; bool ambiguous = false; };
class UppSession final : public ProtocolSession {
    ProtocolServices services_;
    Pending pending_;
    QString direction_ = "auto";
    QSet<quint32> requestIds_, responseIds_;
    QSet<int> soeModels_;
    qint64 fragmentTimeoutUs_ = 100000, packetTimeoutUs_ = 1000000;
    QMap<QString, RequestContext> requests_;
    quint64 generation_ = 0;
    bool active_ = false;
    qint64 now() const { return services_.nowUs(); }
    static QString requestKey(int device, int sequence, int function) { return QString::number(device) + ':' + QString::number(sequence) + ':' + QString::number(function); }
    void publish(const Pending &p, const QString &status, const QString &detail, QJsonObject fields = {}) {
        Message m; m.status = status; m.detail = detail; m.fields = std::move(fields);
        m.fields["ruleProfile"] = Profile; m.fields["provisional"] = true;
        m.fields["aduHex"] = QString::fromLatin1(p.bytes.toHex()); m.fields["sourceKind"] = p.origin;
        m.sourceRanges = p.sources;
        for (const auto &range : p.sources) m.sources.append(range.recordId);
        services_.publishMessage(std::move(m));
    }
    void discard(const QString &status, const QString &detail) {
        if (!pending_.sources.isEmpty()) publish(pending_, status, detail);
        pending_ = {};
        requests_.clear();
    }
    void prune() {
        for (auto it = requests_.begin(); it != requests_.end();) {
            if (now() >= it->expiresUs) it = requests_.erase(it); else ++it;
        }
    }
    void complete() {
        const auto p = std::exchange(pending_, {});
        const auto &b = p.bytes;
        const int device = byte(b[0]), sequence = byte(b[3]), function = byte(b[4]);
        QJsonObject fields{{"deviceId", device}, {"sequence", sequence}, {"function", function}, {"length", p.length}};
        const auto expectedCrc = crc16(b.left(b.size() - 2));
        fields["crcReceived"] = byte(b[b.size()-2]) | byte(b.back()) << 8;
        fields["crcCalculated"] = expectedCrc;
        if (expectedCrc != fields["crcReceived"].toInt()) { publish(p, "crc-error", QStringLiteral("完整报文CRC错误"), fields); return; }
        fields["crcValid"] = true;
        if (!upp::supportedFunction(function)) { publish(p, "unsupported-function", QStringLiteral("功能不在本轮CAN范围"), fields); return; }
        const auto payload = b.mid(5, b.size() - 7);
        auto request = upp::parsePdu(function, payload, false);
        auto response = upp::parsePdu(function, payload, true);
        QString direction = direction_;
        if (requestIds_.contains(p.canId)) direction = "request";
        if (responseIds_.contains(p.canId)) direction = "response";
        const auto key = requestKey(device, sequence, function & 0x7f);
        prune();
        if (direction == "auto") {
            if (request.valid != response.valid) direction = request.valid ? "request" : "response";
            else if (request.valid && response.valid) {
                if (requests_.contains(key)) requests_[key].ambiguous = true;
                fields["direction"] = "ambiguous"; publish(p, "ambiguous-direction", QStringLiteral("请求/响应结构均合法，请配置报文方向"), fields); return;
            }
        }
        fields["direction"] = direction;
        auto parsed = direction == "request" ? request : response;
        if (!parsed.valid) { fields["pduHex"] = QString::fromLatin1(payload.toHex()); publish(p, "pdu-error", parsed.error, fields); return; }
        if (function == 0x23 && direction == "response" && parsed.fields.value("timestampType").toInt() == 0xa1) {
            auto units = parsed.fields.value("units").toArray();
            for (int i = 0; i < units.size(); ++i) {
                auto unit = units[i].toObject();
                if (!soeModels_.contains(unit.value("model").toInt())) continue;
                const auto soe = upp::parseSoe(QByteArray::fromHex(unit.value("dataHex").toString().toLatin1()));
                if (!soe.valid) { fields["pdu"] = parsed.fields; publish(p, "pdu-error", QStringLiteral("已指定SOE模型的数据长度与变化位不匹配"), fields); return; }
                unit["soe"] = soe.fields; units[i] = unit;
            }
            parsed.fields["units"] = units;
        }
        fields["pdu"] = parsed.fields;
        if (direction == "request") {
            if (!requests_.contains(key) && requests_.size() >= MaximumContexts) requests_.erase(requests_.begin());
            RequestContext context; context.pdu = parsed.fields; context.expiresUs = now() + packetTimeoutUs_;
            context.ambiguous = requests_.contains(key);
            for (const auto &range : p.sources) context.sources.append(range.recordId);
            requests_[key] = context;
        } else {
            fields["requestContext"] = "unavailable";
            auto found = requests_.find(key);
            if (found != requests_.end()) {
                const auto context = found.value(); requests_.erase(found);
                if (context.ambiguous) fields["requestContext"] = "ambiguous";
                else {
                    fields["requestContext"] = "matched";
                    QJsonArray refs; for (auto ref : context.sources) refs.append(QString::number(ref)); fields["requestSources"] = refs;
                    fields["requestPdu"] = context.pdu;
                    if (function == 0x21 && payload.size() != context.pdu.value("length").toInt()) {
                        publish(p, "pdu-error", QStringLiteral("读取响应数据长度与已观察请求不符"), fields); return;
                    }
                }
            }
        }
        publish(p, function & 0x80 ? "protocol-exception" : "decoded", QStringLiteral("按暂定UPP规则解析；未验证真实设备"), fields);
    }
public:
    explicit UppSession(ProtocolServices s) : services_(std::move(s)) {}
    QString prepare(const SessionConfig &config) override {
        const auto options = config.protocol;
        direction_ = options.value("direction").toString("auto");
        if (!QStringList{"auto", "request", "response"}.contains(direction_)) return QStringLiteral("UPP报文方向无效");
        auto timeout = [&](const char *key, int fallback, qint64 &value) {
            bool ok = true; const auto v = options.value(key);
            const int ms = v.isUndefined() ? fallback : v.isString() ? v.toString().toInt(&ok) : v.toInt(-1);
            if (!ok || ms < 1 || ms > 60000) return false;
            value = qint64(ms) * 1000; return true;
        };
        if (!timeout("fragmentTimeoutMs", 100, fragmentTimeoutUs_) || !timeout("packetTimeoutMs", 1000, packetTimeoutUs_)) return QStringLiteral("UPP超时必须为1..60000毫秒");
        auto ids = [](const QString &text, QSet<quint32> &out) {
            if (text.trimmed().isEmpty()) return true;
            for (const auto &part : text.split(',')) { bool ok; const auto id = part.trimmed().toUInt(&ok, 16); if (!ok || id > 0x1fffffff) return false; out.insert(id); }
            return true;
        };
        if (!ids(options.value("requestCanIds").toString(), requestIds_) || !ids(options.value("responseCanIds").toString(), responseIds_)) return QStringLiteral("CAN ID方向列表必须为十六进制，逗号分隔");
        for (auto id : requestIds_) if (responseIds_.contains(id)) return QStringLiteral("请求与响应CAN ID列表不能重叠");
        const auto models = options.value("soeModels").toString();
        if (!models.trimmed().isEmpty()) for (const auto &part : models.split(',')) { bool ok; const int model = part.trimmed().toInt(&ok); if (!ok || model < 1 || model > 65534) return QStringLiteral("SOE模型号必须为1..65534的十进制列表"); soeModels_.insert(model); }
        return {};
    }
    QJsonObject snapshot() const override { return {{"ruleProfile", Profile}, {"pendingBytes", pending_.bytes.size()}, {"requestContexts", requests_.size()}}; }
    void activate() override { active_ = true; }
    void stop() override { discard("incomplete", QStringLiteral("会话停止，未完整报文已丢弃")); requests_.clear(); active_ = false; }
    void connectionLost(const QString &) override { discard("incomplete", QStringLiteral("连接中断，未完整报文已丢弃")); requests_.clear(); generation_ = 0; }
    void connectionRestored(bool) override {}
    void tick() override {
        if (!active_) return;
        if (!pending_.sources.isEmpty() && (now() - pending_.lastUs >= fragmentTimeoutUs_ || now() - pending_.firstUs >= packetTimeoutUs_)) discard("reassembly-timeout", QStringLiteral("UPP分片或整包接收超时"));
        prune();
    }
    void onSent(quint64, bool, const QString &, qint64) override {}
    void submit(const OperationRequest &r) override { services_.publish({r.id, "rejected", QStringLiteral("UPP仅接收，不发送业务报文"), {}}); }
    void onInput(const TraceRecord &r) override {
        if (!active_ || r.kind != "rx") return;
        tick();
        if (generation_ && generation_ != r.generation) { discard("incomplete", QStringLiteral("连接代次变化")); requests_.clear(); }
        generation_ = r.generation;
        Pending single; single.bytes = r.bytes; single.sources = {{r.id, 0, int(r.bytes.size())}}; single.origin = r.metadata.value("sourceKind").toString("unknown");
        if (r.metadata.value("frameType").toString() != "data" || !r.metadata.value("classicSupported").toBool() || !r.metadata.value("canIdAvailable").toBool()) {
            publish(single, "ignored-frame", QStringLiteral("非经典CAN数据帧，不参与UPP重组")); return;
        }
        const auto id = quint32(r.metadata.value("canId").toInt()); const bool extended = r.metadata.value("extended").toBool();
        if (!pending_.sources.isEmpty() && (pending_.canId != id || pending_.extended != extended)) discard("interleaved-frame", QStringLiteral("分片CAN ID或格式变化，旧报文中止"));
        if (pending_.sources.isEmpty()) {
            if (r.bytes.size() < 5 || byte(r.bytes[1]) != 0x50) { publish(single, "invalid-header", QStringLiteral("当前CAN帧不包含合法UPP固定块")); return; }
            int length = byte(r.bytes[2]); if (!length) length = MaximumAdu;
            if (length < 7) { publish(single, "length-error", QStringLiteral("UPP完整长度小于7")); return; }
            pending_.length = length; pending_.canId = id; pending_.extended = extended; pending_.generation = r.generation;
            pending_.firstUs = now(); pending_.origin = single.origin;
        }
        const auto expected = qMin(8, pending_.length - int(pending_.bytes.size()));
        pending_.sources.append(single.sources.front()); pending_.lastUs = now();
        if (r.bytes.size() != expected) { pending_.bytes += r.bytes.left(MaximumAdu - pending_.bytes.size()); discard("length-error", QStringLiteral("CAN分片长度不符合无填充暂定规则")); return; }
        pending_.bytes += r.bytes;
        if (pending_.bytes.size() == pending_.length) complete();
    }
};
}
std::unique_ptr<ProtocolSession> createUppSession(ProtocolServices s) { return std::make_unique<UppSession>(std::move(s)); }
