[CmdletBinding()]
param(
    [string]$GameDirectory = 'E:\#111 METIN2 GRAPHICS MARTY 5.9\#MartySama 5.9 DX9Ex\Game Client',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Pml = 'C:\Windows\System32\PackMakerLite.exe',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
$packRoot = Join-Path $gameRoot 'pack'
$unpackedRoot = Join-Path $packRoot 'root'
$executable = Join-Path $sourceRoot "bin\$Configuration\metin2client.exe"
$shaderSource = Join-Path $sourceRoot 'client_pack\root\shaders'
foreach ($required in @($executable, $Pml, $unpackedRoot, $shaderSource,
    (Join-Path $packRoot 'root.eix'), (Join-Path $packRoot 'root.epk'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing required input: $required" }
}
function Assert-GameClosed {
    foreach ($process in @(Get-Process -Name metin2client -ErrorAction SilentlyContinue)) {
        if (!$process.Path -or $process.Path -eq (Join-Path $gameRoot 'metin2client.exe')) {
            throw 'Close metin2client.exe before preparing or deploying the renderer.'
        }
    }
}
Assert-GameClosed
$receiptRoot = Join-Path $sourceRoot ('bin\dx11-deployment-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if (Test-Path -LiteralPath $receiptRoot) { throw "Deployment directory already exists: $receiptRoot" }
$backup = Join-Path $receiptRoot 'original'
$stage = Join-Path $receiptRoot 'stage'
$roundtrip = Join-Path $receiptRoot 'archive-check'
New-Item -ItemType Directory -Path $backup, $stage, $roundtrip | Out-Null
foreach ($name in @('root.eix', 'root.epk')) {
    Copy-Item -LiteralPath (Join-Path $packRoot $name) -Destination (Join-Path $backup $name)
}
$liveExe = Join-Path $gameRoot 'metin2client.exe'
if (Test-Path -LiteralPath $liveExe) { Copy-Item -LiteralPath $liveExe -Destination $backup }
$liveShaders = Join-Path $unpackedRoot 'shaders'
if (Test-Path -LiteralPath $liveShaders) { Copy-Item -LiteralPath $liveShaders -Destination $backup -Recurse }
Copy-Item -LiteralPath $unpackedRoot -Destination $stage -Recurse
$stageRoot = Join-Path $stage 'root'
$stageShaders = Join-Path $stageRoot 'shaders'
New-Item -ItemType Directory -Path $stageShaders -Force | Out-Null
foreach ($file in (Get-ChildItem -LiteralPath $shaderSource -File)) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $stageShaders $file.Name) -Force
}
Push-Location -LiteralPath $stage
try {
    & $Pml --nolog -p root *> (Join-Path $receiptRoot 'pack.log')
    if ($LASTEXITCODE -ne 0) { throw "PML pack failed: $LASTEXITCODE" }
} finally { Pop-Location }
foreach ($name in @('root.eix', 'root.epk')) {
    $packed = Join-Path $stage $name
    if (!(Test-Path -LiteralPath $packed) -or (Get-Item -LiteralPath $packed).Length -eq 0) {
        throw "PML did not produce $name"
    }
    Copy-Item -LiteralPath $packed -Destination (Join-Path $roundtrip $name)
}
$inspector = Join-Path $sourceRoot 'bin\asset-preparation-v3\AssetInspector.exe'
if (Test-Path -LiteralPath $inspector) {
    $oldIndex = @(& $inspector --index (Join-Path $backup 'root.eix'))
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the original root archive index.' }
    $newIndex = @(& $inspector --index (Join-Path $stage 'root.eix'))
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the staged root archive index.' }
    $members = @{}
    foreach ($line in $newIndex) {
        if ($line.StartsWith('{')) { $row = $line | ConvertFrom-Json; $members[$row.name] = $row.type }
    }
    foreach ($line in $oldIndex) {
        if (!$line.StartsWith('{')) { continue }
        $row = $line | ConvertFrom-Json
        if (!$members.ContainsKey($row.name) -or $members[$row.name] -ne $row.type) {
            throw "Root member missing or packing policy changed: $($row.name)"
        }
    }
}
Push-Location -LiteralPath $roundtrip
try {
    & $Pml --nolog -u root *> (Join-Path $receiptRoot 'archive-check.log')
    if ($LASTEXITCODE -ne 0) { throw "PML archive extraction failed: $LASTEXITCODE" }
} finally { Pop-Location }
$files = @()
foreach ($file in (Get-ChildItem -LiteralPath $stageRoot -File -Recurse)) {
    $relative = $file.FullName.Substring($stageRoot.Length + 1)
    $extracted = Join-Path (Join-Path $roundtrip 'root') $relative
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    if (!(Test-Path -LiteralPath $extracted) -or
        (Get-FileHash -LiteralPath $extracted -Algorithm SHA256).Hash -ne $hash) {
        throw "Packed file differs from source: $relative"
    }
    $files += [ordered]@{ path = $relative; sha256 = $hash }
}
$receipt = [ordered]@{
    game = $gameRoot; configuration = $Configuration; deployed = $false
    executable = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    root_eix = (Get-FileHash -LiteralPath (Join-Path $stage 'root.eix') -Algorithm SHA256).Hash
    root_epk = (Get-FileHash -LiteralPath (Join-Path $stage 'root.epk') -Algorithm SHA256).Hash
    files = $files
}
if ($Deploy) {
    Assert-GameClosed
    try {
        New-Item -ItemType Directory -Path $liveShaders -Force | Out-Null
        foreach ($file in (Get-ChildItem -LiteralPath $shaderSource -File)) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $liveShaders $file.Name) -Force
        }
        foreach ($name in @('root.eix', 'root.epk')) {
            Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $packRoot $name) -Force
        }
        Copy-Item -LiteralPath $executable -Destination $liveExe -Force
        $receipt.deployed = $true
    } catch {
        foreach ($name in @('root.eix', 'root.epk')) {
            Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $packRoot $name) -Force
        }
        if (Test-Path -LiteralPath (Join-Path $backup 'metin2client.exe')) {
            Copy-Item -LiteralPath (Join-Path $backup 'metin2client.exe') -Destination $liveExe -Force
        }
        throw
    }
}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $receiptRoot 'receipt.json') -Encoding UTF8
Write-Output "DX11 package ready: $receiptRoot"
Write-Output "Deployed: $($receipt.deployed); root members checked: $($files.Count)"
