# SPDX-License-Identifier: MIT
param([Parameter(Mandatory = $true)][string]$SourceDir)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../scripts/patch-libwdi.ps1')
$temp = Join-Path ([IO.Path]::GetTempPath()) ('zzrk-wdi-patch-' + [guid]::NewGuid())
try {
    $files = @('libwdi/libwdi.c', 'libwdi/.msvc/libwdi_dll.vcxproj')
    $hashes = @{}
    foreach ($file in $files) {
        $original = Join-Path $SourceDir $file
        $copy = Join-Path $temp $file
        $hashes[$file] = (Get-FileHash -LiteralPath $original).Hash
        New-Item -ItemType Directory -Force -Path (Split-Path $copy) | Out-Null
        Copy-Item -LiteralPath $original -Destination $copy
    }
    Set-ZzrkLibwdiProtection $temp
    $code = [IO.File]::ReadAllText((Join-Path $temp 'libwdi/libwdi.c'))
    if ($code.Contains('sid = GetSid();') -or
        [regex]::Matches($code, 'ConvertStringSidToSidA\("S-1-5-32-544", &sid\)').Count -ne 2 -or
        -not $code.Contains('ConvertSidToStringSidA(GetSid(), &sid_str)')) {
        throw 'Resource owner protection or IPC user identity was not preserved.'
    }
    if (-not $code.Contains('return WDI_ERROR_ACCESS;') -or -not $code.Contains('return NULL;')) {
        throw 'The owner setup must fail closed for both resource types.'
    }
    [xml]$xml = [IO.File]::ReadAllText((Join-Path $temp 'libwdi/.msvc/libwdi_dll.vcxproj'))
    $ns = New-Object Xml.XmlNamespaceManager($xml.NameTable)
    $ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $release = @($xml.SelectNodes('//m:ItemDefinitionGroup[contains(@Condition,"Release")]/m:ClCompile/m:RuntimeLibrary', $ns))
    if ($release.Count -ne 2 -or @($release | Where-Object InnerText -ne 'MultiThreaded').Count) {
        throw 'Both Win32 and x64 Release DLLs must use the static CRT.'
    }
    foreach ($file in $files) {
        if ((Get-FileHash -LiteralPath (Join-Path $SourceDir $file)).Hash -ne $hashes[$file]) {
            throw "Immutable source changed: $file"
        }
    }
    $refused = $false
    try { Set-ZzrkLibwdiProtection $temp } catch {
        if ($_.Exception.Message -notlike '*resource-owner sites changed*') { throw }
        $refused = $true
    }
    if (-not $refused) { throw 'Changed upstream context was silently accepted.' }
    Write-Output 'Pinned source: protected resource owners, preserved IPC identity, static CRT, fail-closed patching passed'
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
