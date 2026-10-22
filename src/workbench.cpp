#include "debugger/host.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace debugger {
Workbench::Workbench(std::shared_ptr<PluginCatalog> catalog, QWidget *parent,
                     DebugSession::EndpointFactory factory)
    : QWidget(parent), catalog_(std::move(catalog)), session_(catalog_, nullptr, std::move(factory))
{
    setWindowTitle(QStringLiteral("多协议调试工具 · RTU 会话"));
    resize(680, 540);
    auto *layout = new QVBoxLayout(this);
    auto *formWidget = new QWidget(this);
    auto *form = new QFormLayout(formWidget);
    auto *protocol = new QComboBox(formWidget);
    auto *transport = new QComboBox(formWidget);
    auto *role = new QComboBox(formWidget);
    protocol->setObjectName("protocol");
    transport->setObjectName("transport");
    role->setObjectName("role");
    for (auto *p : catalog_->protocols()) protocol->addItem(p->describe().name, p->describe().id);
    for (auto *t : catalog_->transports()) transport->addItem(t->describe().name, t->describe().id);
    form->addRow(QStringLiteral("协议"), protocol);
    form->addRow(QStringLiteral("传输"), transport);
    form->addRow(QStringLiteral("角色"), role);
    layout->addWidget(formWidget);
    auto *endpointWidget = new QWidget(this);
    auto *endpointForm = new QFormLayout(endpointWidget);
    layout->addWidget(endpointWidget);
    auto editors = std::make_shared<QMap<QString, QWidget *>>();
    auto updateTransport = [this, transport, endpointForm, editors] {
        while (endpointForm->rowCount()) endpointForm->removeRow(0);
        editors->clear();
        auto *t = catalog_->transport(transport->currentData().toString());
        if (!t) return;
        for (const auto &field : t->describe().fields) {
            QWidget *editor;
            if (field.choices.isEmpty()) {
                editor = new QLineEdit(field.defaultValue);
            } else {
                auto *combo = new QComboBox;
                combo->addItems(field.choices);
                combo->setCurrentText(field.defaultValue);
                editor = combo;
            }
            editor->setObjectName(field.key);
            editors->insert(field.key, editor);
            endpointForm->addRow(field.label, editor);
        }
    };
    auto updateProtocol = [this, protocol, role, layout] {
        role->clear();
        delete view_;
        view_ = nullptr;
        auto *p = catalog_->protocol(protocol->currentData().toString());
        if (!p) return;
        for (const auto &r : p->describe().roles) role->addItem(r, r);
        view_ = catalog_->createView(p->describe().id, this);
        if (view_) layout->addWidget(view_);
    };
    connect(transport, &QComboBox::currentIndexChanged, this, updateTransport);
    connect(protocol, &QComboBox::currentIndexChanged, this, updateProtocol);
    updateTransport();
    auto *start = new QPushButton(QStringLiteral("启动会话"), this);
    auto *stop = new QPushButton(QStringLiteral("停止并释放端点"), this);
    start->setObjectName("start");
    stop->setObjectName("stop");
    auto *status = new QLabel(this);
    status->setObjectName("status");
    status->setWordWrap(true);
    auto *diagnostics = new QLabel(catalog_->diagnostics().join('\n'), this);
    diagnostics->setWordWrap(true);
    layout->addWidget(start);
    layout->addWidget(stop);
    layout->addWidget(status);
    layout->addWidget(diagnostics);
    updateProtocol();
    auto refresh = [this, formWidget, endpointWidget, protocol, transport, start, stop, status] {
        const bool active = session_.state() == SessionState::Starting || session_.state() == SessionState::Running;
        formWidget->setEnabled(!active);
        endpointWidget->setEnabled(!active);
        start->setEnabled(!active && protocol->count() && transport->count());
        stop->setEnabled(active);
        status->setText(stateText(session_.state()) + "\n" + session_.diagnostic());
    };
    connect(&session_, &DebugSession::changed, this, refresh);
    connect(start, &QPushButton::clicked, this, [this, protocol, transport, role, editors] {
        SessionConfig config;
        config.protocolId = protocol->currentData().toString();
        config.transportId = transport->currentData().toString();
        config.role = role->currentData().toString();
        for (auto it = editors->cbegin(); it != editors->cend(); ++it) {
            if (auto *line = qobject_cast<QLineEdit *>(it.value())) config.endpoint[it.key()] = line->text();
            else if (auto *combo = qobject_cast<QComboBox *>(it.value())) config.endpoint[it.key()] = combo->currentText();
        }
        session_.start(config);
    });
    connect(stop, &QPushButton::clicked, &session_, &DebugSession::stop);
    refresh();
}
Workbench::~Workbench()
{
    // 在会话与 DLL 租约释放前销毁插件视图。
    delete view_;
    session_.stop();
}
}
