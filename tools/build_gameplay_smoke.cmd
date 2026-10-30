@echo off
call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
cl /nologo /c /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\gameplay\obj\ @build\gameplay\sources.rsp
if errorlevel 1 exit /b 1
lib /nologo /OUT:build\gameplay\engine.lib @build\gameplay\objects.rsp
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\gameplay\gameplay_smoke.obj /Febuild\gameplay\gameplay_smoke.exe src\simulation\gameplay_smoke.cpp build\gameplay\engine.lib /link /STACK:8388608
exit /b %errorlevel%
