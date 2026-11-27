@echo off
rem Builds 360convert.exe (tools/convert/360convert.py) with PyInstaller in its own virtual environment.
rem Output: build\360convert\360convert.exe. Run from the repository root: cmd /c tools\convert\build_360convert.cmd
setlocal
cd /d "%~dp0..\.."
if not exist build\convert_venv\Scripts\python.exe (
    py -3 -m venv build\convert_venv || exit /b 1
    build\convert_venv\Scripts\python -m pip install --quiet pyinstaller || exit /b 1
)
build\convert_venv\Scripts\python -m PyInstaller --noconfirm --clean --onefile --console --name 360convert ^
    --paths tools --paths tools\recomp --hidden-import console_sfx --hidden-import stage_console ^
    --distpath build\360convert --workpath build\360convert\work --specpath build\360convert\work ^
    tools\convert\360convert.py || exit /b 1
echo built build\360convert\360convert.exe
