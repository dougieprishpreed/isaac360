@echo off
call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
cl /nologo /O2 /EHsc /MT /fp:precise /W3 /D_CRT_SECURE_NO_WARNINGS /Ibuild\generated /Isrc\game /Isrc\content /Isrc\win /Febuild\windows\boi360.exe @build\windows\sources.rsp /link d3d9.lib user32.lib gdi32.lib /STACK:8388608
exit /b %errorlevel%
