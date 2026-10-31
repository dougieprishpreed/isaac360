param([switch]$Rebuild,[switch]$UserRequestedTarget)
$ErrorActionPreference = 'Stop'
if(!$UserRequestedTarget) { throw 'XEX builds are reserved for an explicit user request. Routine port work uses Windows host builds.' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $projectRoot
if (Get-Process -Name 'xenia*' -ErrorAction SilentlyContinue) { throw 'Stop Xenia before building.' }
$portWorkers=Get-Process -Name 'MSBuild','cl' -ErrorAction SilentlyContinue | Where-Object {
    -not (($_.ProcessName -eq 'MSBuild' -and $_.Path -like '*\dotnet\sdk\*\MSBuild.exe') -or
        ($_.ProcessName -eq 'cl' -and $_.Path -like '*\Microsoft Visual Studio\2022\*'))
}
if ($portWorkers) { throw 'Another port build worker is active.' }
New-Item -ItemType Directory -Force -Path 'build/generated','build/xbox','logs' | Out-Null
& python tools/prepare_greed_catalog.py
if($LASTEXITCODE -ne 0){throw 'Greed catalogue preparation failed.'}
$buildProcess = [Diagnostics.Process]::GetCurrentProcess()
$oldAffinity = $buildProcess.ProcessorAffinity
$selectedMask = 0L
$selectedCount = 0
for ($bit = 0; $bit -lt 63 -and $selectedCount -lt 2; $bit++) {
    $candidate = 1L -shl $bit
    if (($oldAffinity.ToInt64() -band $candidate) -ne 0) { $selectedMask = $selectedMask -bor $candidate; $selectedCount++ }
}
if ($selectedCount -eq 0) { throw 'Cannot establish the build CPU ceiling.' }
try {
    $buildProcess.ProcessorAffinity = [IntPtr]$selectedMask
    $msbuild = Join-Path ${env:WINDIR} 'Microsoft.NET/Framework/v4.0.30319/MSBuild.exe'
    $target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
    & $msbuild 'src/xbox/GenericPort.vcxproj' $target '/m:1' '/nr:false' '/p:Configuration=Release' '/p:Platform=Xbox 360' '/p:BuildInParallel=false' '/p:MultiProcessorCompilation=false' '/v:minimal' '/fl' '/flp:logfile=logs/xdk-build.log;verbosity=normal'
    if ($LASTEXITCODE -ne 0) { throw 'XDK build failed; see logs/xdk-build.log.' }
    Get-FileHash -LiteralPath 'runtime/default.xex' -Algorithm SHA256
} finally { $buildProcess.ProcessorAffinity = $oldAffinity }
