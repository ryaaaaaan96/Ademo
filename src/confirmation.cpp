#include "debugger/confirmation.h"
#include <QPointer>

namespace debugger {
QMessageBox::StandardButton showConfirmation(QWidget *parent, QMessageBox::Icon icon, const QString &title,
    const QString &message, QMessageBox::StandardButtons buttons, QMessageBox::StandardButton initial)
{
    // 静态便利函数使用栈上子对话框；父对象在 exec 中销毁时，Qt 必须能删除堆对象。
    QPointer<QMessageBox> dialog = new QMessageBox(icon, title, message, buttons, parent);
    dialog->setDefaultButton(initial);
    const int result = dialog->exec();
    if (!dialog) return QMessageBox::NoButton;
    delete dialog;
    return static_cast<QMessageBox::StandardButton>(result);
}
}
