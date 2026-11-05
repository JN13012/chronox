[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$BuildDirectory = 'build\src\Release',
    [string]$BuildRoot = 'build',
    [switch]$Build,
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    throw "LOCALAPPDATA n'est pas défini ; installation impossible."
}

$resolvedBuildRoot = Resolve-RepositoryPath $BuildRoot
$sourceDirectory = Resolve-RepositoryPath $BuildDirectory
$sourceExecutable = Join-Path $sourceDirectory 'chronox.exe'
if ($Build) {
    foreach ($process in Get-Process -Name chronox -ErrorAction SilentlyContinue) {
        if (-not $process.Path) {
            throw "Le chemin de Chronox (PID $($process.Id)) est inaccessible ; compilation annulée sans arrêter le processus."
        }
        if ([string]::Equals(
                [System.IO.Path]::GetFullPath($process.Path),
                [System.IO.Path]::GetFullPath($sourceExecutable),
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Chronox utilise l'exécutable Release à compiler (PID $($process.Id)). Fermez-le manuellement puis relancez -Build."
        }
    }
    $cmakeCache = Join-Path $resolvedBuildRoot 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
        throw "Cache CMake introuvable : $cmakeCache. Configurez d'abord le projet."
    }
    $sourceEntry = Select-String -LiteralPath $cmakeCache `
        -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($null -eq $sourceEntry -or
        -not [string]::Equals(
            [System.IO.Path]::GetFullPath($sourceEntry.Matches[0].Groups[1].Value),
            $repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Le dossier de build ne correspond pas à ce dépôt : $resolvedBuildRoot"
    }
    if (-not $PSCmdlet.ShouldProcess($resolvedBuildRoot, 'Compiler Chronox en configuration Release')) {
        return
    }
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) {
        throw 'CMake est introuvable dans PATH.'
    }
    & $cmakeCommand.Source --build $resolvedBuildRoot --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "La compilation Release a échoué (code $LASTEXITCODE)."
    }
}

$sourceAssets = Join-Path $sourceDirectory 'assets\ui'
$requiredAssets = @('chronox-shell.png', 'chronox-dial-background.png')
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Exécutable Release introuvable : $sourceExecutable"
}
foreach ($asset in $requiredAssets) {
    $assetPath = Join-Path $sourceAssets $asset
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        throw "Ressource obligatoire introuvable : $assetPath"
    }
}

$installRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $env:LOCALAPPDATA 'Programs\Chronox'))
$versionName = 'version-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmssfff'),
    ([guid]::NewGuid().ToString('N'))
$versionDirectory = Join-Path $installRoot $versionName
$stagingDirectory = Join-Path $installRoot ".$versionName.staging"
if ((Test-Path -LiteralPath $versionDirectory) -or
    (Test-Path -LiteralPath $stagingDirectory)) {
    throw "Le chemin de version existe déjà ; aucun fichier modifié : $versionDirectory"
}

$previousVersions = @()
if (Test-Path -LiteralPath $installRoot -PathType Container) {
    $installRootItem = Get-Item -LiteralPath $installRoot -Force
    if (($installRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Le dossier d'installation est un lien ; installation refusée : $installRoot"
    }
    $previousVersions = @(Get-ChildItem -LiteralPath $installRoot -Directory |
        Where-Object { $_.Name -match '^version-\d{8}-\d{9}-[0-9a-f]{32}$' })
}
$processes = @(Get-Process -Name chronox -ErrorAction SilentlyContinue)
if ($processes.Count -gt 0) {
    Write-Host 'Chronox est déjà en cours ; la mise à jour versionnée ne l''arrêtera pas.'
    foreach ($process in $processes) {
        $processPath = if ($process.Path) { $process.Path } else { 'chemin inaccessible' }
        Write-Host ("  PID {0} : {1}" -f $process.Id, $processPath)
    }
}
if ($previousVersions.Count -gt 0) {
    Write-Host ("Versions déjà installées : {0}" -f
        (($previousVersions | ForEach-Object { $_.Name }) -join ', '))
    Write-Host 'Elles sont conservées pour ne pas supprimer un exécutable encore utilisé.'
}

$runKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runEntry = $null
if (Test-Path -LiteralPath $runKeyPath) {
    $runKey = Get-ItemProperty -LiteralPath $runKeyPath -ErrorAction Stop
    if ($null -ne $runKey.PSObject.Properties['Chronox']) {
        $runEntry = $runKey
    }
}
$updateRunEntry = $false
$startupArguments = '--startup'
$oldRunCommand = $null
if ($null -ne $runEntry) {
    $oldRunCommand = [string]$runEntry.Chronox
    if ($oldRunCommand -match '^\s*"(?<Executable>[^"]+)"(?<Arguments>(?:\s+.*)?)\s*$') {
        $registeredExecutable = [System.IO.Path]::GetFullPath($Matches.Executable)
        $installPrefix = $installRoot.TrimEnd('\') + '\'
        if ($Matches.Arguments.Trim() -eq $startupArguments -and
            $registeredExecutable.StartsWith(
                $installPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $updateRunEntry = $true
        } else {
            Write-Warning 'L''entrée HKCU Run Chronox ne correspond pas à une installation versionnée ; elle sera conservée.'
        }
    } else {
        Write-Warning 'L''entrée HKCU Run Chronox a un format inattendu ; elle sera conservée.'
    }
}

if (-not $PSCmdlet.ShouldProcess(
        $versionDirectory, 'Installer Chronox dans un nouveau dossier versionné')) {
    return
}

$installed = $false
$runEntryChanged = $false
$stagedHash = $null
try {
    New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $sourceExecutable `
        -Destination (Join-Path $stagingDirectory 'chronox.exe')
    $stagedAssets = Join-Path $stagingDirectory 'assets\ui'
    New-Item -ItemType Directory -Path $stagedAssets -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceAssets -Force |
        Copy-Item -Destination $stagedAssets -Recurse

    foreach ($asset in $requiredAssets) {
        $sourceAsset = Join-Path $sourceAssets $asset
        $stagedAsset = Join-Path $stagedAssets $asset
        if (-not (Test-Path -LiteralPath $stagedAsset -PathType Leaf)) {
            throw "La ressource copiée est absente : $stagedAsset"
        }
        if ((Get-FileHash -LiteralPath $sourceAsset -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $stagedAsset -Algorithm SHA256).Hash) {
            throw "La vérification SHA-256 de la ressource a échoué : $asset"
        }
    }
    $stagedExecutable = Join-Path $stagingDirectory 'chronox.exe'
    $stagedHash = (Get-FileHash -LiteralPath $sourceExecutable -Algorithm SHA256).Hash
    if ($stagedHash -ne
        (Get-FileHash -LiteralPath $stagedExecutable -Algorithm SHA256).Hash) {
        throw 'La vérification SHA-256 de chronox.exe a échoué.'
    }

    Move-Item -LiteralPath $stagingDirectory -Destination $versionDirectory
    $installed = $true

    if ($updateRunEntry) {
        $runEntryChanged = $true
        Set-ItemProperty -LiteralPath $runKeyPath -Name Chronox `
            -Value ('"{0}" {1}' -f (Join-Path $versionDirectory 'chronox.exe'),
                $startupArguments)
    }
} catch {
    $installationError = $_
    if ($runEntryChanged -and $null -ne $oldRunCommand) {
        try {
            Set-ItemProperty -LiteralPath $runKeyPath -Name Chronox -Value $oldRunCommand
        } catch {
            Write-Warning "Restauration de l'entrée de démarrage impossible : $($_.Exception.Message)"
        }
    }
    foreach ($candidate in @($stagingDirectory, $versionDirectory)) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $fullCandidate = [System.IO.Path]::GetFullPath($candidate)
            if ($fullCandidate -eq $stagingDirectory -or
                ($installed -and $fullCandidate -eq $versionDirectory)) {
                Remove-Item -LiteralPath $fullCandidate -Recurse -Force
            }
        }
    }
    throw $installationError
}

$installedExecutable = Join-Path $versionDirectory 'chronox.exe'
Write-Host "Installation réussie : $installedExecutable"
Write-Host "SHA-256 : $stagedHash"
if ($updateRunEntry) {
    Write-Host 'Chemin HKCU Run Chronox mis à jour ; l''état StartupApproved de Windows reste inchangé.'
} else {
    Write-Host 'Le démarrage Windows n''a pas été activé ni modifié.'
}
Write-Host 'La base SQLite, ses sauvegardes et les préférences utilisateur n''ont pas été modifiées.'

if ($Launch) {
    if ($processes.Count -gt 0) {
        Write-Warning 'Lancement ignoré : Chronox fonctionne déjà. Aucune instance n''a été arrêtée.'
    } elseif ($PSCmdlet.ShouldProcess($installedExecutable, 'Lancer Chronox après installation')) {
        $process = Start-Process -FilePath $installedExecutable `
            -WorkingDirectory $versionDirectory -PassThru
        Start-Sleep -Seconds 2
        $process.Refresh()
        if ($process.HasExited) {
            throw "Chronox s'est fermé au démarrage (code $($process.ExitCode))."
        }
        Write-Host "Chronox fonctionne (PID $($process.Id)). Vérifiez son icône de notification."
    }
}
