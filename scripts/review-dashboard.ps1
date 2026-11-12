param(
    [ValidateSet('Overview','History','Settings')][string]$Page = 'Overview',
    [ValidateRange(640,2400)][int]$Width = 1050,
    [ValidateRange(420,1600)][int]$Height = 700,
    [ValidateSet('standard','missing','zero','uncertain','disabled')][string]$State = 'standard'
)
$ErrorActionPreference = 'Stop'
$reviewRoot = Split-Path -Parent $PSScriptRoot
$reviewExecutable = Join-Path $reviewRoot 'build\src\Release\chronox_dashboard_review.exe'
if (-not (Test-Path -LiteralPath $reviewExecutable)) {
    throw 'Construire la cible chronox_dashboard_review en Release avant la revue.'
}
$pageIndex = switch ($Page) { 'Overview' { 0 } 'History' { 1 } 'Settings' { 2 } }
# Fixture only: no installed executable, database, registry settings or engine.
Start-Process -FilePath $reviewExecutable -WorkingDirectory $reviewRoot -WindowStyle Hidden `
    -ArgumentList "$pageIndex $Width $Height 0 unused.json $State"
