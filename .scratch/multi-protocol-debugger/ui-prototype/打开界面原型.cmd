@echo off
setlocal
set "PATH=C:\Qt\6.12.0\llvm-mingw_64\bin;C:\Qt\Tools\llvm-mingw2217_64\bin;%PATH%"
if not exist "%~dp0build\debugger_ui_prototype.exe" (
  echo Prototype executable not built. Please build this prototype first.
  pause
  exit /b 1
)
start "" /D "%~dp0" "%~dp0build\debugger_ui_prototype.exe"
endlocal
