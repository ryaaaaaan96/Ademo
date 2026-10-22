param([string]$EvidenceId = '02')
$ErrorActionPreference = 'Stop'
$cmakeExecutable = 'C:\Qt\Tools\CMake_64\bin\cmake.exe'
$ctestExecutable = 'C:\Qt\Tools\CMake_64\bin\ctest.exe'
$projectDirectory = Split-Path $PSScriptRoot -Parent
Push-Location $projectDirectory
try {
    foreach ($preset in @('qt-llvm-debug', 'qt-llvm-release')) {
        $evidenceDirectory = Join-Path $projectDirectory ".scratch/multi-protocol-debugger/evidence/$EvidenceId/$preset"
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
        Get-ChildItem -Path "build/$preset/*-results.txt" -File | Copy-Item -Destination $evidenceDirectory
        if ($testExitCode -ne 0) { throw "测试失败：$preset" }
        $artifactPaths = @(
            "build/$preset/bin/multi_protocol_debugger.exe",
            "build/$preset/bin/plugins/libmodbus_plugin.dll", "build/$preset/bin/plugins/libserial_plugin.dll"
        )
        $artifactPaths += Get-ChildItem -Path "build/$preset/bin/*tests.exe" -File |
            ForEach-Object { [IO.Path]::GetRelativePath($projectDirectory, $_.FullName).Replace('\', '/') }
        $artifactPaths += Get-ChildItem -LiteralPath $evidenceDirectory -File |
            Where-Object { $_.Name -ne 'sha256.txt' } |
            ForEach-Object { [IO.Path]::GetRelativePath($projectDirectory, $_.FullName).Replace('\', '/') }
        $hashLines = foreach ($artifactPath in $artifactPaths) {
            $artifactHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash.ToLowerInvariant()
            "$artifactHash  $artifactPath"
        }
        $hashLines | Set-Content -Encoding utf8 "$evidenceDirectory/sha256.txt"
    }
}
finally { Pop-Location }
