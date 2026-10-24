#pragma once
#include "debugger/sdk.h"
std::unique_ptr<debugger::ProtocolSession> createUppSession(debugger::ProtocolServices);
debugger::ParameterView *createUppView(debugger::SessionAccess, QWidget *);
