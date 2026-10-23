#include "slave_file_editor.h"
#include "rtu_slave.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
namespace debugger {
namespace {
class SlaveFileEditor final : public FileDocumentEditor {
public:
    SlaveFileEditor(QWidget *parent) : FileDocumentEditor(parent) {
        auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout;
        area_ = new QComboBox(this); area_->setAccessibleName(QStringLiteral("文件数据区"));
        area_->addItems({"coils","discrete","holding","input"}); area_->setCurrentText("holding");
        start_ = new QSpinBox(this); start_->setRange(0,65535); start_->setAccessibleName(QStringLiteral("文件范围起址"));
        count_ = new QSpinBox(this); count_->setRange(0,65536); count_->setAccessibleName(QStringLiteral("文件范围数量"));
        offset_ = new QSpinBox(this); offset_->setRange(0,65535); offset_->setSingleStep(256); offset_->setAccessibleName(QStringLiteral("文件显示偏移"));
        form->addRow(QStringLiteral("数据区"),area_); form->addRow(QStringLiteral("PDU 起始地址"),start_);
        form->addRow(QStringLiteral("数量"),count_); form->addRow(QStringLiteral("显示偏移（每页最多256项）"),offset_); layout->addLayout(form);
        table_ = new QTableWidget(0,2,this); table_->setAccessibleName(QStringLiteral("从站文件内容"));
        table_->setHorizontalHeaderLabels({QStringLiteral("PDU 地址"),QStringLiteral("值")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); layout->addWidget(table_,1);
        auto *hint = new QLabel(QStringLiteral("这里只编辑文件草稿。运行值与外部主站写入不会刷新此快照；保存不会应用运行数据。"),this);
        hint->setWordWrap(true); layout->addWidget(hint);
        connect(area_,&QComboBox::currentIndexChanged,this,[this]{ offset_->setValue(0); refresh(); });
        connect(offset_,&QSpinBox::valueChanged,this,[this]{ refresh(); });
        connect(start_,&QSpinBox::valueChanged,this,[this](int start){
            auto areas = document_["areas"].toObject(); auto area = areas[area_->currentText()].toObject();
            area["start"] = start; areas[area_->currentText()] = area; document_["areas"] = areas; refresh(); if(changed) changed();
        });
        connect(count_,&QSpinBox::valueChanged,this,[this](int count){
            auto areas = document_["areas"].toObject(); auto area = areas[area_->currentText()].toObject(); auto values = area["values"].toArray();
            while(values.size()>count) values.removeLast();
            while(values.size()<count) values.append(area_->currentIndex()<2 ? QJsonValue(false) : QJsonValue(0));
            area["values"] = values; areas[area_->currentText()] = area; document_["areas"] = areas; refresh(); if(changed) changed();
        });
        connect(table_,&QTableWidget::cellChanged,this,[this](int row,int column){
            if(column!=1) return;
            auto areas = document_["areas"].toObject(); auto area = areas[area_->currentText()].toObject(); auto values = area["values"].toArray();
            const auto text = table_->item(row,1)->text(); QJsonValue value = text;
            if(area_->currentIndex()<2) { if(text=="true"||text=="1") value=true; else if(text=="false"||text=="0") value=false; }
            else { bool ok=false; const auto number=text.toInt(&ok); if(ok) value=number; }
            values[offset_->value()+row] = value; area["values"] = values; areas[area_->currentText()] = area; document_["areas"] = areas;
            if(changed) changed();
        });
        setDocument(emptyDocument());
    }
    QJsonObject document() const override { return document_; }
    QJsonObject emptyDocument() const override {
        QJsonObject areas; for(const auto *name:{"coils","discrete","holding","input"}) areas[name]=QJsonObject{{"start",0},{"values",QJsonArray{}}};
        return {{"version",1},{"areas",areas}};
    }
    QString validate(const QJsonObject &document) const override { return validateSlaveDocument(document); }
    QString setDocument(const QJsonObject &document) override {
        const auto error=validate(document); if(!error.isEmpty()) return error;
        document_=document; refresh(); return {};
    }
private:
    void refresh() {
        const auto area=document_["areas"].toObject()[area_->currentText()].toObject(); const auto values=area["values"].toArray();
        const QSignalBlocker s(start_), c(count_), t(table_);
        start_->setValue(area["start"].toInt()); count_->setValue(int(values.size()));
        table_->setRowCount(qMax(0,qMin(256,int(values.size())-offset_->value())));
        for(int row=0;row<table_->rowCount();++row) {
            const auto value=values[offset_->value()+row];
            auto *address=new QTableWidgetItem(QString::number(start_->value()+offset_->value()+row)); address->setFlags(address->flags()&~Qt::ItemIsEditable); table_->setItem(row,0,address);
            table_->setItem(row,1,new QTableWidgetItem(value.isBool()?(value.toBool()?"true":"false"):value.isDouble()?QString::number(value.toInt()):value.toString()));
        }
    }
    QJsonObject document_;
    QComboBox *area_;
    QSpinBox *start_, *count_, *offset_;
    QTableWidget *table_;
};
}
FileDocumentEditor *createSlaveFileEditor(QWidget *parent) { return new SlaveFileEditor(parent); }
}
