# Chronox

Chronox est un utilitaire natif Windows 10/11 en C++20 qui mesure le temps
pendant lequel la session utilisateur est déverrouillée et le PC éveillé.
L’application s’exécute en console, sans serveur, service cloud ni interface
graphique. Les intervalles sont conservés dans une base SQLite locale.

## Construire

Avec CMake 3.24+, Visual Studio 2022 et le SDK Windows :

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

La configuration télécharge l’amalgame SQLite 3.54.1 depuis sqlite.org et
vérifie son empreinte SHA-256 ; une connexion Internet est nécessaire à la
première configuration. Le prototype Windows Release est construit dans
`build\src\Release\chronox.exe` avec le générateur Visual Studio. Le noyau, le
stockage et leurs tests peuvent aussi être compilés sur un système non-Windows ;
le moniteur Windows ne s’exécute que sous Windows.

## Données et rapports

La base est créée automatiquement dans
`%LOCALAPPDATA%\Chronox\chronox.db`. Elle conserve les périodes de mesure après
la fermeture de Chronox et le redémarrage de Windows. Au lancement, la console
affiche les cumuls du jour, de la semaine (lundi à dimanche), du mois et de
l’année courants, selon le fuseau horaire local. Ils sont actualisés à chaque
transition et chaque minute.

Une période encore en cours est sauvegardée au moins une fois par minute. Si
Chronox s’arrête brutalement, la récupération clôt cette période à son dernier
point de sauvegarde : aucun temps postérieur à l’arrêt n’est ajouté, au prix
d’une perte correspondant au temps écoulé depuis le dernier point de sauvegarde.
Une seule instance peut suivre la session Windows à la fois. Les changements
d’heure sont pris en compte dans les limites calendaires locales.

Voir [docs/SPEC.md](docs/SPEC.md) pour le périmètre, les règles de mesure et les
vérifications manuelles.
