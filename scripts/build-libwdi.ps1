# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [ValidateSet('Win32', 'x64')][string]$Platform = 'x64',
    [string]$MSBuildPath = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRevision = '9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602'
$wdkUrl = 'https://download.microsoft.com/download/0/5/F/05FD6919-6250-425B-86ED-9B095E54065A/wdfcoinstaller.msi'
$wdkHash = '29314207814ce9d5d73695f7e9239539cf37c79e750b9d5ea5a5ef5487a583d6'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-Utf8([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, $utf8)
}

# Ninja/MinGW 没有 CMAKE_VS_MSBUILD_COMMAND；从 VS2022+ 中选择实际包含 v143 的实例。
if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'VS 2022 or newer Build Tools with MSVC v143 (x86/x64) and Windows SDK are required; vswhere.exe was not found.'
    }
    $installations = @(& $vswhere -all -sort -version '[17.0,)' -products '*' -property installationPath)
    if ($LASTEXITCODE -ne 0) { throw 'vswhere failed while enumerating Visual Studio installations.' }
    foreach ($installation in $installations) {
        $toolsDir = Join-Path $installation 'VC/Tools/MSVC'
        $candidate = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
        if (-not (Test-Path -LiteralPath $toolsDir) -or -not (Test-Path -LiteralPath $candidate)) { continue }
        # v143 的工具目录版本为 14.30--14.4x；VS2026 默认工具不等于 v143。
        $v143 = @(Get-ChildItem -LiteralPath $toolsDir -Directory | Where-Object {
            $version = $null
            [version]::TryParse($_.Name, [ref]$version) -and
                $version.Major -eq 14 -and $version.Minor -ge 30 -and $version.Minor -lt 50 -and
                (Test-Path -LiteralPath (Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe')) -and
                (Test-Path -LiteralPath (Join-Path $_.FullName 'bin/Hostx64/x86/cl.exe'))
        })
        if ($v143.Count -gt 0) { $MSBuildPath = $candidate; break }
    }
    if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
        throw 'vswhere found no MSBuild installation with the v143 x86/x64 toolset. Add MSVC v143 and Windows SDK in Visual Studio Installer, or set ZZRK_LIBWDI_MSBUILD.'
    }
}
if (-not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw "MSBuild does not exist: $MSBuildPath"
}
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
if ($BuildDir.StartsWith($SourceDir + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase) -or $BuildDir -eq $SourceDir) {
    throw 'BuildDir must be outside the immutable source directory.'
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir 'libwdi/.msvc/libwdi_dll.vcxproj'))) {
    throw 'SourceDir must contain the pinned libwdi v1.5.1 sources.'
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# MSI 仅作管理解压（/a），不执行系统安装；先校验固定摘要。
$msiPath = Join-Path $BuildDir 'wdfcoinstaller.msi'
if (-not (Test-Path -LiteralPath $msiPath)) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $wdkUrl -OutFile $msiPath
}
if ((Get-FileHash -LiteralPath $msiPath -Algorithm SHA256).Hash -ne $wdkHash) {
    throw "WDF redistributable SHA256 mismatch; remove $msiPath and retry."
}
$wdkExtract = Join-Path $BuildDir 'wdk'
$wdkDir = Join-Path $wdkExtract 'Windows Kits/8.0'
if (-not (Test-Path -LiteralPath (Join-Path $wdkDir 'redist/wdf/x64/WdfCoInstaller01011.dll'))) {
    $msiArguments = '/a "{0}" /qn TARGETDIR="{1}" /l*v "{2}"' -f `
        $msiPath, $wdkExtract, (Join-Path $BuildDir 'wdk-extract.log')
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList $msiArguments -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "WDF administrative extraction failed ($($process.ExitCode)); see wdk-extract.log."
    }
}
foreach ($arch in @('x86', 'x64')) {
    foreach ($name in @('WdfCoInstaller01011.dll', 'winusbcoinstaller2.dll')) {
        $file = Join-Path $wdkDir "redist/wdf/$arch/$name"
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Required WinUSB resource is missing: $file"
        }
    }
}

# 所有适配只改构建副本，保留源包及上游版权。隔离路径还避免多配置竞争。
$workSource = Join-Path $BuildDir 'source'
New-Item -ItemType Directory -Force -Path $workSource | Out-Null
Get-ChildItem -LiteralPath $SourceDir -Force | Where-Object Name -ne '.git' |
    Copy-Item -Destination $workSource -Recurse -Force
. (Join-Path $PSScriptRoot 'patch-libwdi.ps1')
Set-ZzrkLibwdiProtection $workSource
$wdkCPath = $wdkDir.Replace('\', '/')
$sourceCPath = $workSource.Replace('\', '/')
Write-Utf8 (Join-Path $workSource 'msvc/config.h') @"
/* ZzRkDevTool 构建配置：仅嵌入 WinUSB，不依赖 libusb0/libusbK/ARM64。 */
#define SOLUTIONDIR "$sourceCPath"
#define WDK_DIR "$wdkCPath"
#define WDF_VER 1011
#define COINSTALLER_DIR "wdf"
#define X64_DIR "x64"
#define OPT_M32
#define OPT_M64
#define INCLUDE_DEBUG_LOGGING
#define ENABLE_LOGGING 1
"@
if ($Platform -eq 'x64') {
    Write-Utf8 (Join-Path $workSource 'libwdi/build64.h') "#define BUILD64`n"
} else {
    Write-Utf8 (Join-Path $workSource 'libwdi/build64.h') "#define NO_BUILD64`n"
}

