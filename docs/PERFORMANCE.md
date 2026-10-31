# Audit des performances et protocole Windows

## Résultats effectivement mesurés

Le prototype P1 a été configuré, compilé et exécuté sous Windows en MSVC
Release. Les mesures Windows ci-dessous sont de courts échantillons de
processus ; elles ne constituent pas un profil prolongé, un audit de fuites ni
une validation sur d’autres PC. WPR, `dumpbin` et Application Verifier étaient
installés ; `dumpbin` a servi à inspecter les imports du binaire. Les traces WPR
et Application Verifier n’ont pas été exécutées, et WPA n’était pas disponible.

Environnement de la mesure : Windows 11 Famille, build 26300, MSVC 19.44.35229,
Windows SDK 10.0.26100. Le widget et la console ont été lancés avec un
`LOCALAPPDATA` temporaire distinct afin de ne pas toucher à l’historique
personnel. Le binaire P1 final et ses ressources ont été construits dans
`build\src\Release`. Le poste était déverrouillé pendant les échantillons.

### Échantillons du processus Windows Release

CPU « un cœur » = delta du temps CPU du processus divisée par le temps mural,
exprimé en pourcentage d’un processeur logique. Les compteurs du processus ont
été lus une fois par seconde avec `System.Diagnostics.Process`. Les valeurs
mémoire, handles et threads sont les moyennes des relevés.

| État | Échauffement + échantillon | CPU moyen / p95 (un cœur) | Mémoire privée moyenne | Working set moyen | Handles moyens | Threads moyens |
|---|---:|---:|---:|---:|---:|---:|
| Console Release, moteur existant | 5 s + 30 s | 0 / 0 % | 1 653 965 octets | 9 917 235 octets | 120 | 3,6 |
| P1, widget déployé et suivi actif | 10 s + 30 s | 0,1544 / 1,5441 % | 8 043 042 octets | 26 411 418 octets | 243 | 13,9 |
| P1, languette rétractée et suivi actif | 2 s + 30 s après repli | 0 / 0 % | 8 025 156 octets | 25 891 362 octets | 243 | 13,4 |

La fenêtre P1 mesurée était de 250 × 250 pixels à 100 % DPI ; sa coque visible
fait environ 220 px. Après repli, sa fenêtre faisait 34 × 76 px. Le timer de
redessin du widget est arrêté en état rétracté ; le CPU mesuré était inférieur
à la résolution des relevés durant les 30 secondes observées. « 0 % » ne
garantit pas une inactivité permanente.

Les tailles des exécutables Release sur cette configuration étaient de
1 145 344 octets pour la console existante et 1 174 528 octets pour P1 après la
validation finale. Les ressources PNG fournies sont copiées à côté du nouvel
exécutable ; le dossier doit être conservé avec celui-ci. P1 utilisait environ
6,39 MB de mémoire privée, 16,49 MB de working set, 123 handles et 10 threads
de plus que la console dans ces échantillons. Le coût mémoire/handles/threads
des composants Win32, WIC, Direct2D et DirectWrite est donc mesurable et non
nul ; il reste à évaluer avec WPR/WPA et sur plusieurs configurations.

Ces relevés courts ne testent pas plusieurs heures de stabilité, les transitions
de veille ni la croissance mémoire dans le temps. La moyenne CPU de la console
était à 0 % pendant l’échantillon ; la comparaison n’établit pas un coût CPU
représentatif de toutes les scènes ou machines.

### Mesures portables antérieures sous WSL2

Les valeurs suivantes concernent uniquement des tests Linux Release ; elles ne
représentent pas les performances de l’interface ou d’un processus Windows.

Environnement des mesures portables : AMD Ryzen 7 7735HS, machine à 16
processeurs logiques, WSL2 Linux 6.18, GCC 15.2, CMake 4.4.4. La compilation
Release configure SQLite 3.54.1, la version épinglée du projet.

