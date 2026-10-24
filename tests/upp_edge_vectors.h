#pragma once
#include <QMap>
#include <QByteArray>
static const QMap<QString, QByteArray> edgeFrames = {
    {"eight", QByteArray::fromHex("0550080121abca0d")},
    {"nine", QByteArray::fromHex("0550090121abcd7102")},
    {"maximum", QByteArray::fromHex("0550000121aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3433")},
    {"malformed", QByteArray::fromHex("05500e0122020002000600037cab")},
    {"unsupported", QByteArray::fromHex("0550070124494a")},
    {"ambiguous", QByteArray::fromHex("05500d012100020006000365f7")},
    {"wrongResponse", QByteArray::fromHex("055009012102038ec6")},
    {"rightResponse", QByteArray::fromHex("05500a01210203044794")},
    {"empty", QByteArray::fromHex("0550070126c88b")},
    {"shortException", QByteArray::fromHex("05500901a100024f8e")},
    {"badException", QByteArray::fromHex("05500801a1026bb3")},
    {"soe", QByteArray::fromHex("0550220123a101010000000200060010a503e8030203040518d0070607080919d4b4")},
};
