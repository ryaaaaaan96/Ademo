@echo off
setlocal
set "APP_EXE=%~dp0build\qt-llvm-release\bin\multi_protocol_debugger.exe"
set "QT_ROOT=C:\Qt\6.12.0\llvm-mingw_64"
set "LLVM_BIN=C:\Qt\Tools\llvm-mingw2217_64\bin"
if not exist "%APP_EXE%" (
  echo Release executable is missing:
  echo "%APP_EXE%"
  echo Build the qt-llvm-release preset first. See README.md.
  pause
  exit /b 1
)
if not exist "%QT_ROOT%\bin\Qt6Core.dll" (
  echo Required local Qt 6.12.0 LLVM-MinGW runtime is missing. See README.md.
  pause
  exit /b 1
)
if not exist "%LLVM_BIN%\libc++.dll" (
  echo Required local LLVM-MinGW runtime is missing. See README.md.
  pause
  exit /b 1
)
set "PATH=%QT_ROOT%\bin;%LLVM_BIN%;%PATH%"
set "QT_PLUGIN_PATH=%QT_ROOT%\plugins"
set "QML_IMPORT_PATH=%QT_ROOT%\qml"
start "" /d "%~dp0" "%APP_EXE%"
endlocal
