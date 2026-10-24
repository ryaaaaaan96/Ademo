#include "upp_request.h"
#include "upp_pdu.h"
#include <QJsonArray>
#include <QRegularExpression>
namespace upp {
namespace {
int number(const QJsonObject &o,const char *key,int low,int high) {
    auto v=o.value(key); if(!v.isDouble()) return -1;
    double n=v.toDouble(); return n>=low && n<=high && n==int(n) ? int(n) : -1;
}
void u16(QByteArray &b,int n) { b.append(char(n>>8)); b.append(char(n)); }
bool appendUnit(QByteArray &b,const QJsonObject &o,bool writing,int &replyBytes) {
    int model=number(o,"model",1,65534), offset=number(o,"offset",1,65534), length=0; QByteArray data;
    if(writing) {
        if(!o.value("dataHex").isString()) return false;
        QString hex=o.value("dataHex").toString(); hex.remove(QRegularExpression("\\s+"));
        if(hex.isEmpty() || hex.size()>498 || hex.size()%2 || !QRegularExpression("^[0-9a-fA-F]+$").match(hex).hasMatch()) return false;
        data=QByteArray::fromHex(hex.toLatin1()); length=data.size();
    } else length=number(o,"length",1,249);
    if(model<0 || offset<0 || length<1 || offset+length-1>65534) return false;
    u16(b,model); u16(b,offset); u16(b,length); b+=data; replyBytes+=6+length; return b.size()<=249;
}
bool appendUnits(QByteArray &b,const QJsonValue &v,bool writing,int &replyBytes) {
    if(!v.isArray()) return false;
    auto units=v.toArray(); if(units.isEmpty() || units.size()>35) return false;
    b.append(char(units.size())); ++replyBytes;
    for(const auto &unit:units) if(!unit.isObject() || !appendUnit(b,unit.toObject(),writing,replyBytes)) return false;
    return true;
}
bool sameUnits(const QJsonArray &wanted,const QJsonArray &actual) {
    if(wanted.size()!=actual.size()) return false;
    for(int i=0;i<wanted.size();++i) for(const auto *key:{"model","offset","length"})
        if(wanted[i].toObject().value(key)!=actual[i].toObject().value(key)) return false;
    return true;
}
}
Request encodeRequest(const QJsonObject &o) {
    Request r; r.device=number(o,"deviceId",1,255); r.function=number(o,"function",0x20,0x28);
    r.error=QStringLiteral("UPP设备/功能/单元参数无效或报文超过256字节");
    if(r.device<0 || !supportedFunction(r.function)) return r;
    r.writing=r.function>=0x26; int reply=7, ignored=0; bool ok=true;
    switch(r.function) {
    case 0x20: break;
    case 0x21: ok=appendUnit(r.pdu,o,false,ignored); reply+=o.value("length").toInt(); break;
    case 0x22: ok=appendUnits(r.pdu,o.value("units"),false,reply); break;
    case 0x23: { int type=number(o,"incrementType",0,1), sub=number(o,"subsequence",0,65535); ok=type>=0 && sub>=0; r.pdu.append(char(type)); u16(r.pdu,sub); break; }
    case 0x26: ok=appendUnit(r.pdu,o,true,ignored); break;
    case 0x27: ok=appendUnits(r.pdu,o.value("units"),true,ignored); break;
    case 0x28: ok=appendUnits(r.pdu,o.value("readUnits"),false,reply) && appendUnits(r.pdu,o.value("writeUnits"),true,ignored); break;
    }
    const auto parsed=parsePdu(r.function,r.pdu,false);
    if(!ok || r.pdu.size()>249 || reply>256 || !parsed.valid) return r;
    r.fields=parsed.fields; r.error.clear(); return r;
}
QByteArray encodeAdu(int device,int sequence,int function,const QByteArray &pdu) {
    QByteArray b; b.append(char(device)); b.append(char(0x50)); b.append(char(pdu.size()+7)); b.append(char(sequence)); b.append(char(function)); b+=pdu;
    quint16 crc=0xffff; for(auto c:b) { crc^=quint8(c); for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xa001:0); }
    b.append(char(crc)); b.append(char(crc>>8)); return b;
}
bool responseMatches(const Request &r,const QJsonObject &p,bool exception) {
    if(exception) {
        if(!p.contains("model")) return true;
        // Handshake and incremental query carry no concrete target range.
        if(r.function==0x20 || r.function==0x23) return true;
        QJsonArray units;
        if(r.function==0x21 || r.function==0x26) units.append(r.fields);
        else if(r.function==0x28) {units=r.fields.value("readUnits").toArray();for(const auto &u:r.fields.value("writeUnits").toArray()) units.append(u);}
        else units=r.fields.value("units").toArray();
        for(const auto &value:units) {
            const auto u=value.toObject();const int offset=p.value("offset").toInt(),start=u.value("offset").toInt();
            if(p.value("model")==u.value("model") && offset>=start && offset<start+u.value("length").toInt()) return true;
        }
        return false;
    }
    switch(r.function) {
    case 0x21: return p.value("dataHex").toString().size()==r.fields.value("length").toInt()*2;
    case 0x22: return sameUnits(r.fields.value("units").toArray(),p.value("units").toArray());
    case 0x28: return sameUnits(r.fields.value("readUnits").toArray(),p.value("readUnits").toArray());
    case 0x23: return p.value("incrementType")==r.fields.value("incrementType") && p.value("subsequence")==r.fields.value("subsequence");
    default: return true;
    }
}
}
