param([switch]$Capture, [switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$prototypeRoot = $PSScriptRoot
$qtRoot = 'C:/Qt/6.12.0/llvm-mingw_64'
$compilerRoot = 'C:/Qt/Tools/llvm-mingw2217_64/bin'
$cmake = 'C:/Qt/Tools/CMake_64/bin/cmake.exe'
$oldPath = $env:PATH
try {
    $env:PATH = "$qtRoot/bin;$compilerRoot;C:/Qt/Tools/Ninja;$oldPath"
    & $cmake -S $prototypeRoot -B "$prototypeRoot/build" -G Ninja "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_CXX_COMPILER=$compilerRoot/clang++.exe" "-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe" "-DCMAKE_PREFIX_PATH=$qtRoot"
    if ($LASTEXITCODE -ne 0) { throw '原型配置失败' }
    & $cmake --build "$prototypeRoot/build" --parallel 4
    if ($LASTEXITCODE -ne 0) { throw '原型构建失败' }
    if ($BuildOnly) { return }
    $app = "$prototypeRoot/build/debugger_ui_prototype.exe"
    if ($Capture) {
        $oldPlatform = $env:QT_QPA_PLATFORM
        $oldBackend = $env:QT_QUICK_BACKEND
        try {
            $env:QT_QPA_PLATFORM = 'offscreen'
            $env:QT_QUICK_BACKEND = 'software'
            & $app --capture
            if ($LASTEXITCODE -ne 0) { throw '原型截图运行失败' }
        } finally { $env:QT_QPA_PLATFORM = $oldPlatform; $env:QT_QUICK_BACKEND = $oldBackend }
    } else {
        # 用户直接操作的原型窗口需要可见；没有后台服务。
        Start-Process -FilePath $app -WorkingDirectory $prototypeRoot
    }
} finally { $env:PATH = $oldPath }
