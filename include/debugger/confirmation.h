#pragma once
#include <QMessageBox>

namespace debugger {
// GUI线程；父对象可在模态循环中销毁。返回后调用方仍须检查自身 QPointer。
QMessageBox::StandardButton showConfirmation(QWidget *, QMessageBox::Icon, const QString &title,
    const QString &message, QMessageBox::StandardButtons, QMessageBox::StandardButton initial);
}