# 上游跨平台 ProjectReference 不适合直接按 DLL 平台构建：embedder 只有 Win32。
# 手工按顺序调用原始工程，移除副本的引用，避免递归覆盖 build64.h 或要求 ARM64。
foreach ($project in @('embedder', 'libwdi_dll')) {
    $projectPath = Join-Path $workSource "libwdi/.msvc/$project.vcxproj"
    [xml]$xml = Get-Content -LiteralPath $projectPath -Raw
    $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    foreach ($reference in @($xml.SelectNodes('//m:ProjectReference', $ns))) {
        [void]$reference.ParentNode.RemoveChild($reference)
    }
    if ($project -eq 'libwdi_dll') {
        foreach ($event in @($xml.SelectNodes('//m:PreBuildEvent/m:Command', $ns))) {
            $event.InnerText = 'cd /d "$(ProjectDir)\.."' + "`r`n" + '.\embedder.exe embedded.h'
        }
    }
    $xml.Save($projectPath)
}

function Invoke-LibwdiBuild([string]$Project, [string]$TargetPlatform) {
    $projectPath = Join-Path $workSource "libwdi/.msvc/$Project.vcxproj"
    # SolutionDir 原工程与嵌入器均依赖它；尾部 / 避免命令行引号与反斜杠交互。
    $solutionRoot = $workSource.Replace('\', '/') + '/'
    & $MSBuildPath $projectPath /nologo /m:1 /t:Build `
        '/p:Configuration=Release' "/p:Platform=$TargetPlatform" `
        '/p:PlatformToolset=v143' "/p:SolutionDir=$solutionRoot"
    if ($LASTEXITCODE -ne 0) {
        throw "libwdi $Project ($TargetPlatform) failed: $LASTEXITCODE"
    }
}
Invoke-LibwdiBuild 'installer_x86' 'Win32'
Invoke-LibwdiBuild 'installer_x64' 'x64'
Invoke-LibwdiBuild 'embedder' 'Win32'
Invoke-LibwdiBuild 'libwdi_dll' $Platform

$dllPath = Join-Path $workSource "$Platform/Release/dll/libwdi.dll"
$embeddedPath = Join-Path $workSource 'libwdi/embedded.h'
if (-not (Test-Path -LiteralPath $dllPath) -or -not (Test-Path -LiteralPath $embeddedPath)) {
    throw 'libwdi build did not produce libwdi.dll and embedded.h.'
}
$outputDir = Join-Path $BuildDir 'output'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
# 官方重分发包的许可与清单随应用一起分发。
$license = @(Get-ChildItem -LiteralPath $wdkExtract -Filter License.rtf -Recurse)
$redist = @(Get-ChildItem -LiteralPath $wdkExtract -Filter redist.txt -Recurse)
if ($license.Count -eq 0 -or $redist.Count -eq 0) {
    throw 'WDF redistributable license/redist list was not found; refusing to package an incomplete dependency.'
}
Copy-Item -LiteralPath $license[0].FullName -Destination (Join-Path $outputDir 'WDF-License.rtf') -Force
$redistText = ($redist | Sort-Object FullName | ForEach-Object {
    [System.IO.File]::ReadAllText($_.FullName)
}) -join "`r`n"
Write-Utf8 (Join-Path $outputDir 'WDF-redist.txt') $redistText
Write-Utf8 (Join-Path $outputDir 'BUILD-INFO.txt') @"
libwdi v1.5.1 ($sourceRevision)
https://github.com/pbatard/libwdi/tree/$sourceRevision
MSBuild: $MSBuildPath
Toolset: v143; Configuration: Release; DLL platform: $Platform
Adaptations: Release DLL uses /MT; extracted resources are owned by Administrators from creation.
Resources: architecture-appropriate upstream installers, WinUSB INF/CAT templates, WDF 1.11 coinstallers.
WDF redistributable: $wdkUrl
WDF SHA256: $wdkHash
The original source and the build recipe are included alongside this file.
"@
# 最后写主产物：依赖构建失败时不留下可被 CMake 当成成功的输出。
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $outputDir 'libwdi.dll') -Force
