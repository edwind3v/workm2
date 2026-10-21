param(
    [ValidateSet('sampled', 'light', 'full', 'compare')][string]$Mode = 'compare',
    [string]$ClientDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) '..\..\..\..\Game Client')
)

$crowdClientDirectory = (Resolve-Path -LiteralPath $ClientDirectory -ErrorAction Stop).Path
$crowdExecutable = Join-Path $crowdClientDirectory 'metin2client.exe'
if (-not (Test-Path -LiteralPath $crowdExecutable -PathType Leaf)) {
    throw "Client executable missing: $crowdExecutable"
}
if (Get-Process -Name metin2client -ErrorAction SilentlyContinue) {
    throw 'Close the running client before starting this comparison so sessions do not share syserr.txt.'
}
$crowdPreviousMode = $env:M2_MAP_PROFILE
$crowdOutputDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin\cpu_profiles'
New-Item -ItemType Directory -Path $crowdOutputDirectory -Force | Out-Null
$crowdOutputBase = Join-Path $crowdOutputDirectory ('cpu_{0}_{1}' -f $Mode, (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
[ordered]@{
    mode = $Mode
    started_utc = [DateTime]::UtcNow.ToString('o')
    executable = $crowdExecutable
    sha256 = (Get-FileHash -LiteralPath $crowdExecutable -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath "$crowdOutputBase.json" -Encoding UTF8
try {
    $env:M2_MAP_PROFILE = $Mode
    Write-Host "Starting $Mode capture. Keep the same camera and population for at least 30 seconds in each crowded area."
    Write-Host 'Compare mode alternates full and light profiling every five seconds. Exit the game to save the log.'
    # This is the interactive game window the user will control for comparison.
    Start-Process -FilePath $crowdExecutable -WorkingDirectory $crowdClientDirectory -WindowStyle Normal -Wait
    $crowdLog = Join-Path $crowdClientDirectory 'syserr.txt'
    if (Test-Path -LiteralPath $crowdLog -PathType Leaf) {
        Copy-Item -LiteralPath $crowdLog -Destination "$crowdOutputBase.syserr.txt"
        Write-Host "Saved: $crowdOutputBase.syserr.txt"
    }
}
finally {
    $env:M2_MAP_PROFILE = $crowdPreviousMode
}
