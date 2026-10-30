# Audit des performances et protocole Windows

## Résultats effectivement mesurés

Les outils CMake, MSVC, CTest, `dumpbin`, WPA et Application Verifier ne sont
pas installés sur la machine Windows de développement. Il n’a donc pas été
possible de produire ni d’exécuter `chronox.exe` sous Windows. Les mesures
ci-dessous sont des mesures **Release portables sous WSL2** ; elles ne
représentent pas les performances d’un processus Windows ou d’un build MSVC.

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

- **Repos CPU :** le moniteur attend dans `GetMessageW`; il n’y a pas de boucle
  de polling. Le timer Windows se déclenche une fois par minute. Les rapports
  ne sont recalculés qu’au démarrage, au timer et lors d’une transition.
  Aucun pourcentage CPU au repos ou lors d’événements n’a été mesuré sous
  Windows.
- **Écritures SQLite :** une transition ouvre ou ferme un intervalle dans une
  transaction. Tant qu’un intervalle est actif, le timer sauvegarde son point
  de contrôle au plus une fois par minute. SQLite est en journal DELETE avec
  `synchronous=FULL`. Quand le compteur est inactif, le point de contrôle ne
  modifie pas la base.
- **Mémoire et allocations :** le code ne charge pas l’historique en mémoire ;
  les rapports sont des agrégats SQL. Les statements sont finalisés par RAII,
  les connexions SQLite sont fermées par l’objet de stockage, et les ressources
  WTS, fenêtre, notifications, timer et mutex ont des chemins de libération.
  Aucun profil d’allocations ou test de fuite Win32 n’a été réalisé.
- **Threads et handles :** l’application n’ajoute pas de thread applicatif.
  Elle utilise la boucle de messages Windows, une fenêtre cachée, un mutex
  d’instance et une connexion SQLite. Le nombre de handles et de threads du
  processus Windows n’a pas été mesuré.
- **Verrouillage, veille et réveil :** le noyau automatisé couvre les
  transitions et le réveil d’une session verrouillée. Les notifications Win32
  réelles, l’écriture pendant la demande de veille et le comportement matériel
  n’ont pas été exercés sur ce PC.
- **Exécutable et runtime :** aucune taille d’EXE ni liste des imports du build
  MSVC n’est disponible. SQLite est compilé statiquement dans Chronox. Avec la
  configuration MSVC par défaut de CMake, le runtime C/C++ est dynamique ; un
  PC cible peut nécessiter le Microsoft Visual C++ Redistributable 2015–2022
  x64. La version Windows et les imports doivent être confirmés sur l’EXE
  Release réel.

## Protocole reproductible sur Windows

Exécuter les mesures sur le même PC, avec le même build Release, après avoir
laissé Chronox démarrer et afficher ses rapports. Noter la version Windows, le
CPU, le nombre de processeurs logiques, le commit, la taille et la date de
l’exécutable. Ne pas mélanger les mesures Debug et Release.

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
