param()
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $projectRoot
$blocking=Get-Process -Name 'xenia*','MSBuild','cl','normal_flow_check','gameplay_smoke' -ErrorAction SilentlyContinue | Where-Object {
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
    & python tools/prepare_room_runtime.py; if($LASTEXITCODE -ne 0){throw 'Room runtime preparation failed.'}
    & python tools/prepare_gameplay_atlas.py; if($LASTEXITCODE -ne 0){throw 'Gameplay atlas preparation failed.'}
    $sources=@(Get-ChildItem -LiteralPath src/game -Filter '*.cpp' | Sort-Object Name | ForEach-Object {$_.FullName})
    $sources+=@('src/content/game_content.cpp','src/content/resource_index.cpp','src/content/room_record.cpp','src/win/main.cpp') | ForEach-Object {(Join-Path $projectRoot $_)}
    $sources | ForEach-Object {'"'+$_+'"'} | Set-Content -Encoding ASCII build/windows/sources.rsp
    & cmd /d /c tools\build_windows_target.cmd 2>&1 | Tee-Object build/windows/build.log
    if($LASTEXITCODE -ne 0){throw 'Windows target build failed.'}
    Get-FileHash -LiteralPath 'build/windows/boi360.exe' -Algorithm SHA256
} finally {$worker.ProcessorAffinity=$old}
