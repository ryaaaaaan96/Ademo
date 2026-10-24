#pragma once
#include <QJsonObject>
#include <QByteArray>
namespace upp {
struct ParsedPdu { bool valid = false; QJsonObject fields; QString error = QStringLiteral("PDU结构或长度不符"); };
bool supportedFunction(int);
ParsedPdu parsePdu(int function, const QByteArray &, bool response);
ParsedPdu parseSoe(const QByteArray &);
}
