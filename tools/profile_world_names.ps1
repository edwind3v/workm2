param(
    [Parameter(Mandatory = $true)][string]$Client,
    [ValidateSet('cached', 'tutorial')][string]$Renderer = 'cached',
    [string]$Scenario = 'manual',
    [ValidateRange(0, 100000)][int]$EntityCount = 200
)

$ErrorActionPreference = 'Stop'
$clientFile = Get-Item -LiteralPath $Client
if ($clientFile.PSIsContainer -or $clientFile.Extension -ne '.exe') {
    throw 'Client must identify a client executable.'
}
$clientDirectory = $clientFile.Directory.FullName
$sessionId = Get-Date -Format 'yyyyMMdd_HHmmss'
$sessionFile = Join-Path $clientDirectory "text_profile_session_$sessionId.txt"
$fingerprint = Get-FileHash -LiteralPath $clientFile.FullName -Algorithm SHA256
@(
    "Session: $sessionId"
    "Client: $($clientFile.FullName)"
    "SHA256: $($fingerprint.Hash)"
    "Renderer: $Renderer"
    "Scenario: $Scenario"
    "Expected entities (metadata only): $EntityCount"
    'Visibility: controlled by existing in-game settings; this launcher does not change it.'
    'Output: text_performance.txt in the client working directory; summaries append every five seconds.'
) | Set-Content -LiteralPath $sessionFile -Encoding UTF8

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $clientFile.FullName
$startInfo.WorkingDirectory = $clientDirectory
$startInfo.UseShellExecute = $false
$startInfo.EnvironmentVariables['METIN2_TEXT_PROFILE'] = '1'
$startInfo.EnvironmentVariables['METIN2_TEXT_RENDERER'] = $Renderer
$startInfo.EnvironmentVariables['METIN2_TEXT_PROFILE_SESSION'] = $sessionId
foreach ($diagnosticVariable in @('M2_DX11_TEST', 'M2_DX11_ASSET_PATH',
    'M2_DX11_SHARED_PREVIEW', 'M2_DX11_MODEL_PREVIEW', 'M2_DX11_MODEL_TARGET',
    'M2_DX11_CHARACTER_COMPOSITE')) {
    $startInfo.EnvironmentVariables.Remove($diagnosticVariable)
}
$clientProcess = [System.Diagnostics.Process]::Start($startInfo)
Write-Output "Started client PID $($clientProcess.Id). Session details: $sessionFile"
Write-Output 'EntityCount is a recording label. Spawn the desired entities using your usual in-game tools.'
