[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\src\Release'),
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    throw 'LOCALAPPDATA is not defined; Chronox cannot be installed per user.'
}

$sourceDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$sourceExecutable = Join-Path $sourceDirectory 'chronox.exe'
$sourceAssets = Join-Path $sourceDirectory 'assets\ui'
$requiredAssets = @('chronox-shell.png', 'chronox-dial-background.png')

if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Release executable was not found: $sourceExecutable"
}
foreach ($asset in $requiredAssets) {
    $assetPath = Join-Path $sourceAssets $asset
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        throw "Required Chronox widget asset was not found: $assetPath"
    }
}

if ($Launch -and @(Get-Process -Name chronox -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Chronox is already running. No process was stopped; close it manually before using -Launch.'
}

$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\Chronox'
$versionName = 'version-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmssfff'), ([guid]::NewGuid().ToString('N'))
$versionDirectory = Join-Path $installRoot $versionName
$stagingDirectory = Join-Path $installRoot ".$versionName.staging"
if ((Test-Path -LiteralPath $versionDirectory) -or
    (Test-Path -LiteralPath $stagingDirectory)) {
    throw "The generated installation path already exists; no files were changed: $versionDirectory"
}

$runKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runEntry = Get-ItemProperty -LiteralPath $runKeyPath -Name Chronox -ErrorAction SilentlyContinue
$updateRunEntry = $false
$startupArguments = $null
if ($null -ne $runEntry) {
    $runCommand = [string]$runEntry.Chronox
    if ($runCommand -match '^\s*"(?<Executable>[^"]+)"(?<Arguments>(?:\s+.*)?)\s*$') {
        $registeredExecutable = [System.IO.Path]::GetFullPath($Matches.Executable)
        $registeredArguments = $Matches.Arguments.Trim()
        $installPrefix = [System.IO.Path]::GetFullPath($installRoot).TrimEnd('\') + '\'
        if ($registeredArguments -eq '--startup' -and
            $registeredExecutable.StartsWith($installPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $updateRunEntry = $true
            $startupArguments = $registeredArguments
        } else {
            Write-Warning 'An existing HKCU Run entry is not a Chronox versioned startup entry; it will be preserved.'
        }
    } else {
        Write-Warning 'The existing HKCU Run command is not in the expected Chronox format; it will be preserved.'
    }
}

if (-not $PSCmdlet.ShouldProcess($versionDirectory, 'Install Chronox side by side and update an existing Chronox startup path only')) {
    return
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
try {
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $sourceExecutable -Destination (Join-Path $stagingDirectory 'chronox.exe')
    $stagedAssets = Join-Path $stagingDirectory 'assets\ui'
    New-Item -ItemType Directory -Path $stagedAssets -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceAssets -Force |
        Copy-Item -Destination $stagedAssets -Recurse

    foreach ($asset in $requiredAssets) {
        $sourceAsset = Join-Path $sourceAssets $asset
        $stagedAsset = Join-Path $stagedAssets $asset
        if (-not (Test-Path -LiteralPath $stagedAsset -PathType Leaf)) {
            throw "Staged Chronox asset is missing: $stagedAsset"
        }
        $sourceHash = (Get-FileHash -LiteralPath $sourceAsset -Algorithm SHA256).Hash
        $stagedHash = (Get-FileHash -LiteralPath $stagedAsset -Algorithm SHA256).Hash
        if ($sourceHash -ne $stagedHash) {
            throw "Staged Chronox asset failed SHA-256 verification: $asset"
        }
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceExecutable -Algorithm SHA256).Hash
    $stagedExecutable = Join-Path $stagingDirectory 'chronox.exe'
    $stagedHash = (Get-FileHash -LiteralPath $stagedExecutable -Algorithm SHA256).Hash
    if ($sourceHash -ne $stagedHash) {
        throw 'Staged Chronox executable failed SHA-256 verification.'
    }

    Move-Item -LiteralPath $stagingDirectory -Destination $versionDirectory
} catch {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
    throw
}

$installedExecutable = Join-Path $versionDirectory 'chronox.exe'
if ($updateRunEntry) {
    Set-ItemProperty -LiteralPath $runKeyPath -Name Chronox `
        -Value ('"{0}" {1}' -f $installedExecutable, $startupArguments)
}

Write-Host "Installed executable: $installedExecutable"
Write-Host "SHA-256: $stagedHash"
if ($updateRunEntry) {
    Write-Host 'Updated the existing HKCU Run path; StartupApproved was left unchanged.'
} else {
    Write-Host 'Startup registration was not enabled or changed.'
}
Write-Host 'The installer did not modify the database or widget preferences.'

if ($Launch) {
    $process = Start-Process -FilePath $installedExecutable `
        -WorkingDirectory $versionDirectory -PassThru
    Start-Sleep -Seconds 2
    $process.Refresh()
    if ($process.HasExited) {
        throw "Chronox exited during the startup check with code $($process.ExitCode)."
    }
    Write-Host "Chronox is running (PID $($process.Id)); verify its tray icon and widget interactively."
}
