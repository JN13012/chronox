# Chronox

Chronox est une application native Windows 10/11 en C++20 qui mesure le temps
pendant lequel la session utilisateur est déverrouillée et le PC éveillé.
Le widget propose les cumuls du jour, de la semaine et du mois, ainsi que l’état
de suivi et la durée de la session active continue.
L’application fonctionne localement, sans serveur ni service cloud ; les
intervalles sont conservés dans une base SQLite.

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

## Exécuter

Depuis PowerShell à la racine du dépôt :

```powershell
.\build\src\Release\chronox.exe
```

Le programme crée `%LOCALAPPDATA%\Chronox\chronox.db` au premier lancement et
charge les ressources PNG depuis `assets\ui`, livré à côté de `chronox.exe`.
La base SQLite est liée statiquement ; le runtime MSVC est utilisé selon la
configuration CMake par défaut. Un PC qui ne possède pas déjà le runtime peut
nécessiter le Microsoft Visual C++ Redistributable 2015–2022 x64.

Après la réussite complète du workflow Windows, l’exécutable et ses ressources
`assets\ui` sont téléchargeables comme artefact `chronox-windows-x64` dans
l’exécution GitHub Actions correspondante. Aucun installateur distribué n’est
fourni ; le script local ci-dessous sert aux mises à jour de développement.

## Données et rapports

La base est créée automatiquement dans
`%LOCALAPPDATA%\Chronox\chronox.db`. Elle conserve les périodes de mesure après
la fermeture de Chronox et le redémarrage de Windows. Le widget propose les
vues Aujourd'hui, Cette semaine (lundi à dimanche) et Ce mois ; les jours
comptabilisés proviennent des intervalles réels. La vue Aujourd'hui distingue
le cumul depuis minuit de la session active continue, remise à zéro après un
verrouillage, une veille ou une interruption du suivi. Le moteur
expose `SessionDatabase::queryHistory` pour consulter en lecture seule les
cumuls et segments réellement comptés d’une période, y compris une date
antérieure. Le temps déclaré est distinct et indisponible en l’absence de
données déclaratives. Le dashboard natif P1.3.1 fournit sa fenêtre et sa
navigation ; ses pages de statistiques, d’historique et de paramètres seront
complétées par étapes. Un clic sur le compteur du widget ou un double-clic sur
l’icône de notification l’ouvre ; son menu propose également cette commande.
Fermer le dashboard le masque sans arrêter Chronox. L’affichage du widget se
met à jour lors des transitions et chaque seconde uniquement lorsqu’il est
déployé et que le suivi compte.

Une période encore en cours est sauvegardée au moins une fois par minute. Si
Chronox s’arrête brutalement, la récupération clôt cette période à son dernier
point de sauvegarde : aucun temps postérieur à l’arrêt n’est ajouté, au prix
d’une perte correspondant au temps écoulé depuis le dernier point de sauvegarde.
À l’ouverture, SQLite vérifie l’intégrité ; une base corrompue est signalée et
n’est pas réparée ni remplacée automatiquement. L’API de stockage fournit une
sauvegarde cohérente par SQLite vers un nouveau fichier, sans planification
automatique. Une seule instance peut suivre la session Windows à la fois. Le
widget, le dashboard et SQLite partagent ce processus et ce moteur. Fermer le
dashboard ou masquer le widget ne ferme pas Chronox ; seule la commande
« Quitter Chronox » arrête le suivi. L’icône officielle de Chronox reste dans la
zone de notification ; un clic simple affiche/masque le widget, un double-clic
ouvre le dashboard et le clic droit ouvre le menu. L’icône est réenregistrée
si l’Explorateur Windows redémarre. Les changements d’heure sont pris en compte
dans les limites calendaires locales.

Le menu de l’icône de notification permet d’activer explicitement le démarrage
avec Windows. L’option utilise HKCU sans élévation et reste désactivée par
défaut. Elle référence l’installation stable
`%LOCALAPPDATA%\Programs\Chronox\chronox.exe` et refuse l’activation si
l’exécutable ou ses PNG ne sont pas installés. Les mises à jour ne réactivent
pas un démarrage désactivé depuis Windows. Les anciennes copies ne sont pas
supprimées automatiquement.

Après une compilation Release, le script facultatif
`scripts\install-local.ps1` installe l’exécutable et ses PNG au chemin stable
`%LOCALAPPDATA%\Programs\Chronox\chronox.exe`. Le remplacement est préparé à
côté de la cible, vérifié par SHA-256, puis effectué avec sauvegarde et retour
arrière en cas d’échec. Le script ne ferme jamais Chronox : il refuse de
remplacer un binaire stable en cours d’utilisation et laisse tourner une
ancienne copie. Il ne touche ni à la base ni aux préférences et ne crée jamais
une entrée de démarrage. Il met à jour HKCU Run uniquement si une inscription
Chronox existante pointait déjà vers une ancienne installation connue. Les
anciennes copies restent disponibles jusqu’à leur suppression manuelle
confirmée.

Après l’installation, fermez proprement une éventuelle ancienne instance avant
de tester le nouveau binaire. Pour compiler en Release puis installer en une
commande, utiliser `.\scripts\install-local.ps1 -Build`. Pour proposer le
lancement après l’installation, ajouter `-Launch` ; si Chronox fonctionne déjà,
le lancement est ignoré plutôt que de créer une seconde instance. Les
exécutables CTest sont des cibles séparées et ne sont jamais installés. Pour
garder l’icône toujours visible, ouvrir Paramètres Windows > Personnalisation
> Barre des tâches > Autres icônes de la barre d’état système et activer
Chronox.

Les scripts fonctionnent sous Windows PowerShell 5.1 et PowerShell 7 :

```powershell
# Compiler si nécessaire, puis installer au chemin stable sans fermer Chronox
.\scripts\install-local.ps1 -Build

# Installer puis proposer le lancement (une ancienne instance peut rester active)
.\scripts\install-local.ps1 -Launch

# Prévisualiser ou confirmer la désinstallation de l’application uniquement
.\scripts\uninstall-local.ps1 -WhatIf
.\scripts\uninstall-local.ps1

# Prévisualiser ou confirmer le nettoyage des builds CMake/MSVC du dépôt
.\scripts\clean-build.ps1 -WhatIf
.\scripts\clean-build.ps1
```

La désinstallation demande confirmation avant la fermeture normale d’une
instance installée et avant les suppressions. Elle ne supprime jamais la base,
les sauvegardes SQLite ni les préférences dans `%LOCALAPPDATA%\Chronox`.
Le nettoyage ne cible que les dossiers de build CMake dont le cache identifie
ce dépôt ; il conserve un dossier utilisé par un processus Chronox.
`-WhatIf` affiche les opérations envisagées sans les exécuter. Aucun de ces
scripts n’active le démarrage Windows ou ne requiert de privilèges administrateur.

Voir [docs/SPEC.md](docs/SPEC.md) pour le périmètre, les règles de mesure et les
vérifications manuelles. Voir [docs/PERFORMANCE.md](docs/PERFORMANCE.md) pour
les mesures réellement effectuées et le protocole d’audit Windows restant.
Voir [docs/PRODUCT_VISION.md](docs/PRODUCT_VISION.md) pour la vision produit,
les principes de confidentialité et les modes d’interface envisagés. Voir
[docs/ROADMAP.md](docs/ROADMAP.md) pour les étapes proposées et leurs questions
à valider ; elles ne constituent pas des engagements de livraison.
Voir [docs/UI_SPEC.md](docs/UI_SPEC.md) pour la spécification proposée de
l’interface V0.4 et sa référence artistique.
