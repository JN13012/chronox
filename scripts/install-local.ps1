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
$stableExecutable = Join-Path $installRoot 'chronox.exe'
$stableAssetsRoot = Join-Path $installRoot 'assets'
$stableAssets = Join-Path $stableAssetsRoot 'ui'
$token = [guid]::NewGuid().ToString('N')
$stagingDirectory = Join-Path $installRoot ".$token.staging"
$backupDirectory = Join-Path $installRoot ".$token.backup"
$stagedAssets = Join-Path $stagingDirectory 'assets\ui'

if (Test-Path -LiteralPath $installRoot -PathType Container) {
    $installRootItem = Get-Item -LiteralPath $installRoot -Force
    if (($installRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Le dossier d'installation est un lien ; installation refusée : $installRoot"
    }
}
foreach ($candidate in @($stableExecutable, $stableAssets, $stableAssetsRoot)) {
    if (Test-Path -LiteralPath $candidate) {
        $candidateItem = Get-Item -LiteralPath $candidate -Force
        if (($candidateItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Le chemin d'installation est un lien ; remplacement refusé : $candidate"
        }
    }
}
if ((Test-Path -LiteralPath $stableExecutable) -and
    -not (Test-Path -LiteralPath $stableExecutable -PathType Leaf)) {
    throw "Le chemin de l'exécutable stable n'est pas un fichier ; remplacement refusé : $stableExecutable"
}
if ((Test-Path -LiteralPath $stableAssets) -and
    -not (Test-Path -LiteralPath $stableAssets -PathType Container)) {
    throw "Le chemin des ressources stables n'est pas un dossier ; remplacement refusé : $stableAssets"
}

$processes = @(Get-Process -Name chronox -ErrorAction SilentlyContinue)
foreach ($process in $processes) {
    if (-not $process.Path) {
        throw "Le chemin du processus Chronox (PID $($process.Id)) est inaccessible ; installation annulée sans arrêter le processus."
    }
    if ([string]::Equals(
            [System.IO.Path]::GetFullPath($process.Path),
            [System.IO.Path]::GetFullPath($stableExecutable),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Chronox utilise l'exécutable stable (PID $($process.Id)). Fermez-le proprement puis relancez le script ; aucun processus ne sera arrêté automatiquement."
    }
}
if ($processes.Count -gt 0) {
    Write-Host 'Chronox fonctionne depuis une ancienne copie ; elle reste active pendant la préparation de la version stable.'
    foreach ($process in $processes) {
        Write-Host ("  PID {0} : {1}" -f $process.Id, $process.Path)
    }
}

$legacyExecutables = @()
if (Test-Path -LiteralPath $installRoot -PathType Container) {
    $legacyExecutables = @(Get-ChildItem -LiteralPath $installRoot -Directory -Force |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'chronox.exe') })
}
if ($legacyExecutables.Count -gt 0) {
    Write-Host 'Copies historiques détectées ; elles ne seront ni écrasées ni supprimées :'
    foreach ($legacy in $legacyExecutables) {
        Write-Host "  $($legacy.FullName)"
    }
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
            [string]::Equals(
                [System.IO.Path]::GetFileName($registeredExecutable),
                'chronox.exe', [System.StringComparison]::OrdinalIgnoreCase) -and
            $registeredExecutable.StartsWith(
                $installPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $updateRunEntry = $true
        } else {
            Write-Warning 'L''entrée HKCU Run Chronox ne correspond pas à une ancienne installation Chronox ; elle sera conservée.'
        }
    } else {
        Write-Warning 'L''entrée HKCU Run Chronox a un format inattendu ; elle sera conservée.'
    }
}

if (-not $PSCmdlet.ShouldProcess(
        $stableExecutable, 'Installer ou mettre à jour Chronox à son chemin stable')) {
    return
}

