param([ValidateSet('qt-llvm-debug', 'qt-llvm-release')][string]$Preset = 'qt-llvm-debug')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
$previousPath = $env:PATH
try {
    $env:PATH = "C:\Qt\6.12.0\llvm-mingw_64\bin;C:\Qt\Tools\llvm-mingw2217_64\bin;$previousPath"
    & (Join-Path $projectDirectory "build/$Preset/bin/multi_protocol_debugger.exe")
}
finally { $env:PATH = $previousPath }
