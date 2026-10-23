#include "slave_parameters.h"
#include "debugger/parameter_table.h"
#include "debugger/confirmation.h"
#include "slave_file_editor.h"
#include <QComboBox>
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
namespace debugger {
namespace {
class SlaveParameters final : public ParameterView {
public:
    SlaveParameters(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        auto *outer = new QVBoxLayout(this);
        device_ = new QComboBox(this); device_->setAccessibleName(QStringLiteral("模拟设备")); outer->addWidget(device_);
        for (const auto &value : access_.config().protocol["devices"].toArray()) {
            const auto id = QString::number(value.toObject()["unit"].toInt());
            device_->addItem(QStringLiteral("设备 %1").arg(id), id);
        }
        selected_ = selectedDevice();
        tabs_ = new QTabWidget(this); tabs_->setAccessibleName(QStringLiteral("参数页面")); outer->addWidget(tabs_,1);
        auto *operationPage = new QWidget(tabs_); auto *layout = new QVBoxLayout(operationPage);
        tabs_->addTab(operationPage,QStringLiteral("参数操作"));
        auto *bindings = new QHBoxLayout;
        newUnit_=new QSpinBox(this); newUnit_->setRange(1,247); newUnit_->setAccessibleName(QStringLiteral("新增模拟设备地址"));
        bindingPath_=new QLineEdit(this); bindingPath_->setAccessibleName(QStringLiteral("启动文件绑定路径"));
        bindingPath_->setPlaceholderText(QStringLiteral("已有外部 JSON 路径；新建文件时可留空"));
        auto *add=new QPushButton(QStringLiteral("添加模拟设备"),this); add->setToolTip(QStringLiteral("同地址更新绑定，仅停止且修改保护通过时生效"));
        bindings->addWidget(newUnit_); bindings->addWidget(bindingPath_,1); bindings->addWidget(add); layout->addLayout(bindings);
        connect(add,&QPushButton::clicked,this,[this]{ addDevice(); });
        bindingStatus_=new QLabel(this); bindingStatus_->setWordWrap(true); layout->addWidget(bindingStatus_);
        table_ = new ParameterTable(this); table_->setAccessibleName(QStringLiteral("从站模拟参数"));
        area_ = new QComboBox(this); area_->setAccessibleName(QStringLiteral("模拟数据区"));
        area_->addItems({"coils", "discrete", "holding", "input"}); area_->setCurrentText("holding"); layout->addWidget(area_);
        offset_ = new QSpinBox(this); offset_->setAccessibleName(QStringLiteral("模拟显示偏移"));
        offset_->setRange(0, 65535); offset_->setPrefix(QStringLiteral("显示偏移 ")); offset_->setSingleStep(256); layout->addWidget(offset_);
        table_->setColumns({{"name", QStringLiteral("名称")}, {"type", QStringLiteral("类型")},
            {"description", QStringLiteral("说明")}, {"value", QStringLiteral("值")}, {"updated", QStringLiteral("来源")}});
        layout->addWidget(table_, 1);
        status_ = new QLabel(QStringLiteral("操作页修改的是本地模拟值；双击值校验后立即生效，保存文件是独立操作。"), this);
        status_->setAccessibleName(QStringLiteral("模拟值操作状态"));
        status_->setWordWrap(true); layout->addWidget(status_);
        runtimePath_ = new QLineEdit(this); runtimePath_->setAccessibleName(QStringLiteral("运行值保存路径"));
        runtimePath_->setPlaceholderText(QStringLiteral("留空写回当前绑定；另存为时填写新路径")); layout->addWidget(runtimePath_);
        auto *save = new QPushButton(QStringLiteral("保存运行值（不保存文件草稿）"),this); layout->addWidget(save);
        connect(save,&QPushButton::clicked,this,[this]{ saveRuntime(); });
        FileEditorPage::Options options; options.maximumBytes = 4 * 1024 * 1024;
        options.canSave = [this](const QString &path){ return canSaveFile(path); };
        auto *editor = createSlaveFileEditor(this);
        file_ = new FileEditorPage(access_,editor,[this](QString target,const QJsonObject &document,QString path){ return applyFile(target,document,path); },options,this);
        tabs_->addTab(file_,QStringLiteral("文件编辑"));
        connect(tabs_,&QTabWidget::currentChanged,this,[this](int index){ if(index==1 && !fileBound_) bindFile(); });
        if(access_.registerViewState) access_.registerViewState(this,[this]{ return LocalState{false,false,modalBusy_,{}}; },[]{return true;});
        if(access_.setDiscardGuard) {
            QPointer<SlaveParameters> self(this); const auto accessCopy=access_;
            access_.setDiscardGuard([self]{ return self && self->resolveRuntime(); });
            connect(this,&QObject::destroyed,[accessCopy]{ accessCopy.setDiscardGuard({}); });
        }
        table_->onCommit = [this](const QString &id, const QString &, const QString &text) {
            const auto parts = id.split('/'); if (parts.size() != 3) return;
            QJsonValue value;
            if (parts[1] == "coils" || parts[1] == "discrete") {
                if (text == "true" || text == "1") value = true;
                else if (text == "false" || text == "0") value = false;
                else { status_->setText(QStringLiteral("位值须为 true/false 或 1/0")); refresh(); return; }
            } else {
                bool ok = false; const int number = text.toInt(&ok);
                if (!ok || number < 0 || number > 65535) { status_->setText(QStringLiteral("寄存器值须为 0..65535 整数")); refresh(); return; }
                value = number;
            }
            const auto operation = access_.submit("slave-edit-value", {{"unit", parts[0].toInt()}, {"area", parts[1]}, {"address", parts[2].toInt()}, {"value", value}});
            status_->setText(access_.operation(operation).update.detail); refresh();
        };
        connect(device_, &QComboBox::currentIndexChanged, this, [this] {
            const auto next=selectedDevice(); if(next==selected_) return;
            { const QSignalBlocker blocker(device_); device_->setCurrentIndex(device_->findData(selected_)); }
            const auto revision=++selectionRevision_;
            QTimer::singleShot(0,this,[this,next,revision]{ if(revision==selectionRevision_) selectDevice(next); });
        });
        connect(area_, &QComboBox::currentIndexChanged, this, [this] { offset_->setValue(0); refresh(); });
        connect(offset_, &QSpinBox::valueChanged, this, [this] { refresh(); });
        auto *timer = new QTimer(this); timer->setInterval(150); connect(timer, &QTimer::timeout, this, [this] { refresh(); }); timer->start(); refresh();
    }
    QList<ParameterDevice> devices() const override {
        QList<ParameterDevice> result;
        for (int i = 0; i < device_->count(); ++i) result.append({device_->itemData(i).toString(), device_->itemText(i)});
        return result;
    }
    QString selectedDevice() const override { return device_->currentData().toString(); }
    bool selectDevice(const QString &id) override {
        const int index = device_->findData(id); if (index < 0) return false;
        ++selectionRevision_;
        if(id==selected_) return true;
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        if(fileBound_ && !bindFile(id)) return false;
        if(!alive) return false;
        { const QSignalBlocker blocker(device_); device_->setCurrentIndex(index); }
        selected_=id; offset_->setValue(0); runtimePath_->clear(); refresh(); return true;
    }
private:
    void addDevice() {
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        auto configuration=access_.config().protocol; auto bindings=configuration["devices"].toArray();
        const int unit=newUnit_->value(); const auto input=bindingPath_->text().trimmed();
        const auto path=input.isEmpty()?QString():QFileInfo(input).absoluteFilePath();
        int found=-1;
        for(int i=0;i<bindings.size();++i) {
            const auto entry=bindings[i].toObject();
            if(entry["unit"].toInt()==unit) found=i;
            else if(!path.isEmpty() && QFileInfo(entry["path"].toString()).absoluteFilePath().compare(path,Qt::CaseInsensitive)==0) {
                status_->setText(QStringLiteral("每个设备需要独立的启动文件")); return;
            }
        }
        if(found<0 && bindings.size()>=32) { status_->setText(QStringLiteral("一个会话最多32个模拟设备")); return; }
        auto binding=found<0?QJsonObject{}:bindings[found].toObject(); binding["unit"]=unit; binding["path"]=path;
        if(found<0) bindings.append(binding); else bindings[found]=binding;
        configuration["devices"]=bindings;
        const bool configured=access_.configure(configuration);
        if(!alive) return;
        if(!configured) { status_->setText(QStringLiteral("需停止会话并处理未保存修改，绑定未改变")); return; }
        const auto id=QString::number(unit);
        if(device_->findData(id)<0) { const QSignalBlocker blocker(device_); device_->addItem(QStringLiteral("设备 %1").arg(unit),id); }
        selectDevice(id); refresh(); status_->setText(QStringLiteral("设备绑定已更新；仍需有效外部文件才能启动。"));
    }
    QJsonObject runtimeDevice(const QString &id = {}) const {
        if (access_.snapshot) for (const auto &value : access_.snapshot()["devices"].toArray())
            if (value.toObject()["unit"].toInt() == (id.isEmpty()?selectedDevice():id).toInt()) return value.toObject();
        return {};
    }
    bool bindFile(const QString &id = {}) {
        const auto target=id.isEmpty()?selectedDevice():id; if(target.isEmpty()) return false;
        const auto device=runtimeDevice(target);
        auto document=device["data"].toObject();
        if(document.isEmpty()) { QJsonObject areas; for(const auto *name:{"coils","discrete","holding","input"}) areas[name]=QJsonObject{{"start",0},{"values",QJsonArray{}}}; document={{"version",1},{"areas",areas}}; }
        const bool bound=file_->bindTarget(target,QStringLiteral("从站设备 %1").arg(target),document,device["path"].toString());
        if(bound) fileBound_=true; return bound;
    }
    QString canSaveFile(const QString &path) const {
        if(lastFileOperation_ && !access_.operation(lastFileOperation_).finished) return QStringLiteral("运行数据文件操作尚未结束，文件草稿保留");
        for(const auto &entry:access_.config().protocol["devices"].toArray()) {
            const auto binding=entry.toObject(); if(binding["unit"].toInt()==selectedDevice().toInt()) continue;
            const auto other=binding["path"].toString(); if(other.isEmpty()) continue;
            const QFileInfo a(path),b(other);
            if(a.absoluteFilePath().compare(b.absoluteFilePath(),Qt::CaseInsensitive)==0 || (!a.canonicalFilePath().isEmpty() && a.canonicalFilePath().compare(b.canonicalFilePath(),Qt::CaseInsensitive)==0))
                return QStringLiteral("不能覆盖另一模拟设备的启动文件");
        }
        return {};
    }
    QMessageBox::StandardButton decideRuntime(const QString &id) {
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        modalBusy_=true;
        const auto answer=showConfirmation(this,QMessageBox::Warning,QStringLiteral("未保存从站运行值"),
            QStringLiteral("设备 %1 的运行值尚未保存。保存写回该设备绑定文件；放弃用于本次替换。文件编辑草稿单独保护。若保存覆盖了编辑页来源文件，应用会要求重新打开。请选择保存、放弃或取消。").arg(id),
            QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
        if(alive) modalBusy_=false;
        return alive?answer:QMessageBox::Cancel;
    }
    bool resolveRuntime() {
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        const auto captured=access_.snapshot()["devices"].toArray();
        for(const auto &entry:captured) {
            const auto device=entry.toObject(); if(!device["dirty"].toBool()) continue;
            const auto id=QString::number(device["unit"].toInt()); const auto revision=device["revision"];
            const auto choice=decideRuntime(id);
            if(!alive || choice==QMessageBox::Cancel || runtimeDevice(id)["revision"]!=revision) return false;
            if(choice==QMessageBox::Save) {
                if(file_->isBusy()) return false;
                lastFileOperation_=access_.submit("slave-save",{{"unit",id.toInt()}}); refresh(); return false;
            }
            const auto operation=access_.submit("slave-discard",{{"unit",id.toInt()}});
            if(access_.operation(operation).update.state!="success") return false;
        }
        return true;
    }
    QString applyFile(const QString &target,const QJsonObject &document,const QString &path) {
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        if(path.isEmpty()) return QStringLiteral("请先保存或打开有效的外部从站文件");
        const auto error=canSaveFile(path); if(!error.isEmpty()) return error;
        const auto device=runtimeDevice(target);
        if(device.isEmpty()) {
            auto config=access_.config().protocol; auto bindings=config["devices"].toArray();
            for(int i=0;i<bindings.size();++i) if(bindings[i].toObject()["unit"].toInt()==target.toInt()) {
                auto binding=bindings[i].toObject(); binding["path"]=path; bindings[i]=binding;
            }
            config["devices"]=bindings;
            const bool configured=access_.configure(config);
            return configured?QStringLiteral("启动文件绑定已更新；启动时重新校验全部文件，尚无运行数据。"):QStringLiteral("当前无法更新启动文件绑定");
        }
        QString decision;
        if(device["dirty"].toBool()) {
            const auto choice=decideRuntime(target);
            if(!alive) return QStringLiteral("页面已关闭");
            if(choice==QMessageBox::Cancel) return QStringLiteral("已取消应用，运行值与文件草稿保留");
            if(runtimeDevice(target)["revision"]!=device["revision"]) return QStringLiteral("提示期间运行数据已有新修改，未应用，请重试");
            decision=choice==QMessageBox::Save?"save":"discard";
        }
        lastFileOperation_=access_.submit("slave-import",{{"unit",target.toInt()},{"path",path},{"decision",decision},{"expectedDocument",document}});
        const auto result=access_.operation(lastFileOperation_); refresh();
        return result.finished?result.update.detail:QStringLiteral("正在后台校验并应用文件；最终结果见参数操作页，通信继续。");
    }
    void saveRuntime() {
        const auto lifetime=access_.lifetime; QPointer<SlaveParameters> alive(this);
        if(file_->isBusy()) { status_->setText(QStringLiteral("文件页操作尚未结束，未保存运行值")); return; }
        const auto device=runtimeDevice(); if(device.isEmpty()) { status_->setText(QStringLiteral("尚无已加载运行值")); return; }
        const auto path=runtimePath_->text().isEmpty()?device["path"].toString():QFileInfo(runtimePath_->text()).absoluteFilePath();
        const auto error=canSaveFile(path); if(!error.isEmpty()) { status_->setText(error); return; }
        modalBusy_=true;
        const auto choice=showConfirmation(this,QMessageBox::Question,QStringLiteral("保存从站运行值"),
            QStringLiteral("将用当前运行值写入以下文件；文件编辑页的草稿或快照保留，不会作为保存来源。\n%1").arg(path),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
        if(!alive) return; modalBusy_=false;
        if(choice!=QMessageBox::Yes) return;
        if(runtimeDevice()["revision"]!=device["revision"]) { status_->setText(QStringLiteral("确认期间出现新运行值，请重新确认保存")); return; }
        lastFileOperation_=access_.submit("slave-save",{{"unit",selectedDevice().toInt()},{"path",path}}); refresh();
    }
    void refresh() {
        const auto device = runtimeDevice();
        QString path=device["path"].toString();
        if(path.isEmpty()) for(const auto &entry:access_.config().protocol["devices"].toArray())
            if(entry.toObject()["unit"].toInt()==selectedDevice().toInt()) path=entry.toObject()["path"].toString();
        bindingStatus_->setText(QStringLiteral("设备 %1 · 启动文件：%2\n%3").arg(selectedDevice(),path.isEmpty()?QStringLiteral("尚未绑定，不能启动"):path,
            device.isEmpty()?QStringLiteral("尚无运行数据；请打开或新建文件，保存后明确应用绑定。"):device["dirty"].toBool()?QStringLiteral("运行值已修改、尚未保存；文件页保持独立快照。"):QStringLiteral("运行值已加载或保存；外部文件变化不会热加载。")));
        const auto area = device["data"].toObject()["areas"].toObject()[area_->currentText()].toObject();
        const auto values = area["values"].toArray();
        QList<ParameterRow> rows;
        for (int i = offset_->value(); i < qMin(int(values.size()), offset_->value() + 256); ++i) {
            const auto address = area["start"].toInt() + i;
            ParameterRow row; row.id = selectedDevice() + '/' + area_->currentText() + '/' + QString::number(address);
            row.cells = {{"name", QStringLiteral("PDU %1").arg(address)}, {"type", values[i].isBool() ? "bool" : "uint16"},
                {"description", area_->currentText()}, {"value", values[i].isBool() ? (values[i].toBool() ? "true" : "false") : QString::number(values[i].toInt())},
                {"updated", device["dirty"].toBool() ? QStringLiteral("运行值未保存") : QStringLiteral("已加载/保存")}};
            row.editableCells.insert("value"); rows.append(row);
        }
        QStringList ids; for (const auto &row : rows) ids.append(row.id);
        if (ids != displayed_) { table_->setRows(rows); displayed_ = ids; }
        else for (const auto &row : rows) table_->updateRow(row);
        if(lastFileOperation_) {
            const auto result=access_.operation(lastFileOperation_);
            status_->setText(QStringLiteral("%1：%2").arg(result.update.state,result.update.detail));
        }
    }
    SessionAccess access_;
    QComboBox *device_, *area_;
    QSpinBox *offset_, *newUnit_;
    QLabel *status_, *bindingStatus_;
    QLineEdit *bindingPath_;
    QLineEdit *runtimePath_;
    QTabWidget *tabs_;
    FileEditorPage *file_;
    QString selected_;
    bool fileBound_=false, modalBusy_=false;
    quint64 selectionRevision_=0, lastFileOperation_=0;
    QStringList displayed_;
    ParameterTable *table_;
};
}
ParameterView *createSlaveParameterView(SessionAccess access, QWidget *parent) { return new SlaveParameters(std::move(access), parent); }
}
