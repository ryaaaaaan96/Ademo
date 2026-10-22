$ErrorActionPreference = 'Stop'
$cmakeExecutable = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctestExecutable = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
Push-Location $PSScriptRoot
try {
    foreach ($preset in @('qt-llvm-debug', 'qt-llvm-release')) {
        & $cmakeExecutable --preset $preset 2>&1 | Tee-Object -FilePath "$preset-configure.log"
        if ($LASTEXITCODE -ne 0) { throw "Configure failed: $preset" }
        & $cmakeExecutable --build --preset $preset 2>&1 | Tee-Object -FilePath "$preset-build.log"
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $preset" }
        & $ctestExecutable --preset $preset --verbose 2>&1 | Tee-Object -FilePath "$preset-test.log"
        if ($LASTEXITCODE -ne 0) { throw "Runtime check failed: $preset" }
    }
}
finally {
    Pop-Location
}
