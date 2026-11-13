# Préparation de mise en service V2 — 10 novembre 2026

**Verdict de préparation : FAIL — déploiement bloqué.** La répétition sur les
données utilisateur sauvegardées conserve l'historique et produit des rapports
corrects, mais révèle une API de cumul incomplète. Aucune installation ni migration
réelle n'a été effectuée. Une autorisation explicite reste nécessaire, après
correction et revalidation.

## Résultats constatés

| Vérification | Résultat | Preuve / limite |
|---|---|---|
| Sauvegarde SQLite active via `sqlite3_backup` | PASS | Source `mode=ro` + `query_only`, aucun arrêt ; environ 89 ms |
| `quick_check` sauvegarde, migration et archives | PASS | `ok` partout |
| Migration V1 → V2 de la copie | PASS | Constructeur `SessionDatabase(Measured)` de production |
| Conservation exacte V1 | PASS | 46 lignes, 45 closes et une ouverte ; colonnes, valeurs et NULL identiques |
| Échec injecté de migration | PASS | Conflit DDL ; schéma, version et valeurs inchangés après rollback |
| Rapports jour/semaine/mois/année | PASS | 144 comparaisons `queryHistory` contre intersections calculées en Python |
| Jours actifs / identité des sessions ouvertes | PASS | 144 vérifications ; ancien intervalle V1 non présenté comme activité V2 |
| Fournisseur du tableau de bord | PASS | 936 cases comparées indépendamment ; totaux en secondes cohérents |
| API publique `duration()` | **FAIL** | 24 écarts après création de contributions V2 |
| Checkpoint, interruption logique, récupération, redémarrage | PASS | 10 s durables + 5 s RAM : reprise à 10 s ; nouveau run de 7 s ; somme finale 17 s |
| Double comptage / durées négatives | PASS | Zéro chevauchement monotone intra-run et zéro durée négative ; double écriture idempotente |
| Sauvegardes V2 et réouverture | PASS | Avant activité, pendant activité et après récupération ; V1 conservé dans chaque archive |
| Outil de sauvegarde réutilisable | PASS | 3 tests : écriture concurrente non validée exclue, refus d'écrasement/source absente, erreur non publiée |
| CTest existant | PASS | 22/22, dont 12/12 scénarios temporels, 5,95 s |
| Scripts installation/désinstallation | PASS, analyse statique | Syntaxe PowerShell valide ; chemins, garde processus et arguments compatibles |
| Raccourci / HKCU Run présents | PASS, lecture seule | Chemin stable, répertoire de travail stable, `--startup` reconnu |
| Processus, installation, préférences | PASS | Inventaires avant/après égaux par valeurs |
| Installation et lancement réels du candidat | NON TESTÉS | Ni application candidate ni scripts d'installation exécutés |
| Veille moderne/S3/hibernation et reprise matérielles | NON TESTÉS | Aucun changement d'état matériel provoqué |
| Coupure pendant écriture / perte électrique | NON TESTÉS | L'interruption logique abandonne la queue RAM ; pas de crash matériel injecté |

Les chevauchements d'UTC entre runs restent autorisés. Aucune tentative de
réparation des dates historiques n'a été faite. La session ouverte V1 est gelée
à son checkpoint sauvegardé : aucune extrapolation jusqu'à l'heure du validateur.
Le checkpoint V1 le plus récent est légèrement futur par rapport à la capture ;
il est conservé tel quel, sans servir d'ancre à la nouvelle horloge. Ces essais
ne déterminent pas rétrospectivement la cause de cet horodatage.

## Défaut bloquant caractérisé

Dans `src/storage/session_database.cpp`, `queryHistory()` sélectionne les
contributions V1/V2, tandis que la méthode privée `duration(TimeRange, now)`
exécute toujours la requête V1 sur `session_intervals`. Les deux surcharges
publiques `duration(CalendarPeriod, ...)` y aboutissent.

