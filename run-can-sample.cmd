@echo off
setlocal
set "SAMPLE_EXE=%~dp0build\qt-llvm-release\bin\can_sample_source.exe"
set "QT_ROOT=C:\Qt\6.12.0\llvm-mingw_64"
set "LLVM_BIN=C:\Qt\Tools\llvm-mingw2217_64\bin"
if not exist "%SAMPLE_EXE%" (
  echo Release CAN sample executable is missing. Build qt-llvm-release first.
  pause
  exit /b 1
)
if not exist "%QT_ROOT%\bin\Qt6SerialBus.dll" (
  echo Required local Qt SerialBus runtime is missing. See README.md.
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
echo Virtual CAN software sample only. Start the receiver on the same channel first.
if "%~1"=="" (
  "%SAMPLE_EXE%" --interface can0 --count 200 --interval 250
) else (
  "%SAMPLE_EXE%" %*
)
set "SAMPLE_RESULT=%ERRORLEVEL%"
echo Sample finished with exit code %SAMPLE_RESULT%.
if "%~1"=="" pause
exit /b %SAMPLE_RESULT%
