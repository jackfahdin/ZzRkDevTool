# SPDX-License-Identifier: MIT
# 仅对固定上游版本的构建副本调用；任何上下文变化必须人工复核，不能静默跳过保护。
function Set-ZzrkLibwdiProtection([string]$WorkSource) {
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $path = Join-Path $WorkSource 'libwdi/libwdi.c'
    $source = [System.IO.File]::ReadAllText($path)
    $needle = 'sid = GetSid();'
    if ([regex]::Matches($source, [regex]::Escape($needle)).Count -ne 2) {
        throw 'Pinned libwdi resource-owner sites changed; refusing an unreviewed installer build.'
    }
    # GetSid() 还负责 IPC 的调用者身份，不能全局替换。只修改目录和文件的创建 owner。
    $returns = @('WDI_ERROR_ACCESS', 'NULL')
    foreach ($result in $returns) {
        $index = $source.IndexOf($needle, [StringComparison]::Ordinal)
        $replacement = @"
/* ZzRkDevTool: elevated resources must never be owned by the unelevated user. */
    sid = NULL;
    if (!IsUserAnAdmin() || !ConvertStringSidToSidA("S-1-5-32-544", &sid))
        return $result;
"@
        $source = $source.Remove($index, $needle.Length).Insert($index, $replacement)
    }
    [System.IO.File]::WriteAllText($path, $source, $encoding)
    # DLL 使用静态 Release CRT，MinGW 应用部署时不再隐含依赖另一套 MSVC CRT。
    $project = Join-Path $WorkSource 'libwdi/.msvc/libwdi_dll.vcxproj'
    [xml]$xml = [System.IO.File]::ReadAllText($project)
    $ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
    $ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $nodes = @($xml.SelectNodes('//m:ItemDefinitionGroup[contains(@Condition,"Release")]/m:ClCompile/m:RuntimeLibrary', $ns))
    if ($nodes.Count -ne 2) { throw 'Pinned libwdi Release CRT configuration changed.' }
    foreach ($node in $nodes) {
        if ($node.InnerText -ne 'MultiThreadedDLL') { throw 'Unexpected libwdi CRT configuration.' }
        $node.InnerText = 'MultiThreaded'
    }
    $xml.Save($project)
}
