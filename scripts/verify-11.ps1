$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
Push-Location $projectDirectory
try {
    foreach ($preset in @('qt-llvm-debug', 'qt-llvm-release')) {
        $evidenceDirectory = Join-Path $projectDirectory ".scratch/multi-protocol-debugger/evidence/11/$preset"
        New-Item -ItemType Directory -Force $evidenceDirectory | Out-Null
        & 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --preset $preset 2>&1 | Tee-Object -FilePath "$evidenceDirectory/configure.log"
        if ($LASTEXITCODE -ne 0) { throw "配置失败：$preset" }
        & 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --build --preset $preset 2>&1 | Tee-Object -FilePath "$evidenceDirectory/build.log"
        if ($LASTEXITCODE -ne 0) { throw "构建失败：$preset" }
        $previousEvidenceDirectory = $env:DEBUGGER_EVIDENCE_DIR
        try {
            $env:DEBUGGER_EVIDENCE_DIR = $evidenceDirectory
            & 'C:/Qt/Tools/CMake_64/bin/ctest.exe' --preset $preset --verbose 2>&1 | Tee-Object -FilePath "$evidenceDirectory/ctest.log"
            if ($LASTEXITCODE -ne 0) { throw "测试失败：$preset" }
        } finally { $env:DEBUGGER_EVIDENCE_DIR = $previousEvidenceDirectory }
        foreach ($resultFile in @('test-results.txt', 'read-results.txt', 'trace-results.txt')) {
            Copy-Item "build/$preset/$resultFile" "$evidenceDirectory/$resultFile"
        }
        $paths = @("build/$preset/bin/multi_protocol_debugger.exe", "build/$preset/bin/trace_tests.exe",
            "build/$preset/bin/plugins/libmodbus_plugin.dll", "build/$preset/bin/plugins/libserial_plugin.dll")
        $paths += Get-ChildItem -LiteralPath $evidenceDirectory -File |
            Where-Object { $_.Name -ne 'sha256.txt' } |
            ForEach-Object { [IO.Path]::GetRelativePath($projectDirectory, $_.FullName).Replace('\', '/') }
        $lines = foreach ($path in $paths) {
            "$( (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant())  $path"
        }
        $lines | Set-Content -Encoding utf8 "$evidenceDirectory/sha256.txt"
    }
} finally { Pop-Location }
