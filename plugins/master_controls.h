#pragma once
#include "debugger/sdk.h"
namespace debugger {
void showReadSettings(SessionAccess, QWidget *);
QWidget *createAdvancedMasterView(SessionAccess, QWidget *);
} // namespace debugger
