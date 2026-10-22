#include "slave_view.h"
#include <QComboBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

namespace debugger {
namespace {
QJsonObject deviceFor(const SessionAccess &access, int unit) {
    if (access.snapshot) for (const auto &entry : access.snapshot().value("devices").toArray())
        if (entry.toObject().value("unit").toInt() == unit) return entry.toObject();
    return {};
}
QString decision(QWidget *view) {
    const auto answer = QMessageBox::question(view, QStringLiteral("存在未保存修改"),
        QStringLiteral("替换或重新加载前，选择保存、放弃或取消。保存文件不会触发设备写入。"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Save ? "save" : answer == QMessageBox::Discard ? "discard" : "cancel";
}
bool resolveUnsaved(SessionAccess access, QWidget *view) {
    const auto devices = access.snapshot ? access.snapshot().value("devices").toArray() : QJsonArray{};
    bool dirty = false;
    for (const auto &entry : devices) dirty = dirty || entry.toObject().value("dirty").toBool();
    if (!dirty) return true;
    const auto choice = decision(view);
    if (choice == "cancel" || !access.submit || !access.operation) return false;
    for (const auto &entry : devices) {
        const auto device = entry.toObject();
        if (!device.value("dirty").toBool()) continue;
        const auto id = access.submit(choice == "save" ? "slave-save" : "slave-discard", {{"unit", device.value("unit")}});
        if (!access.operation(id).finished) {
            QProgressDialog progress(QStringLiteral("后台保存文件，完成后再重载"), QStringLiteral("取消重载"), 0, 0, view);
            progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0);
            QEventLoop loop; QTimer timer;
            QObject::connect(&progress, &QProgressDialog::canceled, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, [&] { if (access.operation(id).finished) loop.quit(); });
            timer.start(25); loop.exec();
            if (progress.wasCanceled()) return false;
        }
        const auto result = access.operation(id);
        if (!result.finished || result.update.state != "success") {
            QMessageBox::warning(view, QStringLiteral("未重新加载"), result.update.detail); return false;
        }
    }
    for (const auto &entry : access.snapshot().value("devices").toArray())
        if (entry.toObject().value("dirty").toBool()) return false;
    return true;
}
}
void addSlaveEditor(SessionAccess access, QWidget *view, QVBoxLayout *layout) {
    auto *form = new QFormLayout;
    auto *unit = new QSpinBox(view); unit->setRange(1, 247); unit->setObjectName("slaveEditUnit");
    auto *area = new QComboBox(view); area->addItems({"coils", "discrete", "holding", "input"}); area->setCurrentText("holding"); area->setObjectName("slaveEditArea");
    auto *address = new QSpinBox(view); address->setRange(0, 65535); address->setObjectName("slaveEditAddress");
    auto *value = new QLineEdit("42", view); value->setObjectName("slaveEditValue");
    form->addRow(QStringLiteral("编辑设备"), unit); form->addRow(QStringLiteral("数据区"), area);
    form->addRow(QStringLiteral("PDU 地址 / 新范围起址"), address); form->addRow(QStringLiteral("单值 JSON"), value);
    auto *values = new QPlainTextEdit("[10,20]", view); values->setMaximumHeight(70); values->setObjectName("slaveAreaValues");
    auto *path = new QLineEdit(view); path->setObjectName("slaveFilePath");
    auto *status = new QLabel(view); status->setWordWrap(true); status->setObjectName("slaveEditStatus");
    auto *dirtyStatus = new QLabel(view); dirtyStatus->setWordWrap(true); dirtyStatus->setObjectName("slaveDirtyStatus");
    auto operation = std::make_shared<quint64>(0);
    auto submit = [access, operation](QString action, QJsonObject parameters) {
        if (access.submit) *operation = access.submit(std::move(action), std::move(parameters));
    };
    auto button = [form, view](QString text, QString name, std::function<void()> action) {
        auto *widget = new QPushButton(text, view); widget->setObjectName(name); form->addRow(widget);
        QObject::connect(widget, &QPushButton::clicked, view, std::move(action));
    };
    button(QStringLiteral("修改运行值（立即生效）"), "slaveEditApply", [=] {
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(("[" + value->text() + "]").toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || json.array().size() != 1) { status->setText(QStringLiteral("单值 JSON 无效")); return; }
        submit("slave-edit-value", {{"unit", unit->value()}, {"area", area->currentText()}, {"address", address->value()}, {"value", json.array()[0]}});
    });
    form->addRow(QStringLiteral("范围值 JSON 数组"), values);
    button(QStringLiteral("整体替换此数据区范围和值"), "slaveReplaceArea", [=] {
        if (values->toPlainText().size() > 4 * 1024 * 1024) { status->setText(QStringLiteral("输入超过 4 MiB")); return; }
        const auto json = QJsonDocument::fromJson(values->toPlainText().toUtf8());
        if (!json.isArray()) { status->setText(QStringLiteral("范围值须为 JSON 数组")); return; }
        submit("slave-replace-area", {{"unit", unit->value()}, {"area", area->currentText()}, {"start", address->value()}, {"values", json.array()}});
    });
    form->addRow(QStringLiteral("导入 / 另存为绝对路径"), path);
    button(QStringLiteral("选择导入文件"), "slaveChooseImport", [=] {
        const auto selected = QFileDialog::getOpenFileName(view, QStringLiteral("导入单设备 JSON"), path->text(), "JSON (*.json)");
        if (!selected.isEmpty()) path->setText(selected);
    });
    auto doImport = [=](bool reloadCurrent) {
        QJsonObject parameters{{"unit", unit->value()}};
        if (!reloadCurrent) parameters["path"] = path->text();
        if (deviceFor(access, unit->value()).value("dirty").toBool()) {
            const auto choice = decision(view);
            if (choice == "cancel") { status->setText(QStringLiteral("已取消导入，原数据与绑定保留")); return; }
            parameters["decision"] = choice;
        }
        submit("slave-import", parameters);
    };
    button(QStringLiteral("校验并导入目标设备"), "slaveImport", [=] { doImport(false); });
    button(QStringLiteral("重新加载当前绑定文件"), "slaveReload", [=] { doImport(true); });
    button(QStringLiteral("保存当前设备文件"), "slaveSave", [=] { submit("slave-save", {{"unit", unit->value()}}); });
    button(QStringLiteral("另存为并绑定新文件"), "slaveSaveAs", [=] {
        auto selected = path->text();
        if (selected.isEmpty()) selected = QFileDialog::getSaveFileName(view, QStringLiteral("另存为"), {}, "JSON (*.json)");
        if (!selected.isEmpty()) submit("slave-save", {{"unit", unit->value()}, {"path", selected}});
    });
    button(QStringLiteral("放弃目标设备未保存修改"), "slaveDiscard", [=] {
        if (deviceFor(access, unit->value()).value("dirty").toBool()) {
            const auto choice = decision(view);
            if (choice != "cancel") submit(choice == "save" ? "slave-save" : "slave-discard", {{"unit", unit->value()}});
        }
    });
    form->addRow(dirtyStatus); form->addRow(status); layout->addLayout(form);
    if (access.setDiscardGuard) {
        QPointer<QWidget> weak(view);
        access.setDiscardGuard([weak, access] { return weak && resolveUnsaved(access, weak); });
        QObject::connect(view, &QObject::destroyed, [access] { access.setDiscardGuard({}); });
    }
    auto *timer = new QTimer(view); timer->setInterval(100);
    QObject::connect(timer, &QTimer::timeout, view, [=] {
        QStringList dirty;
        if (access.snapshot) for (const auto &entry : access.snapshot().value("devices").toArray())
            if (entry.toObject().value("dirty").toBool()) dirty << QString::number(entry.toObject().value("unit").toInt());
        dirtyStatus->setText(QStringLiteral("未保存设备：%1；停止通信仍保留本地数据，可继续保存。").arg(dirty.isEmpty() ? QStringLiteral("无") : dirty.join(',')));
        if (*operation && access.operation) {
            const auto result = access.operation(*operation);
            status->setText(QStringLiteral("%1：%2").arg(result.update.state, result.update.detail));
        }
    }); timer->start();
}
}
