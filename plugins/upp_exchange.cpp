#include "upp_session.h"
#include "upp_request.h"
#include <QJsonDocument>
#include <QMap>
using namespace debugger;
std::unique_ptr<ProtocolSession> createUppObserver(ProtocolServices);
namespace {
struct Route { quint32 tx=0, rx=0; bool extended=false; };
struct Transaction {
    quint64 id=0; upp::Request request; Route route; int sequence=0, offset=0, completed=0;
    QByteArray adu; bool accepted=false, waitingLocal=false, allLocal=false;
    qint64 due=0, localDeadline=0, replyDeadline=0; std::optional<Message> early;
    QList<TraceRange> sources;
};
class Exchange final : public ProtocolSession {
    ProtocolServices services_;
    std::unique_ptr<ProtocolSession> observer_;
    bool master_=false, active_=false;
    QMap<int,Route> routes_; QMap<int,int> sequences_;
    QMap<QString,qint64> retired_;
    qint64 gapUs_=20000, replyUs_=1000000;
    Transaction transaction_;
    qint64 now() const { return services_.nowUs(); }
    QString key(int device,int seq) const { return QString::number(device)+':'+QString::number(seq); }
    void finish(QString state,QString detail,QJsonObject result={}) {
        if(!transaction_.id) return;
        auto old=std::exchange(transaction_,{});
        retired_[key(old.request.device,old.sequence)]=now()+replyUs_;
        result["sequence"]=old.sequence; result["acceptedAny"]=old.accepted;
        result["localFramesCompleted"]=old.completed; result["frameCount"]=(old.adu.size()+7)/8;
        result["allFramesLocal"]=old.allLocal; result["provisional"]=true;
        if(old.request.writing && old.accepted && state!="succeeded" && state!="protocol-exception") {
            result["uncertain"]=true;
            state="uncertain"; detail+=QStringLiteral("；设备写入结果不确定，不自动重试");
        }
        services_.publish({old.id,state,detail,result});
    }
    void reply(Message message) {
        if(!transaction_.id) { services_.publishMessage(std::move(message)); return; }
        const auto &f=message.fields;
        const bool exception=message.status=="protocol-exception";
        const bool matches=(message.status=="decoded" || exception)
            && f.value("deviceId").toInt()==transaction_.request.device
            && f.value("sequence").toInt()==transaction_.sequence
            && f.value("function").toInt()==(transaction_.request.function | (exception?0x80:0))
            && upp::responseMatches(transaction_.request,f.value("pdu").toObject(),exception);
        if(!matches) { services_.publishMessage(std::move(message)); return; }
        message.operationId=transaction_.id;
        if(!transaction_.allLocal) {
            if(!transaction_.early) transaction_.early=message;
            message.fields["confirmationPendingLocalCompletion"]=true;
            services_.publishMessage(std::move(message)); return;
        }
        services_.publishMessage(message);
        finish(exception?"protocol-exception":"succeeded",exception?QStringLiteral("设备回复协议异常"):QStringLiteral("收到匹配的设备应答"),message.fields);
    }
    void sendNext() {
        auto &t=transaction_; if(!t.id || t.waitingLocal || t.allLocal || now()<t.due) return;
        const auto bytes=t.adu.mid(t.offset,8);
        QJsonObject metadata{{"unitKind","can-frame"},{"frameType","data"},{"canId",int(t.route.tx)},{"extended",t.route.extended},
            {"fragmentIndex",t.offset/8},{"fragmentCount",(t.adu.size()+7)/8},{"protocolAttempt",1}};
        t.waitingLocal=true; t.localDeadline=now()+replyUs_; t.offset+=bytes.size();
        const auto receipt=services_.sendUnit ? services_.sendUnit(t.id,bytes,metadata) : SendReceipt{};
        const bool accepted=receipt.accepted;
        if(receipt.recordId) t.sources.append({receipt.recordId,0,int(bytes.size())});
        t.accepted|=accepted;
        if(!accepted) { t.waitingLocal=false; finish("send-failed",QStringLiteral("CAN分片未获后端接受，停止剩余发送")); return; }
        services_.publish({t.id,"submitted",QStringLiteral("分片已提交，尚未收到设备确认"),{{"acceptedFrames",(t.offset+7)/8},{"frameCount",(t.adu.size()+7)/8}}});
    }
public:
    explicit Exchange(ProtocolServices s):services_(std::move(s)) {}
    QString prepare(const SessionConfig &config) override {
        master_=config.role=="master";
        auto options=config.protocol;
        if(master_) {
            auto duration=[&](const char *name,int fallback,qint64 &out,int minimum) {
                auto v=options.value(name); bool ok=true; double n=v.isUndefined()?fallback:v.isString()?v.toString().toDouble(&ok):v.isDouble()?v.toDouble():-1;
                if(!ok || n<minimum || n>60000 || n!=int(n)) return false; out=qint64(n)*1000; return true;
            };
            if(!duration("sendGapMs",20,gapUs_,0) || !duration("responseTimeoutMs",1000,replyUs_,1)) return QStringLiteral("发送间隔/应答超时无效");
            QJsonObject routeOptions;
            auto routeValue=options.value("deviceRoutes");
            if(routeValue.isString()) { QJsonParseError error; auto doc=QJsonDocument::fromJson(routeValue.toString().toUtf8(),&error); if(error.error!=QJsonParseError::NoError || !doc.isObject()) return QStringLiteral("设备路由必须是JSON对象"); routeOptions=doc.object(); }
            else if(routeValue.isObject()) routeOptions=routeValue.toObject();
            else if(!routeValue.isUndefined()) return QStringLiteral("设备路由必须是JSON对象");
            for(auto it=routeOptions.begin();it!=routeOptions.end();++it) {
                bool valid; int device=it.key().toInt(&valid); if(!valid || device<1 || device>255 || !it->isObject()) return QStringLiteral("设备路由地址必须为1..255");
                auto o=it->toObject(); if(o.contains("extended") && !o.value("extended").isBool()) return QStringLiteral("扩展帧标志必须为布尔值");
                Route route{quint32(device),quint32(device),o.value("extended").toBool()};
                for(auto field:{"txId","rxId"}) if(o.contains(field)) {
                    auto v=o.value(field); double n=v.isDouble()?v.toDouble():-1;
                    if(n<0 || n>(route.extended?0x1fffffff:0x7ff) || n!=qint64(n)) return QStringLiteral("CAN ID超出所选帧格式范围");
                    if(QString(field)=="txId") route.tx=quint32(n); else route.rx=quint32(n);
                }
                routes_[device]=route;
            }
            options["direction"]="response"; options["requestCanIds"]=""; options["responseCanIds"]="";
        }
        auto observed=services_;
        observed.publishMessage=[this](Message m){ if(master_) reply(std::move(m)); else services_.publishMessage(std::move(m)); };
        observer_=createUppObserver(std::move(observed)); auto observerConfig=config; observerConfig.protocol=options;
        return observer_->prepare(observerConfig);
    }
    QJsonObject snapshot() const override { auto o=observer_?observer_->snapshot():QJsonObject{}; o["activeOperation"]=QString::number(transaction_.id); return o; }
    void activate() override { active_=true; observer_->activate(); }
    void stop() override { active_=false; finish("cancelled",QStringLiteral("会话停止，后续分片未发送")); if(observer_)observer_->stop(); }
    void connectionLost(const QString &reason) override { active_=false; finish("transport-failed",reason); if(observer_)observer_->connectionLost(reason); }
    void connectionRestored(bool reconnect) override { active_=true; observer_->connectionRestored(reconnect); }
    void submit(const OperationRequest &r) override {
        if(!master_) { observer_->submit(r); return; }
        auto reject=[&](QString why){services_.publish({r.id,"rejected",why,{}});};
        if(!active_ || transaction_.id || r.action!="upp.request") { reject(QStringLiteral("UPP忙、未启动或操作不支持")); return; }
        auto request=upp::encodeRequest(r.parameters); if(!request.error.isEmpty()) { reject(request.error); return; }
        for(auto it=retired_.begin();it!=retired_.end();) if(it.value()<=now()) it=retired_.erase(it); else ++it;
        if(retired_.size()>=1024) {reject(QStringLiteral("近期操作身份保护容量已满，请稍后提交"));return;}
        int sequence=sequences_.value(request.device,1);
        if(retired_.contains(key(request.device,sequence))) { reject(QStringLiteral("序号回绕保护期内，请稍后提交")); return; }
        sequences_[request.device]=sequence==255?1:sequence+1;
        observer_->stop(); observer_->activate();
        auto &t=transaction_; t.id=r.id; t.request=request; t.sequence=sequence; t.route=routes_.value(request.device,{quint32(request.device),quint32(request.device),false});
        t.adu=upp::encodeAdu(request.device,sequence,request.function,request.pdu); t.due=now();
        services_.publish({r.id,"queued",QStringLiteral("暂定UPP操作，不自动重试"),{{"sequence",sequence}}}); sendNext();
    }
    void onInput(const TraceRecord &r) override {
        if(master_ && transaction_.id && transaction_.allLocal && r.monotonicUs>=transaction_.replyDeadline) finish("timeout",QStringLiteral("应答在期限后到达"));
        if(master_ && transaction_.id && r.kind=="rx") {
            if(r.metadata.value("localEcho").toBool() || r.metadata.value("canId").toInt(-1)!=int(transaction_.route.rx)
               || r.metadata.value("extended").toBool()!=transaction_.route.extended || transaction_.offset<transaction_.adu.size()) return;
        }
        observer_->onInput(r);
    }
    void onSent(quint64 id,bool ok,const QString &reason,qint64 observedUs) override {
        auto &t=transaction_; if(!master_ || !t.id || id!=t.id || !t.waitingLocal) return;
        if(!ok || observedUs>=t.localDeadline) { finish("send-failed",reason.isEmpty()?QStringLiteral("本地发送失败或超时"):reason); return; }
        t.waitingLocal=false; ++t.completed;
        if(t.offset==t.adu.size()) {
            t.allLocal=true; t.replyDeadline=observedUs+replyUs_;
            Message sent; sent.operationId=t.id; sent.status="sent"; sent.detail=QStringLiteral("完整UPP请求本地发送完成，尚不代表设备确认"); sent.sourceRanges=t.sources;
            sent.fields={{"deviceId",t.request.device},{"sequence",t.sequence},{"function",t.request.function},{"direction","request"},{"pdu",t.request.fields},{"aduHex",QString::fromLatin1(t.adu.toHex())},{"provisional",true}};
            for(const auto &range:t.sources) sent.sources.append(range.recordId);services_.publishMessage(std::move(sent));
            services_.publish({t.id,"sent",QStringLiteral("全部分片本地完成，等待设备应答"),{{"allFramesLocal",true}}});
            if(t.early) { auto early=std::move(*t.early); const bool exception=early.status=="protocol-exception"; finish(exception?"protocol-exception":"succeeded",QStringLiteral("先到应答已通过验证，全部分片本地完成"),early.fields); }
        } else t.due=observedUs+gapUs_;
    }
    void tick() override {
        observer_->tick(); if(!active_ || !transaction_.id) return;
        const auto &t=transaction_;
        if(t.waitingLocal && now()>=t.localDeadline) { finish("send-failed",QStringLiteral("本地发送完成超时")); return; }
        if(t.allLocal && now()>=t.replyDeadline) { finish("timeout",QStringLiteral("设备应答超时")); return; }
        sendNext();
    }
};
}
std::unique_ptr<ProtocolSession> createUppSession(ProtocolServices s) { return std::make_unique<Exchange>(std::move(s)); }
