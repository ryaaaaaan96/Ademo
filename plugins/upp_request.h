#pragma once
#include <QJsonObject>
#include <QByteArray>
namespace upp {
struct Request { int device=0, function=0; QByteArray pdu; QJsonObject fields; bool writing=false; QString error; };
Request encodeRequest(const QJsonObject &);
QByteArray encodeAdu(int device, int sequence, int function, const QByteArray &pdu);
bool responseMatches(const Request &, const QJsonObject &response, bool exception);
}
