#include "write_pdu.h"
#include <QJsonArray>

namespace debugger {
namespace {
void word(QByteArray &bytes, int value) {
    bytes.append(char(value >> 8));
    bytes.append(char(value));
}
bool integer(const QJsonValue &value, int maximum) {
    return value.isDouble() && value.toInt(-1) >= 0 && value.toInt(-1) <= maximum;
}
}
bool isWriteAction(const QString &action) {
    return action == "write-single-coil" || action == "write-single-register"
        || action == "write-multiple-coils" || action == "write-multiple-registers";
}
QString encodeWritePdu(const QString &action, const QJsonObject &parameters, WritePdu &output) {
    WritePdu result;
    if (action == "write-single-coil") result.function = 5;
    else if (action == "write-single-register") result.function = 6;
    else if (action == "write-multiple-coils") result.function = 15;
    else if (action == "write-multiple-registers") result.function = 16;
    else return QStringLiteral("不支持的写入操作");
    if (!integer(parameters.value("start"), 65535)) return QStringLiteral("写入 PDU 起始地址必须为 0..65535 整数");
    result.start = parameters.value("start").toInt();
    result.pdu.append(char(result.function));
    word(result.pdu, result.start);
    const bool coils = result.function == 5 || result.function == 15;
    if (result.function == 5 || result.function == 6) {
        const auto value = parameters.value("value");
        if (coils ? !value.isBool() : !integer(value, 65535))
            return QStringLiteral("单线圈值必须为布尔值；单寄存器值必须为 0..65535 整数");
        result.count = 1;
        word(result.pdu, coils ? (value.toBool() ? 0xff00 : 0) : value.toInt());
        result.acknowledgement = result.pdu;
    } else {
        if (!parameters.value("values").isArray()) return QStringLiteral("多个值须使用 values 数组");
        const auto values = parameters.value("values").toArray();
        result.count = int(values.size());
        if (result.count < 1 || result.count > (coils ? 1968 : 123) || result.start + result.count > 65536)
            return QStringLiteral("多线圈数量为 1..1968，多寄存器数量为 1..123，PDU 范围不得越界");
        for (const auto &value : values) if (coils ? !value.isBool() : !integer(value, 65535))
            return QStringLiteral("线圈数组仅接受布尔值；寄存器数组仅接受 0..65535 整数");
        word(result.pdu, result.count);
        result.acknowledgement = result.pdu;
        result.pdu.append(char(coils ? (result.count + 7) / 8 : result.count * 2));
        if (coils) {
            for (int i = 0; i < result.count; i += 8) {
                int byte = 0;
                for (int bit = 0; bit < 8 && i + bit < result.count; ++bit)
                    if (values[i + bit].toBool()) byte |= 1 << bit;
                result.pdu.append(char(byte));
            }
        } else for (const auto &value : values) word(result.pdu, value.toInt());
    }
    output = std::move(result);
    return {};
}
int decodeWritePdu(const QByteArray &pdu, WritePdu &output, QJsonArray &values)
{
    if (pdu.isEmpty()) return 3;
    const int function = quint8(pdu[0]);
    const bool single = function == 5 || function == 6, coils = function == 5 || function == 15;
    const QString action = function == 5 ? "write-single-coil" : function == 6 ? "write-single-register"
        : function == 15 ? "write-multiple-coils" : function == 16 ? "write-multiple-registers" : QString();
    if (action.isEmpty()) return 1;
    if (pdu.size() < (single ? 5 : 6) || (single && pdu.size() != 5)) return 3;
    const auto readWord = [&](int offset) { return (quint8(pdu[offset]) << 8) | quint8(pdu[offset + 1]); };
    const int start = readWord(1), count = single ? 1 : readWord(3);
    QJsonArray decoded;
    QJsonObject parameters{{"start", start}};
    if (single) {
        const int value = readWord(3);
        if (coils && value != 0 && value != 0xff00) return 3;
        const QJsonValue typed = coils ? QJsonValue(value != 0) : QJsonValue(value);
        decoded.append(typed); parameters["value"] = typed;
    } else {
        if (count < 1 || count > (coils ? 1968 : 123)) return 3;
        const int byteCount = coils ? (count + 7) / 8 : 2 * count;
        if (quint8(pdu[5]) != byteCount || pdu.size() != 6 + byteCount) return 3;
        if (coils && count % 8 && (quint8(pdu.back()) >> (count % 8)) != 0) return 3;
        for (int i = 0; i < count; ++i)
            decoded.append(coils ? QJsonValue((quint8(pdu[6 + i / 8]) & (1 << (i % 8))) != 0)
                                 : QJsonValue(readWord(6 + 2 * i)));
        parameters["values"] = decoded;
    }
    if (start + count > 65536) return 2;
    WritePdu checked;
    if (!encodeWritePdu(action, parameters, checked).isEmpty()) return 3;
    output = std::move(checked); values = std::move(decoded);
    return 0;
}
}
