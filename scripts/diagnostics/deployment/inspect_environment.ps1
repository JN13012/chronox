# Read only. Does not invoke install/uninstall or application startup synchronization.
$ErrorActionPreference = 'Stop'
$install = Join-Path $env:LOCALAPPDATA 'Programs\Chronox'
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'Chronox.lnk'
$shortcut = $null
if (Test-Path -LiteralPath $shortcutPath) {
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
    $shortcut = @{path=$shortcutPath; target=$link.TargetPath; arguments=$link.Arguments;
        workingDirectory=$link.WorkingDirectory; sha256=(Get-FileHash -LiteralPath $shortcutPath).Hash}
}
function Read-Values([string]$Path) {
    $values = [ordered]@{}
    if (Test-Path -LiteralPath $Path) {
        $key = Get-Item -LiteralPath $Path
        foreach ($name in $key.GetValueNames()) {
            if ($Path -like '*Preferences' -or $name -eq 'Chronox') {
                $values[$name] = $key.GetValue($name)
            }
        }
    }
    return $values
}
[ordered]@{
    processes = @(Get-Process -Name chronox -ErrorAction SilentlyContinue | ForEach-Object {
        @{id=$_.Id; started=$_.StartTime.ToString('o'); path=$_.Path}
    })
    installedFiles = @(Get-ChildItem -LiteralPath $install -Recurse -File | Sort-Object FullName | ForEach-Object {
        @{path=$_.FullName; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
    })
    shortcut = $shortcut
    run = Read-Values 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    startupApproved = Read-Values 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
    preferences = Read-Values 'HKCU:\Software\Chronox\Preferences'
} | ConvertTo-Json -Depth 8
