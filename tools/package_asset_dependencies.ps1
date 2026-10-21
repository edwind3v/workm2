# Run after building GenerateAssetDependencies.vcxproj in Release/Win32.
$ErrorActionPreference = 'Stop'
$assetRepo = Split-Path -Parent $PSScriptRoot
$assetStage = Join-Path $assetRepo 'bin\asset-dependencies-tool\portable'
$assetExe = Join-Path $assetRepo 'bin\asset-dependencies-tool\GenerateAssetDependencies.exe'
$assetDll = Join-Path (Split-Path -Parent $assetRepo) 'Extern\dll\granny2.11.8.0.dll'
if (!(Test-Path -LiteralPath $assetExe -PathType Leaf)) { throw 'Build the Release/Win32 generator first.' }
if (!(Test-Path -LiteralPath $assetDll -PathType Leaf)) { throw 'Missing matching Granny 2.11.8.0 runtime.' }
New-Item -ItemType Directory -Path $assetStage -Force | Out-Null
Copy-Item -LiteralPath $assetExe -Destination (Join-Path $assetStage 'GenerateAssetDependencies.exe') -Force
Copy-Item -LiteralPath $assetDll -Destination (Join-Path $assetStage 'granny2.dll') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'GenerateAssetDependencies.cmd') -Destination $assetStage -Force
Copy-Item -LiteralPath (Join-Path $assetRepo 'docs\asset-dependencies-generator.md') -Destination (Join-Path $assetStage 'README.md') -Force
$assetContents = @('GenerateAssetDependencies.exe', 'GenerateAssetDependencies.cmd', 'granny2.dll', 'README.md') | ForEach-Object { Join-Path $assetStage $_ }
$assetZip = Join-Path $PSScriptRoot 'asset_dependencies_portable.zip'
Compress-Archive -LiteralPath $assetContents -DestinationPath $assetZip -Force
Write-Output "Portable bundle: $assetZip"
