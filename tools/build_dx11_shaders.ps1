[CmdletBinding()]
param([string]$Fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $sourceRoot 'bin\dx11-shaders'
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (!(Test-Path -LiteralPath $Fxc)) { throw 'Supply the Windows SDK fxc.exe path with -Fxc.' }
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'client_pack\root\shaders') -Filter '*.hlsl')) {
    $profile = if ($file.BaseName.EndsWith('_cs')) { 'cs_5_0' } elseif ($file.BaseName.EndsWith('_vs')) { 'vs_5_0' } else { 'ps_5_0' }
    $entry = if ($profile -eq 'cs_5_0') { 'CSMain' } else { 'main' }
    & $Fxc /nologo /O3 /T $profile /E $entry /Fo (Join-Path $output ($file.BaseName + '.cso')) $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Shader build failed: $($file.Name)" }
}
Write-Output "Shaders compiled to $output. The client loads the HLSL files from pack/root/shaders."
