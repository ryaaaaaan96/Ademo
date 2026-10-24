#include "upp_session.h"
#include <QComboBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
using namespace debugger;
QWidget *createUppControls(SessionAccess access,QWidget *parent) {
    auto *panel=new QWidget(parent);auto *layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);
    auto *device=new QSpinBox(panel);device->setObjectName("uppTargetDevice");device->setRange(1,255);device->setValue(5);layout->addWidget(new QLabel(QStringLiteral("请求设备地址"),panel));layout->addWidget(device);
    auto *function=new QComboBox(panel);function->setObjectName("uppFunction");
    for(auto entry:QList<QPair<int,QString>>{{0x20,QStringLiteral("20 握手")},{0x21,QStringLiteral("21 单段读取")},{0x22,QStringLiteral("22 多段读取")},{0x23,QStringLiteral("23 增量查询")},{0x26,QStringLiteral("26 单段写入")},{0x27,QStringLiteral("27 多段写入")},{0x28,QStringLiteral("28 组合读写")}})function->addItem(entry.second,entry.first);
    layout->addWidget(function);auto *parameters=new QPlainTextEdit(panel);parameters->setObjectName("uppRequestParameters");parameters->setMaximumHeight(140);layout->addWidget(new QLabel(QStringLiteral("参数JSON：模型、偏移、字节长度或dataHex"),panel));layout->addWidget(parameters);
    const QMap<int,QByteArray> examples{{0x20,"{}"},{0x21,R"({"model":2,"offset":6,"length":3})"},{0x22,R"({"units":[{"model":2,"offset":6,"length":3}]})"},{0x23,R"({"incrementType":1,"subsequence":0})"},{0x26,R"({"model":2,"offset":6,"dataHex":"010203"})"},{0x27,R"({"units":[{"model":2,"offset":6,"dataHex":"010203"}]})"},{0x28,R"({"readUnits":[{"model":2,"offset":6,"length":3}],"writeUnits":[{"model":2,"offset":6,"dataHex":"010203"}]})"}};
    parameters->setPlainText("{}");
    QObject::connect(function,&QComboBox::currentIndexChanged,panel,[function,parameters,examples]{parameters->setPlainText(QString::fromUtf8(examples.value(function->currentData().toInt())));});
    auto *send=new QPushButton(QStringLiteral("发送一次（不自动重试）"),panel);send->setObjectName("uppSend");layout->addWidget(send);
    auto *status=new QLabel(QStringLiteral("仅明确发送才与设备交互；写入可能改变设备数据"),panel);status->setObjectName("uppOperationStatus");status->setWordWrap(true);layout->addWidget(status);
    auto current=std::make_shared<quint64>(0);
    QObject::connect(send,&QPushButton::clicked,panel,[access,current,device,function,parameters,status]{
        QJsonParseError error;auto doc=QJsonDocument::fromJson(parameters->toPlainText().toUtf8(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) {status->setText(QStringLiteral("参数必须为有效JSON对象"));return;}
        auto request=doc.object();request["deviceId"]=device->value();request["function"]=function->currentData().toInt();
        *current=access.submit("upp.request",request);
    });
    auto *timer=new QTimer(panel);timer->setInterval(100);
    auto refresh=[access,current,send,status]{
        bool busy=false;
        if(*current) {auto op=access.operation(*current);busy=op.request.id && !op.finished;status->setText(op.update.state+QStringLiteral("：")+op.update.detail);}
        send->setEnabled(access.running() && !busy);
    };
    QObject::connect(timer,&QTimer::timeout,panel,refresh);timer->start();refresh();return panel;
}
