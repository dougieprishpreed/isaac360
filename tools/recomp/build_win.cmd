@echo off
rem Builds build\recomp\isaac360_recomp.exe (x64, clang-cl, Ninja, two jobs). Run tools\recomp\translate.py first.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
if errorlevel 1 exit /b 1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
cmake -S src\recomp -B build\recomp\cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl >nul
if errorlevel 1 exit /b 1
cmake --build build\recomp\cmake -j 2
exit /b %errorlevel%
