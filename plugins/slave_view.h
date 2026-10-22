#pragma once
#include "debugger/sdk.h"
#include <QVBoxLayout>
namespace debugger {
QWidget *createSlaveView(SessionAccess access, QWidget *parent);
void addSlaveEditor(SessionAccess access, QWidget *view, QVBoxLayout *layout);
}