Exemple reproductible sur copie : après 15 s mesurées en RAM, le rapport annuel
vaut `235050987677` µs ; `duration()` retourne encore `235035987677` µs, soit
15 s omises. Après récupération puis nouveau run propre, les 17 s durables sont
également absentes de cette API. Les données restent présentes dans SQLite.

Le widget et les fournisseurs du tableau de bord utilisent actuellement
`queryHistory()`, ce qui limite l'impact visible, sans rendre acceptable cette
divergence publique. Les suites existantes passent car elles ne confrontent pas
cette API aux contributions V2 dans ce cas. Le verdict précédent 22/22 ne
couvrait donc pas cette cohérence supplémentaire.

Correction proposée, non réalisée ici : faire partager aux deux surcharges de
`duration()` l'interprétation durable/measured de `queryHistory()`, en préservant
les attentes du mode Legacy ; ajouter une régression V1 ReadOnly, V2 live,
checkpoint, reprise et convertisseur calendrier injecté. Aucun changement
d'intervalle historique ni de schéma n'est nécessaire. Rejouer ensuite la présente
répétition et CTest avant toute mise en service.

## Sauvegarde et preuves locales

La sauvegarde initiale figée est :

`build/deployment-preflight/rehearsal-20261110/user-v1-snapshot.db`

SHA-256 : `bb238ec118058099cf04e8271d72cca0f1390c72a3d4a52348ab633d46f39179`.

Empreinte des 46 lignes V1 triées par rowid, JSON compact ASCII :
`60a043d0f2180b347a4a3919a8cec8e25f427dbc3dc903979ce11b2b08776f13`.
Somme durable V1 : `235035987677` µs. L'inventaire contient les valeurs exactes,
les colonnes et les empreintes. La référence est restée identique après essais.

La première capture est documentée dans `rehearsal-20261110/validation.json`.
Le validateur final enrichi a ensuite été rejoué **depuis cette sauvegarde** dans
`final-replay-20261110/` : `validation.json`, `reports.tsv`, `buckets.tsv`, bases
V2 de test, archives, logs et sous-répertoire d'échec injecté.
`build/deployment-preflight/ctest.xml` et `environment-{before,after}.json`
complètent les preuves. Les chemins sont relatifs au dépôt.

Ces fichiers contiennent des données personnelles et sont exclus de Git par
`/build/`. Ce dossier de travail n'est pas une sauvegarde externe pérenne : avant
mise en service, conserver une copie vérifiée dans un emplacement de sauvegarde
durable choisi par l'utilisateur. Aucun fichier de preuve n'a été supprimé.

L'instance installée est restée PID 29868, démarrée le 9 novembre à
16:21:52 +01:00 ; SHA-256 du binaire installé :
`B52594DC4B3C24DF250F5B0FD9C8755049F68FF37B37A69B0B92A59C28E3BEB1`.
Raccourci : `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Chronox.lnk`.
HKCU Run : `"%LOCALAPPDATA%\Programs\Chronox\chronox.exe" --startup`
(chemin développé dans la valeur réelle). `WidgetPage=0`, valeur Run et état
StartupApproved ont été conservés. L'ordre des clés JSON n'entre pas dans la
comparaison des inventaires.

Le candidat analysé correspond au commit `89414e498993b20aae2e28e6528114252a726b74`.
SHA-256 actuel de `build/src/Release/chronox.exe` :
`1F634BF4A57F44F063FBD9F3B310B764B79B31FC82C789D17314E12996829026`.
**Ce hash sera à remplacer après correction du défaut ; ce candidat n'est pas
autorisé au déploiement.** Aucun fichier de production n'a été modifié ici.

## Reproduire les essais isolés

Depuis la racine du dépôt, choisir un nom de sortie neuf sous `build/` :

