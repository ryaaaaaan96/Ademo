$ErrorActionPreference = 'Stop'
$cmakeExecutable = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctestExecutable = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$projectDirectory = Split-Path $PSScriptRoot -Parent
Push-Location $projectDirectory
try {
    foreach ($preset in @('qt-llvm-debug', 'qt-llvm-release')) {
        $evidenceDirectory = Join-Path $projectDirectory ".scratch/multi-protocol-debugger/evidence/01/$preset"
        New-Item -ItemType Directory -Force $evidenceDirectory | Out-Null
        & $cmakeExecutable --preset $preset 2>&1 | Tee-Object -FilePath "$evidenceDirectory/configure.log"
        if ($LASTEXITCODE -ne 0) { throw "配置失败：$preset" }
        & $cmakeExecutable --build --preset $preset 2>&1 | Tee-Object -FilePath "$evidenceDirectory/build.log"
        if ($LASTEXITCODE -ne 0) { throw "构建失败：$preset" }
        $previousEvidenceDirectory = $env:DEBUGGER_EVIDENCE_DIR
        try {
            $env:DEBUGGER_EVIDENCE_DIR = $evidenceDirectory
            & $ctestExecutable --preset $preset --verbose 2>&1 | Tee-Object -FilePath "$evidenceDirectory/ctest.log"
            $testExitCode = $LASTEXITCODE
        }
        finally { $env:DEBUGGER_EVIDENCE_DIR = $previousEvidenceDirectory }
        Copy-Item "build/$preset/test-results.txt" "$evidenceDirectory/test-results.txt"
        if ($testExitCode -ne 0) { throw "测试失败：$preset" }
    }
}
finally { Pop-Location }
