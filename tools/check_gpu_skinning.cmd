@echo off
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "M2_SKIN_VS=%%i"
if not defined M2_SKIN_VS exit /b 1
call "%M2_SKIN_VS%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x86
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /MD /I"..\Extern\include" tools\check_gpu_skinning.cpp /Fo"bin\check_gpu_skinning.obj" /Fe"bin\check_gpu_skinning.exe" /link /LIBPATH:"..\Extern\lib" d3d9.lib d3dx9.lib d3dcompiler.lib granny2.11.8.0.lib user32.lib
if errorlevel 1 exit /b 1
set "PATH=%CD%\..\..\..\..\Game Client;%PATH%"
bin\check_gpu_skinning.exe
exit /b %errorlevel%