```powershell
cmake -S scripts/diagnostics/deployment -B build/deployment-probe
cmake --build build/deployment-probe --config Release
python scripts/diagnostics/deployment/test_backup_sqlite.py -v
python scripts/diagnostics/deployment/validate_copy.py `
  --source build/deployment-preflight/rehearsal-20261110/user-v1-snapshot.db `
  --output build/deployment-preflight/replay-NOUVEAU `
  --probe build/deployment-probe/Release/chronox_deployment_probe.exe
ctest --test-dir build -C Release --output-on-failure
```

Le code de sortie 1 du validateur signifie actuellement le FAIL caractérisé,
pas un échec de la sauvegarde. Le validateur n'accepte qu'un nouveau répertoire
sous `build/` et n'envoie aucun message Windows à Chronox. Les échantillons
temporels sont simulés. Le fournisseur de données du tableau de bord est exécuté,
pas son interface graphique. Les bornes calendaires viennent du code natif ;
les intersections, sommes et jours actifs de référence sont calculés séparément.

## Mise en service future — opérations prévues, non exécutées

1. Corriger le défaut, reconstruire, refaire tous les essais et fixer le nouveau
   hash candidat. Qualifier la veille réelle, ou exposer explicitement son statut
   NON TESTÉ et obtenir l'acceptation de ce risque. Obtenir l'autorisation explicite
   de déploiement ; l'autorisation de préparation ne suffit pas.
2. Préparer un répertoire d'archive durable neuf, avec espace pour les bases,
   l'installation actuelle complète, les empreintes et les inventaires Run,
   raccourci et préférences. Copier l'ancien binaire et `assets/ui` dans cette
   archive et vérifier les SHA-256. Ne pas compter sur la sauvegarde temporaire
   de `install-local.ps1`, qui est supprimée après réussite.
3. Demander **Quitter** dans Chronox. Attendre la disparition du PID identifié,
   sans arrêt forcé. En cas de refus ou erreur de fermeture : interrompre la
   procédure. Aucun `Stop-Process` ni désinstalleur. Ne pas laisser un démarrage
   concurrent pendant l'intervention et ne pas modifier l'option automatique.
4. Créer la sauvegarde finale après fermeture avec la commande ci-dessous.
   Inspecter sur cette sauvegarde `quick_check`, `user_version=1`, le dernier
   intervalle et son checkpoint final. Après fermeture propre, l'intervalle
   actif doit être clos ; sinon traiter explicitement l'interruption et sa borne
   durable, sans lui ajouter le temps d'arrêt.
5. Rejouer `validate_copy.py` sur cette sauvegarde finale vers une nouvelle
   copie isolée ; exiger PASS et égalité V1 exacte. Garder un manifeste de toutes
   les lignes et cumuls. Ne jamais utiliser la sauvegarde de ce document comme
   base finale : elle ne contient pas l'activité postérieure à sa capture.
6. Exécuter `scripts/install-local.ps1` **sans `-Launch` et sans `-Build`**, après
   vérification des hashes et de l'absence de processus. Le script remplace le
   chemin stable et ses ressources ; aucune migration SQLite n'y est effectuée.
   Vérifier le nouveau hash, la cible du raccourci et la valeur Run inchangée.
   La création du raccourci intervient après remplacement du binaire : un échec
   à cette étape exige une vérification manuelle, sans conclure à un rollback
   automatique de toute l'installation.
7. Lancer une seule fois le chemin stable. Le constructeur de stockage effectue
   la migration additive dans une transaction, puis la récupération V2 au dernier
   checkpoint. Il n'existe pas de commande de migration seule dans l'application.
   Dès ce lancement, de nouvelles durées V2 peuvent être enregistrées : toute
   stratégie de retour arrière doit désormais les préserver.
8. Créer une nouvelle sauvegarde cohérente de V2 pendant qu'elle fonctionne,
   vérifier sur cette copie version 2, intégrité, empreintes V1 inchangées, cumuls
   augmentés des seules durées mesurées, spans non négatifs, absence de
   chevauchement monotone intra-run et au plus un span actif. Vérifier visuellement
   widget/dashboard et état d'incertitude. Ne pas exiger l'égalité brute des cumuls
   avant/après alors que le temps a continué à être comptabilisé.
9. Faire un arrêt/restart propre de validation, puis un second snapshot et la
   même vérification. Conserver les archives et le rapport ; ne pas supprimer
   automatiquement V1. Le lancement à l'ouverture de session suivante reste un
   test distinct, sans changer la préférence de démarrage automatique.

Commande de sauvegarde prévue (les variables doivent être fixées explicitement
au répertoire d'archive neuf et aux chemins vérifiés ; **pas exécutée ici sur
l'installation dans un but de déploiement**) :

```powershell
$dataFile = Join-Path $env:LOCALAPPDATA 'Chronox\chronox.db'
# $archiveRoot = chemin absolu du répertoire durable neuf préparé à l'étape 2
python scripts/diagnostics/deployment/backup_sqlite.py `
  --source $dataFile --destination (Join-Path $archiveRoot 'pre-v2.db')
# Après lancement V2, autre nom neuf :
python scripts/diagnostics/deployment/backup_sqlite.py `
  --source $dataFile --destination (Join-Path $archiveRoot 'post-v2.db')
