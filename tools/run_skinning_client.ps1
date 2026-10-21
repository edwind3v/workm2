param(
    [ValidateSet('gpu', 'cpu')][string]$Mode = 'gpu',
    [string]$ClientDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) '..\..\..\..\Game Client'),
    [string]$Scenario = 'manual'
)

$skinClientPath = (Resolve-Path -LiteralPath $ClientDirectory -ErrorAction Stop).Path
$skinExecutable = Join-Path $skinClientPath 'metin2client.exe'
if (-not (Test-Path -LiteralPath $skinExecutable -PathType Leaf)) {
    throw "Client executable missing: $skinExecutable"
}
$skinPreviousMode = $env:M2_GPU_SKINNING
$skinPreviousPreview = $env:M2_DX11_MODEL_PREVIEW
$skinPreviousProfile = $env:M2_MAP_PROFILE
$skinProfileDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\skinning_profiles'
New-Item -ItemType Directory -Path $skinProfileDirectory -Force | Out-Null
$skinSession = 'skin_{0}_{1}' -f $Mode, (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
$skinProfileBase = Join-Path $skinProfileDirectory $skinSession
$skinMetadata = [ordered]@{
    mode = $Mode
    scenario = $Scenario
    started_utc = [DateTime]::UtcNow.ToString('o')
    executable = $skinExecutable
    sha256 = (Get-FileHash -LiteralPath $skinExecutable -Algorithm SHA256).Hash
    model_preview = 0
    map_profile = 1
}
$skinMetadata | ConvertTo-Json | Set-Content -LiteralPath "$skinProfileBase.json" -Encoding UTF8
try {
    $env:M2_GPU_SKINNING = if ($Mode -eq 'gpu') { '1' } else { '0' }
    $env:M2_DX11_MODEL_PREVIEW = '0'
    $env:M2_MAP_PROFILE = '1'
    Write-Host "Running $Mode skinning. Exit the client to save this session's log."
    Start-Process -FilePath $skinExecutable -WorkingDirectory $skinClientPath -WindowStyle Normal -Wait
    $skinLog = Join-Path $skinClientPath 'syserr.txt'
    if (Test-Path -LiteralPath $skinLog -PathType Leaf) {
        Copy-Item -LiteralPath $skinLog -Destination "$skinProfileBase.syserr.txt"
        Write-Host "Saved profile: $skinProfileBase.syserr.txt"
    }
}
finally {
    $env:M2_GPU_SKINNING = $skinPreviousMode
    $env:M2_DX11_MODEL_PREVIEW = $skinPreviousPreview
    $env:M2_MAP_PROFILE = $skinPreviousProfile
}
