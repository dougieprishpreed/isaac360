param()
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $projectRoot
$blocking=Get-Process -Name 'normal_flow_check','gameplay_smoke' -ErrorAction SilentlyContinue | Where-Object {
    if($_.ProcessName -eq 'cl'){$compilerPath=$_.Path;!$compilerPath -or $compilerPath -notmatch '\\Microsoft Visual Studio\\2022\\'}
    elseif($_.ProcessName -eq 'MSBuild'){$buildPath=$_.Path;!$buildPath -or $buildPath -notmatch '\\dotnet\\'}
    elseif($_.ProcessName -like 'xenia*'){$emulatorPath=$_.Path;!$emulatorPath -or $emulatorPath -like '*\boi360\*'}
    else {$true}
}
if($blocking){throw 'Stop the other port build/test worker first.'}
$worker=[Diagnostics.Process]::GetCurrentProcess();$old=$worker.ProcessorAffinity;$mask=$old.ToInt64()
try {
    $worker.ProcessorAffinity=[IntPtr]($mask -band (-$mask))
    New-Item -ItemType Directory -Force -Path build/windows | Out-Null
    & python tools/prepare_player_runtime.py; if($LASTEXITCODE -ne 0){throw 'Player runtime preparation failed.'}
    & python tools/prepare_audio.py; if($LASTEXITCODE -ne 0){throw 'Audio preparation failed.'}
    & python tools/prepare_room_runtime.py; if($LASTEXITCODE -ne 0){throw 'Room runtime preparation failed.'}
    & python tools/prepare_greed_catalog.py; if($LASTEXITCODE -ne 0){throw 'Greed catalogue preparation failed.'}
    & python tools/prepare_gameplay_atlas.py; if($LASTEXITCODE -ne 0){throw 'Gameplay atlas preparation failed.'}
    # One CMake build (CMakeLists.txt): MSVC 2022 Win32, two compilers, the Vulkan renderer the console shares.
    & cmake -S . -B build/win -G 'Visual Studio 17 2022' -A Win32 2>&1 | Tee-Object build/windows/build.log
    if($LASTEXITCODE -ne 0){throw 'Windows target configure failed.'}
    & cmake --build build/win --config Release -- /m:1 /v:minimal 2>&1 | Tee-Object -Append build/windows/build.log
    if($LASTEXITCODE -ne 0){throw 'Windows target build failed.'}
    Get-FileHash -LiteralPath 'build/windows/boi360.exe' -Algorithm SHA256
} finally {$worker.ProcessorAffinity=$old}