| Mesure | Résultat observé |
|---|---:|
| CTest complet, noyau et persistance, dont le cas de 100 000 intervalles anciens | 0,27 s, 2/2 suites réussies |
| Pic RSS de `chronox_core_tests` Release | 3 984 KiB |
| Durée de `chronox_storage_tests` Release avec 100 000 lignes | 0,17 s |
| Pic RSS de `chronox_storage_tests` Release avec 100 000 lignes | 8 320 KiB |
| Base synthétique de 100 000 intervalles fermés, index inclus | 7 368 704 octets, 1 799 pages de 4 096 octets |

Le RSS mesure le processus de test Linux, y compris son chargement et SQLite,
pas la mémoire privée de Chronox sous Windows. La taille de base a été mesurée
sur un jeu synthétique SQLite avec 100 000 intervalles et les index du schéma ;
elle donne environ 74 octets par intervalle pour ce jeu, et non une garantie de
taille pour toute répartition des données.

### Coût des rapports SQLite

Un benchmark SQL a comparé l’ancienne agrégation à la requête désormais
utilisée. Il a créé 100 000 intervalles fermés, espacés d’une heure à partir de
2015, puis un intervalle ouvert. Les deux requêtes ont été exécutées sur la même
base, avec cache chaud, 101 fois par période ; le tableau indique la médiane et
le 95e percentile des 100 dernières exécutions.

Cette mesure SQL a été exécutée dans WSL avec le module Python et SQLite 3.46.1,
et non avec l’amalgame SQLite 3.54.1 de l’application. La CTest Release avec
SQLite 3.54.1 vérifie séparément le résultat sur 100 000 intervalles anciens.

| Période locale | Ancienne requête, médiane / p95 | Requête indexée, médiane / p95 |
|---|---:|---:|
| Jour | 17,48 / 21,92 ms | 0,027 / 0,043 ms |
| Semaine | 17,37 / 20,57 ms | 0,019 / 0,022 ms |
| Mois | 18,81 / 22,93 ms | 0,027 / 0,039 ms |
| Année | 19,16 / 23,73 ms | 0,911 / 1,424 ms |

Le plan SQLite confirme que les rapports utilisent l’index de fin d’intervalle
pour parcourir les données de la période, et l’index unique partiel pour
l’intervalle ouvert. L’ancienne expression parcourait les intervalles
historiques précédant la fin de période ; les quatre rapports répétés à chaque
minute auraient donc ralenti avec l’historique. La nouvelle requête évite ce
parcours complet. La suite de tests contient un cas de régression avec 100 000
intervalles anciens, sans seuil de temps dépendant de la machine.

## Audit statique du comportement

- **Repos CPU :** la boucle de messages Windows attend dans `GetMessageW`; il
  n’y a pas de boucle de polling. Le moniteur de session garde son timer d’une
  minute. Le widget actualise ses valeurs à la seconde seulement s’il est
  visible, déployé et que le suivi compte ; le timer s’arrête lorsqu’il est
  rétracté ou en pause. Les courts échantillons Windows sont indiqués plus
  haut.
- **Écritures SQLite :** une transition ouvre ou ferme un intervalle dans une
  transaction. Tant qu’un intervalle est actif, le timer sauvegarde son point
  de contrôle au plus une fois par minute. SQLite est en journal DELETE avec
  `synchronous=FULL`. Quand le compteur est inactif, le point de contrôle ne
  modifie pas la base.
- **Mémoire et allocations :** les images sont réduites à la taille du widget
  avant la mise en cache Direct2D ; le processus n’alloue pas un bitmap de
  1254 × 1254 pixels par couche. Les compteurs Release observés sont indiqués
  plus haut. Aucun profil de tas, test de fuite Win32 ou essai de stabilité
  prolongé n’a été réalisé.
- **Threads et handles :** le code n’ajoute pas de thread applicatif dédié ;
  les API et bibliothèques Windows créent cependant des threads internes.
  Les compteurs réels du processus Release sont indiqués plus haut. Aucun
  suivi de dérive des handles ou threads sur plusieurs heures n’a été fait.
- **Verrouillage, veille et réveil :** le noyau automatisé couvre les
  transitions et le réveil d’une session verrouillée. Les notifications Win32
  réelles, l’écriture pendant la demande de veille et le comportement matériel
  n’ont pas été exercés sur ce PC.
