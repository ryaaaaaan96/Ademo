#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCanBus>
#include <QCanBusDevice>
#include <QCanBusFrame>
#include <QTimer>
#include <QTextStream>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
namespace {
int b(char c) { return quint8(c); }
int u16(const QByteArray &a,int p) { return (b(a[p])<<8)|b(a[p+1]); }
void append16(QByteArray &a,int value) { a.append(char(value>>8)); a.append(char(value)); }
quint16 crc(const QByteArray &a) { quint16 c=65535; for(char value:a) { c^=quint8(value); for(int bit=0;bit<8;++bit) c=(c&1)?(c>>1)^0xa001:c>>1; } return c; }
struct Unit { int model,offset,length; QByteArray data; };
bool units(const QByteArray &p,int &position,int count,bool write,QList<Unit> &out) {
    if(count<1 || count>35) return false;
    for(int i=0;i<count;++i) {
        if(position+6>p.size()) return false;
        Unit unit{u16(p,position),u16(p,position+2),u16(p,position+4),{}}; position+=6;
        if(unit.model<1 || unit.model>4 || unit.offset<1 || unit.length<1 || unit.offset+unit.length>65) return false;
        if(write) { if(position+unit.length>p.size()) return false; unit.data=p.mid(position,unit.length); position+=unit.length; }
        out.append(unit);
    }
    return true;
}
QByteArray makeReply(const QByteArray &adu,QMap<int,QByteArray> &models) {
    int f=b(adu[4]), position=0; auto p=adu.mid(5,adu.size()-7); QByteArray reply; QList<Unit> reads,writes; bool ok=true;
    auto readData=[&](const Unit &u){return models[u.model].mid(u.offset-1,u.length);};
    switch(f) {
    case 0x20: ok=p.isEmpty(); reply=QByteArray::fromHex("32304141ffffffff"); break;
    case 0x21: ok=units(p,position,1,false,reads); if(ok) reply=readData(reads[0]); break;
    case 0x22: if(p.isEmpty()) {ok=false;break;} position=1; ok=units(p,position,b(p[0]),false,reads); break;
    case 0x23: ok=p.size()==3 && b(p[0])<=1; if(ok) { reply.append(char(0)); reply.append(p[0]); reply.append(char(1)); reply+=p.mid(1,2); append16(reply,2); append16(reply,6); append16(reply,3); reply+=models[2].mid(5,3); position=p.size(); } break;
    case 0x26: ok=units(p,position,1,true,writes); break;
    case 0x27: if(p.isEmpty()) {ok=false;break;} position=1; ok=units(p,position,b(p[0]),true,writes); break;
    case 0x28:
        if(p.isEmpty()) {ok=false;break;} position=1; ok=units(p,position,b(p[0]),false,reads);
        if(!ok || position>=p.size()) {ok=false;break;}
        {int count=b(p[position++]); ok=units(p,position,count,true,writes);} break;
    default: ok=false;
    }
    ok=ok && position==p.size();
    if(ok && (f==0x22 || f==0x28)) {
        reply.append(char(reads.size()));
        for(const auto &u:reads) { append16(reply,u.model); append16(reply,u.offset); append16(reply,u.length); reply+=readData(u); }
    }
    if(reply.size()>249) ok=false;
    // 软件对端约定：整包校验后，先读取旧值再原子应用全部写入。
    if(ok) for(const auto &u:writes) models[u.model].replace(u.offset-1,u.length,u.data);
    else { f|=0x80; reply=QByteArray::fromHex("0002"); }
    QByteArray out; out.append(adu[0]); out.append(char(0x50)); out.append(char(reply.size()+7)); out.append(adu[3]); out.append(char(f)); out+=reply;
    auto c=crc(out); out.append(char(c)); out.append(char(c>>8)); return out;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QCommandLineParser parser;
    parser.addHelpOption(); parser.addOptions({{"interface","virtualcan channel","channel","can0"},{"device","UPP device 1..255","number","5"},
        {"rx-id","Request CAN ID hexadecimal","hex","5"},{"tx-id","Response CAN ID hexadecimal","hex","5"},{"extended","Use extended frames"},
        {"duration","Exit after milliseconds","ms","30000"}});
    if(!parser.parse(app.arguments())) { QTextStream(stderr)<<parser.errorText()<<Qt::endl; return 2; }
    if(parser.isSet("help")) { QTextStream(stdout)<<parser.helpText(); return 0; }
    bool ok1,ok2,ok3,ok4; int device=parser.value("device").toInt(&ok1), duration=parser.value("duration").toInt(&ok4);
    quint32 rx=parser.value("rx-id").toUInt(&ok2,16),tx=parser.value("tx-id").toUInt(&ok3,16); bool extended=parser.isSet("extended");
    if(!ok1||!ok2||!ok3||!ok4||device<1||device>255||duration<1||duration>3600000||rx>(extended?0x1fffffff:0x7ff)||tx>(extended?0x1fffffff:0x7ff)) return 2;
    auto channel=parser.value("interface"); if(channel.size()!=4 || !channel.startsWith("can") || channel[3]<'0' || channel[3]>'9') return 2;
    QString error; std::unique_ptr<QCanBusDevice> bus(QCanBus::instance()->createDevice("virtualcan",channel,&error));
    if(!bus) {QTextStream(stderr)<<error<<Qt::endl;return 3;}
    QMap<int,QByteArray> models; for(int m=1;m<=4;++m) { QByteArray bytes; for(int i=1;i<=64;++i) bytes.append(char(i)); models[m]=bytes; }
    QByteArray incoming,outgoing; int expected=0,offset=0; QTimer sending,receiveDeadline; sending.setInterval(20); receiveDeadline.setSingleShot(true); receiveDeadline.setInterval(100);
    QObject::connect(&receiveDeadline,&QTimer::timeout,[&]{incoming.clear();expected=0;});
    QObject::connect(&sending,&QTimer::timeout,[&]{
        if(offset>=outgoing.size()) {sending.stop();outgoing.clear();return;}
        QCanBusFrame frame(tx,outgoing.mid(offset,8)); frame.setExtendedFrameFormat(extended);
        if(!bus->writeFrame(frame)) {app.exit(4);return;} offset+=frame.payload().size();
        if(offset>=outgoing.size()) {sending.stop();outgoing.clear();}
    });
    QObject::connect(bus.get(),&QCanBusDevice::stateChanged,[&](auto state){if(state==QCanBusDevice::ConnectedState) QTextStream(stdout)<<"READY virtual-can-test"<<Qt::endl;});
    QObject::connect(bus.get(),&QCanBusDevice::framesReceived,[&]{
        while(bus->framesAvailable()) {
            auto frame=bus->readFrame(); auto bytes=frame.payload();
            if(frame.frameId()!=rx || frame.hasExtendedFrameFormat()!=extended || frame.hasLocalEcho() || frame.frameType()!=QCanBusFrame::DataFrame || frame.hasFlexibleDataRateFormat()) continue;
            if(incoming.isEmpty()) { if(bytes.size()<5 || b(bytes[0])!=device || b(bytes[1])!=0x50) continue; expected=b(bytes[2]);if(!expected)expected=256; }
            if(expected<7 || bytes.size()!=qMin(8,expected-int(incoming.size()))) { incoming.clear();expected=0;continue; }
            incoming+=bytes;receiveDeadline.start(); if(incoming.size()!=expected) continue;
            auto request=std::exchange(incoming,{});receiveDeadline.stop();expected=0;
            if(crc(request)!=0 || !outgoing.isEmpty()) continue;
            outgoing=makeReply(request,models);offset=0;sending.start();
            QTextStream(stdout)<<QJsonDocument(QJsonObject{{"sourceKind","virtual-can-test"},{"requestHex",QString::fromLatin1(request.toHex())},{"responseHex",QString::fromLatin1(outgoing.toHex())}}).toJson(QJsonDocument::Compact)<<Qt::endl;
        }
    });
    if(!bus->connectDevice()) return 3;
    QTimer::singleShot(duration,&app,&QCoreApplication::quit); return app.exec();
}
