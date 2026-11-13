param(
    [ValidateRange(640,2400)][int]$Width = 1050,
    [ValidateRange(420,1600)][int]$Height = 700,
    [ValidateSet('standard','missing','zero','uncertain','disabled','reference')][string]$State = 'standard',
    [switch]$Baseline
)
$ErrorActionPreference='Stop'
$reviewRoot=Split-Path -Parent $PSScriptRoot
$reviewName=if($Baseline){'chronox_dashboard_review.exe'}else{'chronox_dashboard_titanium_review.exe'}
$reviewExecutable=Join-Path $reviewRoot "build/src/Release/$reviewName"
if(-not(Test-Path -LiteralPath $reviewExecutable)){throw 'Construire les cibles chronox_dashboard_review et chronox_dashboard_titanium_review en Release.'}
# Interactive, presentation-only fixture requested for visual review. No engine,
# application database, registry settings, installation or deployment.
Start-Process -FilePath $reviewExecutable -WorkingDirectory $reviewRoot -NoNewWindow -ArgumentList "0 $Width $Height 0 unused.json $State"
