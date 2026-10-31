param([ValidateRange(1,4096)][int]$Seeds=32,[int[]]$VerifiedExternalWorkerIds=@())
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path;Set-Location -LiteralPath $projectRoot
$portWorkers=Get-Process -Name 'xenia*','MSBuild','cl','normal_flow_check' -ErrorAction SilentlyContinue | Where-Object {
    # Explicit IDs may exclude only processes whose project/parent tree has
    # been inspected and verified to belong to an unrelated workspace.
    -not (($_.ProcessName -in @('cl','MSBuild') -and $_.Id -in $VerifiedExternalWorkerIds) -or ($_.ProcessName -eq 'MSBuild' -and $_.Path -like '*\dotnet\sdk\*\MSBuild.exe') -or ($_.ProcessName -eq 'cl' -and $_.Path -like '*\Microsoft Visual Studio\2022\*') -or ($_.ProcessName -like 'xenia*' -and $_.Path -and $_.Path -notlike '*\boi360\*'))
}
if($portWorkers) {throw 'Stop other port build/test workers first.'}
$worker=[Diagnostics.Process]::GetCurrentProcess();$oldAffinity=$worker.ProcessorAffinity;$mask=$oldAffinity.ToInt64()
try {
    $worker.ProcessorAffinity=[IntPtr]($mask -band (-$mask))
    New-Item -ItemType Directory -Force -Path build/normal_flow/obj,logs | Out-Null
    $targetBefore=@(Get-ChildItem -LiteralPath runtime -Recurse -Filter '*.xex' | ForEach-Object {
        @{path=$_.FullName.Substring($projectRoot.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    ConvertTo-Json -InputObject $targetBefore -Depth 4 | Set-Content -Encoding UTF8 build/normal_flow/target_before.json
    foreach($script in @('prepare_native_data.py','prepare_boss_data.py','prepare_player_setup.py','prepare_mid_special_reference.py','prepare_floor_tail_reference.py','prepare_normal_flow.py')) {
        & python "tools/$script"
        if($LASTEXITCODE -ne 0) {throw "Preparation failed: $script"}
    }
    $sources=@(Get-ChildItem -LiteralPath src/game -Filter '*.cpp' | Sort-Object Name | ForEach-Object {$_.FullName})
    $sources+=@('src/content/game_content.cpp','src/content/resource_index.cpp','src/content/room_record.cpp') | ForEach-Object {(Join-Path $projectRoot $_)}
    $sources | ForEach-Object {'"'+$_+'"'} | Set-Content -Encoding ASCII build/normal_flow/sources.rsp
    $sources | ForEach-Object {'"build/normal_flow/obj/'+[IO.Path]::GetFileNameWithoutExtension($_)+'.obj"'} | Set-Content -Encoding ASCII build/normal_flow/objects.rsp
    & cmd /d /c tools\build_normal_flow.cmd 2>&1 | Tee-Object logs/normal-flow-build.log
    if($LASTEXITCODE -ne 0) {throw 'Normal-flow Windows build failed.'}
    & python -u tools/run_normal_flow.py --seeds $Seeds
    if($LASTEXITCODE -ne 0) {throw 'Normal-flow checks failed; inspect the immutable run.'}
} finally {$worker.ProcessorAffinity=$oldAffinity}
