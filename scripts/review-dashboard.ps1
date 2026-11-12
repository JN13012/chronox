param(
    [ValidateSet('Overview','History','Settings')][string]$Page = 'Overview',
    [ValidateRange(640,2400)][int]$Width = 1050,
    [ValidateRange(420,1600)][int]$Height = 700,
    [ValidateSet('standard','missing','zero','uncertain','disabled','reference')][string]$State = 'standard'
)
$ErrorActionPreference = 'Stop'
$reviewRoot = Split-Path -Parent $PSScriptRoot
$reviewExecutable = Join-Path $reviewRoot 'build\src\Release\chronox_dashboard_review.exe'
if (-not (Test-Path -LiteralPath $reviewExecutable)) {
    throw 'Construire la cible chronox_dashboard_review en Release avant la revue.'
}
$pageIndex = switch ($Page) { 'Overview' { 0 } 'History' { 1 } 'Settings' { 2 } }
# Fixture only: no installed executable, database, registry settings or engine.
# Interactive review: show the preview requested by the user. Hidden startup can
# override its first ShowWindow and make the manual fixture exit immediately.
Start-Process -FilePath $reviewExecutable -WorkingDirectory $reviewRoot -NoNewWindow `
    -ArgumentList "$pageIndex $Width $Height 0 unused.json $State"
