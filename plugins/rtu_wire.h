#pragma once
#include <QByteArray>

namespace debugger::rtuwire {
inline quint16 crc16(const QByteArray &bytes)
{
    quint16 crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}

struct FrameTiming {
    qint64 interCharacterUs;
    qint64 interFrameUs;
};
// 调用方先验证正波特率及11位字符配置；低速按1.5/3.5字符时间向上取整。
inline FrameTiming frameTiming(int baud)
{
    return {baud > 19200 ? 750 : (16500000LL + baud - 1) / baud,
            baud > 19200 ? 1750 : (38500000LL + baud - 1) / baud};
}
}