```

L'outil refuse une destination existante, ouvre toujours la source en lecture
seule, valide l'intégrité puis publie par renommage Windows sans écrasement.
Une erreur laisse un fichier `.incomplete-*`, jamais une archive annoncée valide.
Ne pas copier seulement le fichier `.db` d'une instance active, ne pas utiliser
`immutable=1` et ne pas provoquer de checkpoint WAL sur la source pour cet audit.

## Retour arrière prévu

**Échec avant toute migration et toute écriture V2 :** garder les erreurs et
empreintes, vérifier que la base réelle est toujours V1, restaurer hors exécution
l'ancien binaire et ses ressources depuis l'archive vérifiée. La base courante
reste en place. Vérifier raccourci et Run sans changer la préférence, puis
redémarrer l'ancienne version si autorisé.

**Échec de migration transactionnelle :** ne pas supposer la réussite du rollback.
Vérifier sur une sauvegarde cohérente la version, l'intégrité et les empreintes V1
contre le snapshot final. Si elles sont identiques et la version reste 1,
retour binaire possible. Sinon conserver le tout et arrêter la procédure pour
analyse ; aucune restauration silencieuse.

**Migration V2 effectuée, avec ou sans nouvelles durées :** ne pas ouvrir la base
V2 avec l'ancien binaire et ne pas faire `PRAGMA user_version=1`. Demander un arrêt
propre du nouveau processus, puis produire une archive cohérente V2 sous un nom
neuf, ses hashes et l'inventaire complet des runs/spans. Si l'application ne se
ferme pas proprement, ne pas forcer l'arrêt automatiquement ; la sauvegarde en
ligne reste possible, mais ne contient que les checkpoints durables.

Le choix par défaut est une correction en avant, application mise à l'arrêt le
temps nécessaire. Si un retour V1 est expressément choisi, conserver la base V2
originale et son archive, vérifier sur copie les durées ajoutées depuis le snapshot
V1 et les rendre explicites dans le rapport. Installer la sauvegarde finale V1
comme base distincte après mise à l'abri de la base actuelle et de ses éventuels
sidecars, hors exécution ; restaurer l'ancien binaire et ressources. Ne jamais
écraser/supprimer l'archive V2 ni ses spans. Les nouvelles durées V2 resteraient
dans cette archive et **ne seraient pas visibles dans V1** : ce choix nécessite
une acceptation explicite et un suivi de réconciliation. Aucun export/import
automatique V2 → V1 n'est fourni ni validé, donc aucun rollback transparent sans
perte de visibilité des nouveaux cumuls n'est promis.

Une sauvegarde SQLite conserve les checkpoints validés, pas les secondes encore
en RAM ni les périodes ambiguës exclues par le moteur. Les bornes de cette garantie
doivent rester visibles dans le rapport d'incident.
