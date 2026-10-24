#include "upp_pdu.h"
#include <QJsonArray>
namespace upp {
namespace {
class Reader {
    const QByteArray &bytes_; int position_ = 0; bool ok_ = true;
public:
    explicit Reader(const QByteArray &bytes) : bytes_(bytes) {}
    int remaining() const { return int(bytes_.size()) - position_; }
    QByteArray take(int size) { if (size < 0 || size > remaining()) { ok_ = false; return {}; } const auto out = bytes_.mid(position_, size); position_ += size; return out; }
    int u8() { const auto b = take(1); return b.isEmpty() ? 0 : static_cast<unsigned char>(b[0]); }
    int u16() { const int high = u8(); return (high << 8) | u8(); }
    bool done() const { return ok_ && remaining() == 0; }
    bool ok() const { return ok_; }
    void reject() { ok_ = false; }
};
QJsonObject unit(Reader &r, bool withData) {
    const int model = r.u16(), offset = r.u16(), length = r.u16();
    if (model < 1 || model > 65534 || offset < 1 || offset > 65534 || length < 1) r.reject();
    QJsonObject out{{"model", model}, {"offset", offset}, {"length", length}};
    if (withData) out["dataHex"] = QString::fromLatin1(r.take(length).toHex());
    return out;
}
QJsonArray units(Reader &r, int count, bool data) {
    QJsonArray out;
    if (count > r.remaining() / 6) { r.reject(); return out; }
    for (int i = 0; i < count && r.ok(); ++i) out.append(unit(r, data));
    return out;
}
}
bool supportedFunction(int f) { return QList<int>{0x20,0x21,0x22,0x23,0x26,0x27,0x28}.contains(f & 0x7f) && f <= 0xff; }
ParsedPdu parseSoe(const QByteArray &bytes) {
    ParsedPdu out; Reader r(bytes);
    const int value = r.u8(), mask = r.u8(); QJsonArray changes;
    for (int bit = 0; bit < 8; ++bit) if (mask & (1 << bit)) {
        const auto raw = r.take(7); if (raw.size() != 7) return out;
        auto b = [&](int index) { return static_cast<unsigned char>(raw[index]); };
        const int milliseconds = b(0) | b(1) << 8, minute = b(2) & 0x3f, hour = b(3) & 0x1f;
        const int day = b(4) & 0x1f, month = b(5) & 0x0f, year = b(6) & 0x7f;
        changes.append(QJsonObject{{"bit", bit}, {"value", bool(value & (1 << bit))}, {"rawHex", QString::fromLatin1(raw.toHex())},
            {"millisecondsWithinMinute", milliseconds}, {"minute", minute}, {"hour", hour}, {"day", day},
            {"dayOfWeek", b(4) >> 5}, {"month", month}, {"yearWithinCentury", year}, {"invalidFlag", bool(b(2) & 0x80)},
            {"summerTime", bool(b(3) & 0x80)}, {"clockFieldsInRange", milliseconds < 60000 && minute < 60 && hour < 24 && day >= 1 && day <= 31 && month >= 1 && month <= 12 && year <= 99}});
    }
    out.valid = r.done(); out.fields = {{"value", value}, {"changeMask", mask}, {"changes", changes}, {"timezone", "unspecified"}}; return out;
}
ParsedPdu parsePdu(int function, const QByteArray &bytes, bool response) {
    ParsedPdu out; Reader r(bytes); auto &fields = out.fields;
    if (!supportedFunction(function)) return out;
    if (function & 0x80) {
        if (!response || (bytes.size() != 2 && bytes.size() != 6)) return out;
        fields["exceptionCode"] = r.u16();
        if (bytes.size() == 6) { fields["model"] = r.u16(); fields["offset"] = r.u16(); }
        out.valid = r.done(); return out;
    }
    switch (function) {
    case 0x20:
        if (response) { if (bytes.size() != 8) return out; fields["protocolNumberHex"] = QString::fromLatin1(r.take(4).toHex()); fields["reservedHex"] = QString::fromLatin1(r.take(4).toHex()); }
        break;
    case 0x21:
        if (response) fields["dataHex"] = QString::fromLatin1(r.take(r.remaining()).toHex());
        else fields = unit(r, false);
        break;
    case 0x22: {
        const int count = r.u8(); if (!count) return out;
        fields["unitCount"] = count; fields["units"] = units(r, count, response); break;
    }
    case 0x23:
        if (!response) {
            const int type = r.u8(); if (type > 1) return out;
            fields["incrementType"] = type; fields["subsequence"] = r.u16();
        } else {
            const int timeType = r.u8(), type = r.u8(), countAndMore = r.u8();
            if ((timeType != 0 && timeType != 0xa1) || type > 1) return out;
            fields["timestampType"] = timeType; fields["incrementType"] = type;
            fields["more"] = bool(countAndMore & 0x80); fields["unitCount"] = countAndMore & 0x7f; fields["subsequence"] = r.u16();
            if (!(countAndMore & 0x7f) && (countAndMore & 0x80)) return out;
            fields["units"] = units(r, countAndMore & 0x7f, true);
            if (timeType) fields["timeEncoding"] = "CP56Time2a; model-type-required";
        }
        break;
    case 0x26:
        if (!response) fields = unit(r, true);
        break;
    case 0x27:
        if (!response) { const int count = r.u8(); if (!count) return out; fields["unitCount"] = count; fields["units"] = units(r, count, true); }
        break;
    case 0x28: {
        const int reads = r.u8(); if (!reads) return out;
        fields["readUnits"] = units(r, reads, response); fields["readCount"] = reads;
        if (!response) { const int writes = r.u8(); if (!writes) return out; fields["writeUnits"] = units(r, writes, true); fields["writeCount"] = writes; }
        break;
    }
    }
    out.valid = r.done(); return out;
}
}
