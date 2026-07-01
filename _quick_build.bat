@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\VSproject\SelfEngine\build
MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo
echo BUILD_EXIT=%ERRORLEVEL%
