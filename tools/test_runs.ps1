param([ValidateRange(1,4096)][int]$Seeds=128)
& (Join-Path $PSScriptRoot 'test_normal_flow.ps1') -Seeds $Seeds
exit $LASTEXITCODE
