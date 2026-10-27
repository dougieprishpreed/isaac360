param([Parameter(Mandatory=$true)][string]$Plan)
$ErrorActionPreference='Stop'
$workspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$prefix=$workspace+[IO.Path]::DirectorySeparatorChar
$manifest=Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json
$targets=@()
foreach($relative in $manifest.remove) {
    $absolute=[IO.Path]::GetFullPath((Join-Path $workspace $relative))
    if(!$absolute.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Outside workspace: $absolute"}
    foreach($protected in @('game','runtime/game','evidence','recovered')) {
        $guard=[IO.Path]::GetFullPath((Join-Path $workspace $protected))
        if($absolute.Equals($guard,[StringComparison]::OrdinalIgnoreCase) -or $absolute.StartsWith($guard+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            #Only explicitly listed discarded decompiler output may be removed.
            if(!($relative -like 'recovered/*/pseudocode')){throw "Protected source: $absolute"}
        }
    }
    if(Test-Path -LiteralPath $absolute) {
        $item=Get-Item -LiteralPath $absolute -Force
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse target: $absolute"}
        $targets+=$absolute
    }
}
#All resolved absolute paths have been checked before the first deletion.
foreach($absolute in $targets){Remove-Item -LiteralPath $absolute -Recurse -Force}
Write-Output "Removed $($targets.Count) checked workspace targets."
