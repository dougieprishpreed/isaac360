param([uint32]$Seed=1,[switch]$Restart)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $projectRoot
$blocking=Get-Process -Name 'xenia*','MSBuild','cl','normal_flow_check','gameplay_smoke' -ErrorAction SilentlyContinue | Where-Object {
    #This maintained port only builds with VS2010/XDK. A known VS2022
    #compiler belongs to another project; do not stop the user's unrelated
    #work or mistake it for a second port worker. Unknown paths still block.
    if($_.ProcessName -ne 'cl'){$true}
    else {$compilerPath=$_.Path;!$compilerPath -or $compilerPath -notmatch '\\Microsoft Visual Studio\\2022\\'}
}
if($blocking){throw 'Stop the other port build/test/emulator worker first.'}
$worker=[Diagnostics.Process]::GetCurrentProcess();$oldAffinity=$worker.ProcessorAffinity;$mask=$oldAffinity.ToInt64()
try {
    $worker.ProcessorAffinity=[IntPtr]($mask -band (-$mask))
    New-Item -ItemType Directory -Force -Path build/gameplay/obj,build/gameplay/profile | Out-Null
    & python tools/prepare_player_runtime.py
    if($LASTEXITCODE -ne 0){throw 'Player runtime source preparation failed.'}
    & python tools/prepare_room_runtime.py
    if($LASTEXITCODE -ne 0){throw 'Room runtime source preparation failed.'}
    $sources=@(Get-ChildItem -LiteralPath src/game -Filter '*.cpp' | Sort-Object Name | ForEach-Object {$_.FullName})
    $sources+=@('src/content/game_content.cpp','src/content/resource_index.cpp','src/content/room_record.cpp') | ForEach-Object {(Join-Path $projectRoot $_)}
    $sources | ForEach-Object {'"'+$_+'"'} | Set-Content -Encoding ASCII build/gameplay/sources.rsp
    $sources | ForEach-Object {'"build/gameplay/obj/'+[IO.Path]::GetFileNameWithoutExtension($_)+'.obj"'} | Set-Content -Encoding ASCII build/gameplay/objects.rsp
    & cmd /d /c tools\build_gameplay_smoke.cmd 2>&1 | Tee-Object build/gameplay/build.log
    if($LASTEXITCODE -ne 0){throw 'Gameplay host build failed.'}
    $smokeArguments=@($Seed);if($Restart){$smokeArguments+='--restart'}
    & build/gameplay/gameplay_smoke.exe @smokeArguments | Tee-Object build/gameplay/smoke.log
    if($LASTEXITCODE -ne 0){throw 'Gameplay session smoke failed.'}
    $reportArguments=@('--seed',$Seed,'--affinity',$worker.ProcessorAffinity.ToInt64())
    if($Restart){$reportArguments+='--restart'}
    & python tools/report_gameplay_smoke.py @reportArguments
    if($LASTEXITCODE -ne 0){throw 'Focused host provenance record failed.'}
} finally {$worker.ProcessorAffinity=$oldAffinity}
