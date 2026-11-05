[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param()

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    throw "LOCALAPPDATA n'est pas défini ; installation Chronox introuvable."
}

$installRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $env:LOCALAPPDATA 'Programs\Chronox'))
if (-not (Test-Path -LiteralPath $installRoot -PathType Container)) {
    Write-Host "Aucune installation Chronox trouvée dans $installRoot."
    Write-Host 'Les données utilisateur, sauvegardes et préférences sont conservées.'
    return
}
$rootItem = Get-Item -LiteralPath $installRoot -Force
if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Le dossier d'installation est un lien ; refus de le parcourir : $installRoot"
}

$versionPattern = '^version-\d{8}-\d{9}-[0-9a-f]{32}$'
$versions = @(Get-ChildItem -LiteralPath $installRoot -Directory -Force |
    Where-Object {
        $_.Name -match $versionPattern -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'chronox.exe') -PathType Leaf)
    } | Sort-Object Name)
foreach ($version in $versions) {
    $candidate = [System.IO.Path]::GetFullPath($version.FullName)
    if (-not [string]::Equals(
            [System.IO.Path]::GetDirectoryName($candidate), $installRoot,
            [System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetFileName($candidate) -notmatch $versionPattern) {
        throw "Chemin de version inattendu ; suppression refusée : $candidate"
    }
    $versionItem = Get-Item -LiteralPath $candidate -Force
    if (($versionItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Le dossier de version est un lien ; suppression refusée : $candidate"
    }
    $reparsePoints = @(Get-ChildItem -LiteralPath $candidate -Force -Recurse |
        Where-Object {
            ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
    if ($reparsePoints.Count -gt 0) {
        throw "Un lien existe dans le dossier ; suppression récursive refusée : $candidate"
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
$removeRunEntry = $false
if ($null -ne $runEntry) {
    $runCommand = [string]$runEntry.Chronox
    if ($runCommand -match '^\s*"(?<Executable>[^"]+)"(?<Arguments>(?:\s+.*)?)\s*$') {
        $registeredExecutable = [System.IO.Path]::GetFullPath($Matches.Executable)
        $installPrefix = $installRoot.TrimEnd('\') + '\'
        if ($Matches.Arguments.Trim() -eq '--startup' -and
            $registeredExecutable.StartsWith(
                $installPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $removeRunEntry = $true
        } else {
            Write-Warning 'L''entrée HKCU Run Chronox ne pointe pas vers cette installation ; elle sera conservée.'
        }
    } else {
        Write-Warning 'L''entrée HKCU Run Chronox a un format inattendu ; elle sera conservée.'
    }
}

$allProcesses = @(Get-Process -Name chronox -ErrorAction SilentlyContinue)
if (@($allProcesses | Where-Object { -not $_.Path }).Count -gt 0) {
    foreach ($process in $allProcesses | Where-Object { -not $_.Path }) {
        Write-Warning ("Chemin du processus PID {0} inaccessible ; désinstallation annulée pour éviter de supprimer des fichiers utilisés." -f
            $process.Id)
    }
    return
}
$installPrefix = $installRoot.TrimEnd('\') + '\'
$installedExecutables = @($versions | ForEach-Object {
    [System.IO.Path]::GetFullPath((Join-Path $_.FullName 'chronox.exe'))
})
$installedProcesses = @($allProcesses | Where-Object {
    $_.Path -and $installedExecutables -contains
        [System.IO.Path]::GetFullPath($_.Path)
})
foreach ($process in $allProcesses | Where-Object {
        $_.Path -and
        $_.Path.StartsWith($installPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
        $installedExecutables -notcontains [System.IO.Path]::GetFullPath($_.Path)
    }) {
    Write-Warning ("Instance dans un dossier non reconnu conservée - PID {0} : {1}" -f
        $process.Id, $process.Path)
}
foreach ($process in $allProcesses | Where-Object {
        $_.Path -and -not $_.Path.StartsWith(
            $installPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    }) {
    Write-Host ("Instance externe conservée - PID {0} : {1}" -f
        $process.Id, $process.Path)
}

if ($installedProcesses.Count -gt 0) {
    if (-not ('ChronoxLocalProcessNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ChronoxLocalProcessNative {
    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr window, StringBuilder className, int maxCount);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindSessionMonitorWindow(int expectedProcessId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId == (uint)expectedProcessId) {
                StringBuilder className = new StringBuilder(128);
                GetClassName(window, className, className.Capacity);
                if (className.ToString() == "ChronoxSessionMonitorWindow") {
                    found = window;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static bool RequestGracefulClose(IntPtr window) {
        return PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
    }
}
'@
    }

    foreach ($process in $installedProcesses) {
        $description = "PID $($process.Id) ($($process.Path))"
        if (-not $PSCmdlet.ShouldProcess(
                $description, 'Demander la fermeture normale de Chronox')) {
            if (-not $WhatIfPreference) {
                Write-Host 'Désinstallation annulée ; l''instance active et les fichiers sont conservés.'
                return
            }
            continue
        }
        $window = [ChronoxLocalProcessNative]::FindSessionMonitorWindow(
            [int]$process.Id)
        if ($window -eq [IntPtr]::Zero) {
            throw "Fenêtre de contrôle introuvable pour $description ; fermeture impossible, fichiers conservés."
        }
        if (-not [ChronoxLocalProcessNative]::RequestGracefulClose($window)) {
            throw "La fermeture normale de $description n'a pas pu être demandée."
        }
        $deadline = (Get-Date).AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 200
            $stillRunning = @(Get-Process -Id $process.Id `
                -ErrorAction SilentlyContinue).Count -gt 0
        } while ($stillRunning -and (Get-Date) -lt $deadline)
        if ($stillRunning) {
            throw "$description fonctionne encore ; les fichiers seront conservés."
        }
        Write-Host "$description fermé normalement."
    }
}

if ($PSCmdlet.ShouldProcess(
        $installRoot, 'Désinstaller Chronox et ses versions installées')) {
    if ($removeRunEntry) {
        Remove-ItemProperty -LiteralPath $runKeyPath -Name Chronox
        Write-Host 'Entrée HKCU Run Chronox supprimée.'
    }
    foreach ($version in $versions) {
        $candidate = [System.IO.Path]::GetFullPath($version.FullName)
        Remove-Item -LiteralPath $candidate -Recurse -Force
        Write-Host "Version supprimée : $($version.Name)"
    }
} elseif ($WhatIfPreference) {
    if ($removeRunEntry) {
        Write-Host 'Suppression prévisualisée : HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Chronox'
    }
    foreach ($version in $versions) {
        Write-Host ("Suppression prévisualisée : {0}" -f
            [System.IO.Path]::GetFullPath($version.FullName))
    }
} else {
    Write-Host 'Désinstallation annulée ; les fichiers et l''entrée de démarrage sont conservés.'
    return
}

Write-Host 'Les données personnelles sont conservées : base SQLite, sauvegardes et préférences.'
Write-Host 'Le dépôt Git et les fichiers de développement ne sont pas concernés.'
Write-Host "Les entrées inconnues dans $installRoot sont laissées intactes."
