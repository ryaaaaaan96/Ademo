#include "debugger/sdk.h"
#include "debugger/parameter_table.h"
#include "debugger/file_editor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace debugger;
// 仅验证 SDK 的独立 DLL，不能放进正式产品插件目录或视为真实设备协议。
namespace {
QJsonObject defaultScene() {
    return {{"kind", "ui-contract-scene"}, {"version", 1}, {"title", QStringLiteral("就绪场景")},
        {"group", QStringLiteral("灯光场景")}, {"mode", "steady"}, {"enabled", true}};
}
QString validateScene(const QJsonObject &document) {
    if (document["kind"] != "ui-contract-scene" || document["version"] != 1)
        return QStringLiteral("仅支持 version=1 的界面契约场景文件");
    for (const auto &key : {"title", "group"}) {
        const auto text = document[key].toString();
        if (text.trimmed().isEmpty() || text.size() > 80 || text == QStringLiteral("诊断"))
            return QStringLiteral("场景名称与分组须为1至80字符，诊断为保留分组");
    }
    if ((document["mode"] != "steady" && document["mode"] != "blink") || !document["enabled"].isBool())
        return QStringLiteral("播放模式或启用标记无效");
    return {};
}
class SceneEditor final : public FileDocumentEditor {
public:
    explicit SceneEditor(QWidget *parent) : FileDocumentEditor(parent) {
        auto *form = new QFormLayout(this);
        title_ = new QLineEdit(this); title_->setAccessibleName(QStringLiteral("文件场景名称"));
        group_ = new QLineEdit(this); group_->setAccessibleName(QStringLiteral("文件场景分组"));
        mode_ = new QComboBox(this); mode_->setAccessibleName(QStringLiteral("文件播放模式")); mode_->addItems({"steady", "blink"});
        enabled_ = new QCheckBox(QStringLiteral("允许预览"), this); enabled_->setAccessibleName(QStringLiteral("文件允许预览"));
        form->addRow(QStringLiteral("场景名称"), title_); form->addRow(QStringLiteral("场景分组"), group_);
        form->addRow(QStringLiteral("播放模式"), mode_); form->addRow(enabled_);
        const auto notify = [this] { if (!loading_ && changed) changed(); };
        connect(title_, &QLineEdit::textChanged, this, notify); connect(group_, &QLineEdit::textChanged, this, notify);
        connect(mode_, &QComboBox::currentIndexChanged, this, notify); connect(enabled_, &QCheckBox::toggled, this, notify);
        setDocument(defaultScene());
    }
    QJsonObject document() const override {
        auto result = original_; result["title"] = title_->text(); result["group"] = group_->text();
        result["mode"] = mode_->currentText(); result["enabled"] = enabled_->isChecked(); return result;
    }
    QString setDocument(const QJsonObject &value) override {
        const auto error = validate(value); if (!error.isEmpty()) return error;
        loading_ = true; original_ = value; title_->setText(value["title"].toString());
        group_->setText(value["group"].toString()); mode_->setCurrentText(value["mode"].toString());
        enabled_->setChecked(value["enabled"].toBool()); loading_ = false; return {};
    }
    QJsonObject emptyDocument() const override { return defaultScene(); }
    QString validate(const QJsonObject &value) const override { return validateScene(value); }
private:
    QJsonObject original_;
    QLineEdit *title_, *group_;
    QComboBox *mode_;
    QCheckBox *enabled_;
    bool loading_ = false;
};
class SceneSession final : public ProtocolSession {
public:
    explicit SceneSession(ProtocolServices services) : services_(std::move(services)) {}
    QString prepare(const SessionConfig &) override { return {}; }
    void activate() override {}
    void stop() override { pending_ = 0; }
    void submit(const OperationRequest &request) override {
        const auto target = request.parameters["target"].toString();
        const auto mode = request.parameters["mode"].toString();
        if (pending_ || request.action != "preview" || (target != "lamp-a" && target != "lamp-b")
            || (mode != "steady" && mode != "blink")) {
            services_.publish({request.id, "rejected", QStringLiteral("测试操作目标或模式无效，或已有操作等待确认"), {}}); return;
        }
        pending_ = request.id; target_ = target; deadline_ = services_.nowUs() + 1000000;
        if (!services_.send(pending_, "PREVIEW " + target.toUtf8() + " " + mode.toUtf8() + "\n")) {
            services_.publish({pending_, "transport-error", QStringLiteral("测试端点拒绝发送"), {}}); pending_ = 0;
        }
    }
    void onInput(const TraceRecord &record) override {
        // 受控测试端点每次交付完整 ACK；这不是产品协议的流解析实现。
        if (!pending_ || record.bytes != "ACK " + target_.toUtf8() + "\n") return;
        Message message{0, pending_, {record.id}, "success", QStringLiteral("测试端点确认场景"), {{"target", target_}}};
        message.sourceRanges = {{record.id, 0, int(record.bytes.size())}};
        services_.publishMessage(message);
        services_.publish({pending_, "success", QStringLiteral("场景已由受控测试端点确认"), {{"target", target_}}}); pending_ = 0;
    }
    void onSent(quint64 id, bool ok, const QString &reason, qint64) override {
        if (id == pending_ && !ok) { services_.publish({id, "transport-error", reason, {}}); pending_ = 0; }
    }
    void tick() override {
        if (pending_ && services_.nowUs() >= deadline_) {
            services_.publish({pending_, "timeout", QStringLiteral("测试预览未确认；不自动重发"), {}}); pending_ = 0;
        }
    }
private:
    ProtocolServices services_;
    quint64 pending_ = 0;
    qint64 deadline_ = 0;
    QString target_;
};
class SceneView final : public ParameterView {
public:
    SceneView(SessionAccess access, QWidget *parent) : ParameterView(parent), access_(std::move(access)) {
        auto *layout = new QVBoxLayout(this);
        auto *notice = new QLabel(QStringLiteral("独立界面契约插件：仅用于受控测试，不是 UPP 或产品支持组合。"), this);
        notice->setWordWrap(true); layout->addWidget(notice);
        target_ = new QLabel(this); layout->addWidget(target_);
        auto *pages = new QTabWidget(this); pages->setAccessibleName(QStringLiteral("场景页面")); layout->addWidget(pages);
        auto *operations = new QWidget(pages); auto *operationLayout = new QVBoxLayout(operations);
        group_ = new QComboBox(operations); group_->setAccessibleName(QStringLiteral("场景分组")); operationLayout->addWidget(group_);
        table_ = new ParameterTable(operations); table_->setAccessibleName(QStringLiteral("场景参数表"));
        table_->setColumns({{"scene", QStringLiteral("场景")}, {"mode", QStringLiteral("本次预览模式")},
            {"actions", QStringLiteral("动作")}});
        operationLayout->addWidget(table_); pages->addTab(operations, QStringLiteral("参数操作"));
        file_ = new FileEditorPage(access_, new SceneEditor(pages),
            [this](QString target, const QJsonObject &document, QString path) {
                const auto lifetime = access_.lifetime;
                QPointer<SceneView> alive(this);
                if (access_.running()) return QStringLiteral("请停止测试会话后应用场景声明");
                auto config = access_.config().protocol;
                auto scenes = config["scenes"].toObject(); scenes[target] = document; config["scenes"] = scenes;
                auto files = config["sceneFiles"].toObject(); files[target] = path; config["sceneFiles"] = files;
                const bool accepted = access_.configure(config);
                if (alive && accepted) refresh();
                return accepted ? QString() : QStringLiteral("场景配置被修改保护拒绝");
            }, pages);
        pages->addTab(file_, QStringLiteral("文件编辑"));
        table_->onAction = [this](QString, QString action) {
            if (action == "preview") access_.submit("preview", {{"target", selected_}, {"mode", mode_->currentText()}});
        };
        connect(group_, &QComboBox::currentTextChanged, this, [this] { rebuildRows(); });
        selectDevice("lamp-a");
        auto *timer = new QTimer(this); timer->setInterval(100);
        connect(timer, &QTimer::timeout, this, [this] { refresh(); }); timer->start();
    }
    QList<ParameterDevice> devices() const override { return {{"lamp-a", QStringLiteral("灯组 A")}, {"lamp-b", QStringLiteral("灯组 B")}}; }
    QString selectedDevice() const override { return selected_; }
    bool selectDevice(const QString &id) override {
        const auto lifetime = access_.lifetime;
        if (id != "lamp-a" && id != "lamp-b") return false;
        QPointer<SceneView> alive(this);
        const auto config = access_.config().protocol;
        const auto document = scene(id);
        if (!file_->bindTarget(id, id, document, config["sceneFiles"].toObject()[id].toString()) || !alive) return false;
        selected_ = id; target_->setText(QStringLiteral("场景目标：") + id);
        lastDocument_ = {}; refresh(); return true;
    }
private:
    QJsonObject scene(const QString &target) const {
        const auto value = access_.config().protocol["scenes"].toObject().value(target);
        return value.isObject() ? value.toObject() : defaultScene();
    }
    void refresh() {
        const auto document = scene(selected_);
        const bool running = access_.running();
        if (document == lastDocument_) {
            if (running != lastRunning_ && group_->currentText() == QStringLiteral("诊断")) rebuildRows();
            lastRunning_ = running; return;
        }
        lastRunning_ = running;
        lastDocument_ = document;
        { const QSignalBlocker blocker(group_); group_->clear();
          group_->addItems({document["group"].toString(), QStringLiteral("诊断")}); }
        rebuildRows();
    }
    void rebuildRows() {
        ParameterRow row;
        if (group_->currentText() == QStringLiteral("诊断")) {
            row.id = "status"; row.cells = {{"scene", QStringLiteral("会话状态")},
                {"mode", access_.running() ? QStringLiteral("运行中") : QStringLiteral("停止")}};
            table_->setRows({row}); mode_ = nullptr; return;
        }
        row.id = "preview"; row.cells["scene"] = lastDocument_["title"].toString();
        row.actions = {{"preview", QStringLiteral("预览场景"), lastDocument_["enabled"].toBool()}};
        table_->setRows({row});
        mode_ = new QComboBox(table_); mode_->setAccessibleName(QStringLiteral("本次预览模式"));
        mode_->addItems({"steady", "blink"}); mode_->setCurrentText(lastDocument_["mode"].toString());
        table_->setCellWidget(0, 1, mode_);
    }
    SessionAccess access_;
    QString selected_;
    QJsonObject lastDocument_;
    QLabel *target_;
    ParameterTable *table_;
    QComboBox *mode_ = nullptr, *group_;
    FileEditorPage *file_;
    bool lastRunning_ = false;
};
}
class UiContractPlugin final : public QObject, public ProtocolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DebuggerProtocolIid)
    Q_INTERFACES(debugger::ProtocolPlugin)
public:
    Descriptor describe() const override { return {"ui.contract.scene", QStringLiteral("界面契约场景（仅测试）"), buildKey(), {"preview"}, {"serial"}, {}, {}}; }
    std::unique_ptr<ProtocolSession> createSession(ProtocolServices services) override { return std::make_unique<SceneSession>(std::move(services)); }
    QWidget *createView(SessionAccess, QWidget *) override { return nullptr; }
    ParameterView *createParameterView(SessionAccess access, QWidget *parent) override { return new SceneView(std::move(access), parent); }
    QJsonObject relocateConfiguration(QJsonObject config, const QString &from, const QString &to) const override {
        auto paths = config["sceneFiles"].toObject();
        for (auto it = paths.begin(); it != paths.end(); ++it) {
            const auto path = it.value().toString(); if (path.isEmpty()) continue;
            const auto absolute = QFileInfo(path).isAbsolute() ? path : QDir(from).absoluteFilePath(path);
            it.value() = to.isEmpty() ? absolute : QDir(to).relativeFilePath(absolute);
        }
        if (config.contains("sceneFiles")) config["sceneFiles"] = paths;
        return config;
    }
};
#include "ui_contract_plugin.moc"
