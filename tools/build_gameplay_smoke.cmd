@echo off
rem MSVC 2022 x86 (SSE2 floats, as IEEE as the console's GCC); ISAAC360_HOST_VC=2010 builds with the retired
rem VS2010 toolchain for an A/B of the simulation results.
if "%ISAAC360_HOST_VC%"=="2010" (call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" x86 >nul) else (call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>nul)
if errorlevel 1 exit /b 1
set CL=
set _CL_=
cl /nologo /c /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\gameplay\obj\ @build\gameplay\sources.rsp
if errorlevel 1 exit /b 1
lib /nologo /OUT:build\gameplay\engine.lib @build\gameplay\objects.rsp
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\gameplay\gameplay_smoke.obj /Febuild\gameplay\gameplay_smoke.exe src\simulation\gameplay_smoke.cpp build\gameplay\engine.lib /link /STACK:8388608
exit /b %errorlevel%