- **Exécutable et runtime :** les tailles des binaires Release sont indiquées
  plus haut. `dumpbin /dependents` a confirmé les imports Win32, WTS, Shell,
  OLE, Direct2D et DirectWrite, ainsi que `MSVCP140.dll`, `VCRUNTIME140.dll`,
  `VCRUNTIME140_1.dll` et l’API-set CRT. SQLite est compilé statiquement.
  Un PC cible où le runtime MSVC manque peut nécessiter Microsoft Visual C++
  Redistributable 2015–2022 x64. WIC est activé via COM. La présence des outils
  WPR et Application Verifier n’équivaut pas à un profil : aucun scénario de
  diagnostic prolongé n’a été exécuté.

## Protocole reproductible sur Windows

Pour compléter les mesures P1, exécuter les relevés sur plusieurs heures, dans
les états déployé, rétracté, masqué et en pause, sur le même PC et build
Release. Noter la version Windows, le CPU, le nombre de processeurs logiques,
le commit, la taille et la date de l’exécutable. Ne pas mélanger Debug et
Release.

1. Construire et tester le binaire Release :

   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release --parallel
   ctest --test-dir build -C Release --output-on-failure
   Get-Item .\build\src\Release\chronox.exe | Select-Object Length,LastWriteTime
   ```

2. Pour le repos, lancer Chronox, attendre cinq minutes sans interaction, puis
   relever une fois par seconde les compteurs du processus. Dans une seconde
   fenêtre PowerShell, utiliser le script suivant et répéter la capture pendant
   au moins cinq transitions de verrouillage/déverrouillage :

   ```powershell
   $p = Get-Process chronox
   $cpu = $p.CPU
   $sampleTime = [DateTime]::UtcNow
   1..300 | ForEach-Object {
       Start-Sleep -Seconds 1
       $p = Get-Process -Id $p.Id -ErrorAction Stop
       $now = [DateTime]::UtcNow
       $seconds = ($now - $sampleTime).TotalSeconds
       $cpuPercent = 100 * ($p.CPU - $cpu) / $seconds
       [pscustomobject]@{
           Utc = $now.ToString('o')
           CpuOneCorePercent = [math]::Round($cpuPercent, 4)
           CpuMachinePercent = [math]::Round(
               $cpuPercent / [Environment]::ProcessorCount, 4)
           PrivateBytes = $p.PrivateMemorySize64
           WorkingSetBytes = $p.WorkingSet64
           Handles = $p.HandleCount
           Threads = $p.Threads.Count
       }
       $cpu = $p.CPU
       $sampleTime = $now
   } | Export-Csv .\chronox-process.csv -NoTypeInformation
   ```

   Les pourcentages CPU sont calculés à partir du temps CPU cumulé, normalisé
   sur un cœur puis sur le nombre de processeurs logiques.
3. Pour les allocations, les fuites et les E/S, effectuer une trace d’au moins
   15 minutes avec Windows Performance Recorder/Analyzer ou un outil équivalent
   sur la build Release. Inclure CPU, heap/processus et activité disque ; relever
   les écritures pendant une période stable, un verrouillage et une veille.
   Enregistrer la base avant et après avec :

   ```powershell
   Get-Item "$env:LOCALAPPDATA\Chronox\chronox.db" | Select-Object Length,LastWriteTime
   ```

4. Répéter les scénarios réels : session déverrouillée, `Win+L`, veille,
   réveil, verrouillage avant veille et arrêt/redémarrage. Vérifier les rapports,
   la dernière période persistée et l’absence de temps compté lorsque Chronox
   était arrêté. Ne déclarer ces scénarios validés qu’après leur exécution.
5. Mesurer la mémoire privée, le pic RAM, les handles et les threads sur la
   même durée, puis contrôler les dépendances du binaire Release avec
   `dumpbin /dependents build\src\Release\chronox.exe` depuis une invite
   développeur Visual Studio. SQLite ne doit pas nécessiter de DLL séparée ;
   vérifier les runtimes Visual C++ indiqués par les imports.

Les étapes Windows ci-dessus restent à exécuter avec un build MSVC Release réel.
