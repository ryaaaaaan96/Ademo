#include "master_controls.h"
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
namespace debugger {
void showReadSettings(SessionAccess access, QWidget *parent) {
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("读取重试配置"));
    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    const auto configuration = access.config ? access.config().protocol : QJsonObject{};
    auto field = [&](const QString &label, const char *key, int low, int high, int fallback) {
        auto *spin = new QSpinBox(dialog);
        spin->setAccessibleName(label);
        spin->setRange(low, high);
        spin->setValue(configuration.value(key).toInt(fallback));
        form->addRow(label, spin);
        return spin;
    };
    auto *retries = field(QStringLiteral("额外读取重试"), "extraReadRetries", 0, 10, 2);
    auto *timeout = field(QStringLiteral("应答超时"), "responseTimeoutMs", 1, 60000, 1000);
    timeout->setSuffix(" ms");
    auto *gap = field(QStringLiteral("重试间隔"), "retryIntervalMs", 0, 60000, 100);
    gap->setSuffix(" ms");
    layout->addLayout(form);
    auto *status = new QLabel(QStringLiteral("只调整读取重试；设备写入不会自动重试。保留已有轮询范围与周期。"), dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *apply = new QPushButton(QStringLiteral("应用读取配置"), dialog);
    layout->addWidget(apply);
    QObject::connect(apply, &QPushButton::clicked, dialog, [access, dialog, retries, timeout, gap, status] {
        const auto lifetime = access.lifetime;
        QPointer<QDialog> alive(dialog);
        auto settings = access.config ? access.config().protocol : QJsonObject{};
        settings["extraReadRetries"] = retries->value();
        settings["responseTimeoutMs"] = timeout->value();
        settings["retryIntervalMs"] = gap->value();
        if (access.running && access.running()) {
            const auto id = access.submit ? access.submit("configure-polling", settings) : 0;
            if (alive)
                status->setText(id && access.operation ? access.operation(id).update.detail
                                                       : QStringLiteral("操作不可用或容量已满，读取配置未应用。"));
        } else {
            const bool accepted = access.configure && access.configure(settings);
            if (alive)
                status->setText(accepted ? QStringLiteral("读取配置已保存，启动后生效。")
                                         : QStringLiteral("未应用，请处理未保存修改后重试。"));
        }
    });
    dialog->show();
}
} // namespace debugger
