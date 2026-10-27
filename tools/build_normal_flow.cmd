@echo off
call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
cl /nologo /c /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\normal_flow\obj\ @build\normal_flow\sources.rsp
if errorlevel 1 exit /b 1
lib /nologo /OUT:build\normal_flow\engine.lib @build\normal_flow\objects.rsp
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /MT /fp:precise /W4 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Fobuild\normal_flow\normal_flow_check.obj /Febuild\normal_flow\normal_flow_check.exe src\simulation\normal_flow_check.cpp build\normal_flow\engine.lib /link /STACK:8388608 /PDB:build\normal_flow\normal_flow_check.pdb
exit /b %errorlevel%
