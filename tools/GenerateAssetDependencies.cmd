@echo off
setlocal
"%~dp0GenerateAssetDependencies.exe" %*
set "asset_dependency_result=%errorlevel%"
echo.
if not "%asset_dependency_result%"=="0" echo Generation failed. Your previous manifest has not been replaced.
pause
exit /b %asset_dependency_result%