$stagedHash = $null
$oldExecutableMoved = $false
$oldAssetsMoved = $false
$newAssetsMoved = $false
$newExecutableMoved = $false
$runEntryChanged = $false
try {
    New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    New-Item -ItemType Directory -Path $stagedAssets -Force | Out-Null
    Copy-Item -LiteralPath $sourceExecutable `
        -Destination (Join-Path $stagingDirectory 'chronox.exe')
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

    New-Item -ItemType Directory -Path $backupDirectory | Out-Null
    if (Test-Path -LiteralPath $stableExecutable -PathType Leaf) {
        Move-Item -LiteralPath $stableExecutable `
            -Destination (Join-Path $backupDirectory 'chronox.exe')
        $oldExecutableMoved = $true
    }
    if (Test-Path -LiteralPath $stableAssets -PathType Container) {
        $backupAssets = Join-Path $backupDirectory 'assets'
        New-Item -ItemType Directory -Path $backupAssets | Out-Null
        Move-Item -LiteralPath $stableAssets `
            -Destination (Join-Path $backupAssets 'ui')
        $oldAssetsMoved = $true
    }

    if ($updateRunEntry) {
        $runEntryChanged = $true
        Set-ItemProperty -LiteralPath $runKeyPath -Name Chronox `
            -Value ('"{0}" {1}' -f $stableExecutable, $startupArguments)
    }

    New-Item -ItemType Directory -Path $stableAssetsRoot -Force | Out-Null
    Move-Item -LiteralPath $stagedAssets -Destination $stableAssets
    $newAssetsMoved = $true
    Move-Item -LiteralPath $stagedExecutable -Destination $stableExecutable
    $newExecutableMoved = $true
} catch {
    $installationError = $_
    $rollbackErrors = @()
    if ($runEntryChanged -and $null -ne $oldRunCommand) {
        try {
            Set-ItemProperty -LiteralPath $runKeyPath -Name Chronox -Value $oldRunCommand
        } catch {
            $rollbackErrors += "Restauration de HKCU Run impossible : $($_.Exception.Message)"
        }
    }
    if ($newExecutableMoved -and (Test-Path -LiteralPath $stableExecutable -PathType Leaf)) {
        try { Remove-Item -LiteralPath $stableExecutable -Force } catch {
            $rollbackErrors += "Retrait du nouvel exécutable impossible : $($_.Exception.Message)"
        }
    }
    if ($newAssetsMoved -and (Test-Path -LiteralPath $stableAssets -PathType Container)) {
        try { Remove-Item -LiteralPath $stableAssets -Recurse -Force } catch {
            $rollbackErrors += "Retrait des nouvelles ressources impossible : $($_.Exception.Message)"
        }
    }
    if ($oldAssetsMoved -and (Test-Path -LiteralPath (Join-Path $backupDirectory 'assets\ui'))) {
        try {
            New-Item -ItemType Directory -Path $stableAssetsRoot -Force | Out-Null
            Move-Item -LiteralPath (Join-Path $backupDirectory 'assets\ui') `
                -Destination $stableAssets
        } catch {
            $rollbackErrors += "Restauration des anciennes ressources impossible : $($_.Exception.Message)"
        }
    }
    if ($oldExecutableMoved -and
        (Test-Path -LiteralPath (Join-Path $backupDirectory 'chronox.exe') -PathType Leaf)) {
        try {
            Move-Item -LiteralPath (Join-Path $backupDirectory 'chronox.exe') `
                -Destination $stableExecutable
        } catch {
            $rollbackErrors += "Restauration de l'ancien exécutable impossible : $($_.Exception.Message)"
        }
    }
    if ($rollbackErrors.Count -gt 0) {
        throw ("Échec d'installation : {0}`n{1}`nLa sauvegarde est conservée dans : {2}" -f
            $installationError.Exception.Message, ($rollbackErrors -join "`n"), $backupDirectory)
    }
    foreach ($candidate in @($stagingDirectory, $backupDirectory)) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            Remove-Item -LiteralPath $candidate -Recurse -Force
        }
    }
    throw $installationError
}

foreach ($candidate in @($stagingDirectory, $backupDirectory)) {
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        try {
            Remove-Item -LiteralPath $candidate -Recurse -Force
        } catch {
            Write-Warning "Suppression du dossier temporaire impossible ; il est conservé : $candidate"
        }
    }
}

Write-Host "Installation stable réussie : $stableExecutable"
Write-Host "SHA-256 : $stagedHash"
if ($updateRunEntry) {
    Write-Host 'HKCU Run Chronox pointe maintenant vers la version stable ; l''état StartupApproved de Windows reste inchangé.'
} else {
    Write-Host 'Le démarrage Windows n''a pas été activé ni modifié.'
}
Write-Host 'Les copies historiques, SQLite, les sauvegardes et les préférences utilisateur sont préservées.'

if ($Launch) {
    if ($processes.Count -gt 0) {
        Write-Warning 'Lancement ignoré : Chronox fonctionne déjà. Aucune instance n''a été arrêtée.'
    } elseif ($PSCmdlet.ShouldProcess($stableExecutable, 'Lancer Chronox après installation')) {
        $process = Start-Process -FilePath $stableExecutable `
            -WorkingDirectory $installRoot -PassThru
        Start-Sleep -Seconds 2
        $process.Refresh()
        if ($process.HasExited) {
            throw "Chronox s'est fermé au démarrage (code $($process.ExitCode))."
        }
        Write-Host "Chronox fonctionne (PID $($process.Id)). Vérifiez son icône de notification."
    }
}
