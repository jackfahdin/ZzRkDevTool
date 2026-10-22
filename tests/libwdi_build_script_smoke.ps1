# SPDX-License-Identifier: MIT
# 可在 Windows PowerShell 5.1 或跨平台 pwsh 中执行，不下载依赖。
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '../scripts/build-libwdi.ps1'
$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    $script, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw ($errors | Out-String) }

$testDir = Join-Path ([System.IO.Path]::GetTempPath()) ('zzrk-libwdi-test-' + [guid]::NewGuid())
$sourceDir = Join-Path $testDir 'source'
$buildDir = Join-Path $testDir 'build'
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $sourceDir 'libwdi/.msvc'), $buildDir | Out-Null
    $project = Join-Path $sourceDir 'libwdi/.msvc/libwdi_dll.vcxproj'
    [System.IO.File]::WriteAllText($project, '<Project />')
    # 哈希错误必须在执行任何 MSI、构建或修改源码前失败。
    [System.IO.File]::WriteAllText((Join-Path $buildDir 'wdfcoinstaller.msi'), 'invalid cached payload')
    $hash = (Get-FileHash -LiteralPath $project).Hash
    $failedAsExpected = $false
    try {
        & $script -SourceDir $sourceDir -BuildDir $buildDir -MSBuildPath $script
    } catch {
        if ($_.Exception.Message -notlike '*SHA256 mismatch*') { throw }
        $failedAsExpected = $true
    }
    if (-not $failedAsExpected) { throw 'An invalid cached MSI was accepted.' }
    if ((Get-FileHash -LiteralPath $project).Hash -ne $hash) {
        throw 'The immutable source was modified before resource verification.'
    }
    if (Test-Path -LiteralPath (Join-Path $buildDir 'output/libwdi.dll')) {
        throw 'A failed dependency build left a successful output.'
    }
    $failedAsExpected = $false
    try {
        & $script -SourceDir $sourceDir -BuildDir (Join-Path $sourceDir 'nested') -MSBuildPath $script
    } catch {
        if ($_.Exception.Message -notlike '*outside the immutable source*') { throw }
        $failedAsExpected = $true
    }
    if (-not $failedAsExpected) { throw 'A build inside the source tree was accepted.' }
    Write-Output 'PowerShell syntax, corrupt-resource rejection, source isolation: passed'
} finally {
    if (Test-Path -LiteralPath $testDir) { Remove-Item -LiteralPath $testDir -Recurse -Force }
}
