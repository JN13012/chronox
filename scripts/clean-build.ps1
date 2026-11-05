[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rootPrefix = $repositoryRoot.TrimEnd('\') + '\'
$candidatePaths = @((Join-Path $repositoryRoot 'build'),
    (Join-Path $repositoryRoot 'out\build'))
Get-ChildItem -LiteralPath $repositoryRoot -Directory -Force |
    Where-Object { $_.Name -like 'cmake-build-*' } |
    ForEach-Object { $candidatePaths += $_.FullName }

$buildDirectories = @()
foreach ($path in $candidatePaths | Select-Object -Unique) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        continue
    }
    $candidate = [System.IO.Path]::GetFullPath($path)
    if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Le chemin de build sort du dépôt ; suppression refusée : $candidate"
    }
    $item = Get-Item -LiteralPath $candidate -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        Write-Warning "Dossier de build symbolique ignoré : $candidate"
        continue
    }
    if ($candidate.EndsWith('\out\build', [System.StringComparison]::OrdinalIgnoreCase)) {
        $outPath = Join-Path $repositoryRoot 'out'
        if (Test-Path -LiteralPath $outPath -PathType Container) {
            $outDirectory = Get-Item -LiteralPath $outPath -Force
            if (($outDirectory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                Write-Warning "Dossier out symbolique ignoré : $candidate"
                continue
            }
        }
    }

    $cache = Join-Path $candidate 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
        Write-Host "Dossier ignoré (cache CMake absent) : $candidate"
        continue
    }
    $sourceEntry = Select-String -LiteralPath $cache `
        -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($null -eq $sourceEntry) {
        Write-Host "Dossier ignoré (source CMake non identifiée) : $candidate"
        continue
    }
    $cacheSource = [System.IO.Path]::GetFullPath(
        $sourceEntry.Matches[0].Groups[1].Value)
    if (-not [string]::Equals($cacheSource, $repositoryRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Dossier ignoré (cache d'un autre projet) : $candidate"
        continue
    }
    $reparsePoints = @(Get-ChildItem -LiteralPath $candidate -Force -Recurse |
        Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
    if ($reparsePoints.Count -gt 0) {
        Write-Warning "Dossier ignoré (lien détecté, suppression récursive refusée) : $candidate"
        continue
    }
    $buildDirectories += $candidate
}

if ($buildDirectories.Count -eq 0) {
    Write-Host 'Aucun dossier CMake de ce dépôt à nettoyer.'
    return
}

$processes = @(Get-Process -Name chronox -ErrorAction SilentlyContinue)
if (@($processes | Where-Object { -not $_.Path }).Count -gt 0) {
    Write-Warning 'Un chemin de processus Chronox est inaccessible ; aucun dossier de build ne sera supprimé.'
    return
}
foreach ($directory in $buildDirectories) {
    $directoryPrefix = $directory.TrimEnd('\') + '\'
    $activeProcesses = @($processes | Where-Object {
        $_.Path -and $_.Path.StartsWith(
            $directoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($activeProcesses.Count -gt 0) {
        foreach ($process in $activeProcesses) {
            Write-Warning ("Build conservé car Chronox fonctionne (PID {0}) : {1}" -f
                $process.Id, $process.Path)
        }
        continue
    }
    Write-Host "Artefacts CMake/MSVC ciblés : $directory"
    if ($PSCmdlet.ShouldProcess(
            $directory, 'Supprimer les seuls artefacts de build reconstruisibles')) {
        Remove-Item -LiteralPath $directory -Recurse -Force
        Write-Host "Build supprimé : $directory"
    } else {
        Write-Host "Suppression prévisualisée : $directory"
    }
}

Write-Host 'Les sources, ressources, documents, fichiers Git, installations et données utilisateur sont préservés.'
