# P1.3.4 — activité clavier/souris

Le temps de session conserve son sens V2 : temps monotone éveillé et
déverrouillé. Le temps d'activité est un sous-ensemble indépendant de ces
contributions. `TemporalEngine`, ses règles d'époques UTC et le calendrier
existant ne sont pas modifiés.

## Mesure

`ActivityTracker` ne lit aucune horloge et ne dépend pas de Win32. Il reçoit les
snapshots V2 avant/après une observation, la borne civile prédite du span V2 et
un `InputSample`. Les nouvelles contributions sont bornées par le delta de
session déjà validé par V2. Leur durée est exclusivement monotone.

La détection est activée par défaut, avec cinq minutes de grâce. Les valeurs
acceptées sont 1, 5, 10, 15 et 30 minutes. Le polling natif central est de 1 Hz ;
il fonctionne également lorsque les fenêtres sont masquées. Les checkpoints
restent à 60 secondes, avec écriture supplémentaire aux frontières utiles.
Les périodes civiles réutilisent `calendarPeriodContaining()` et les intersections
réutilisent `contributionWithin()`.

Une échéance connue est `dernière entrée validée + seuil`, dans les coordonnées
éveillées d'un segment dont la correspondance est établie. Le retard du timer
n'allonge pas cette échéance. Une entrée reçue avant l'ancienne échéance peut
prouver la continuité. Après une période inactive, la reprise commence à
l'observation validée ; aucune durée passée n'est ajoutée.

Un polling tardif ne révèle que la dernière entrée. Si plusieurs séquences
d'interaction sont possibles, le préfixe certainement couvert est conservé,
le reste n'est pas reconstitué et une incertitude est signalée. Les contenus
vidéo/PDF, le son et les applications ouvertes ne sont jamais interrogés.

## Source Windows et limites

`sampleWindowsInput()` appelle uniquement `GetLastInputInfo()` et
`GetTickCount64()`. Aucun hook ni dispositif de capture n'est installé.
Le compteur uptime est fourni même si la lecture de dernière entrée échoue.
Une lecture durant plus de 100 ms est rejetée.

Le DWORD est étendu par rapport à l'uptime 64 bits et à la dernière observation
acceptée. Zéro est une valeur valide. Les régressions, les valeurs futures et
les compteurs incohérents ne prolongent pas l'activité. Une valeur rejetée
reste en quarantaine tant qu'une entrée différente et valide ne l'a pas
remplacée ; le temps seul ne la rend pas valide. Un DWORD inchangé n'est jamais
interprété comme une nouvelle interaction au tour suivant du compteur. Une
aliasation après un tour complet est signalée comme ambiguë.

L'uptime inclut la veille, contrairement au compteur éveillé V2. Une divergence
avec les échantillons monotones, au-delà d'une garde de 150 ms couvrant la
quantification uptime et la lecture native, exclut l'intervalle ambigu et bloque
la mesure d'activité. Seule une notification explicite de reprise/déverrouillage
peut lever ce doute de puissance. Un timer ou un changement de préférence ne
le lève pas. Les garanties et les incertitudes V2 restent applicables.

Les corrections UTC divisent les contributions suivant les époques V2 ; elles
ne modifient aucune durée. Les recouvrements civils sont possibles entre époques,
les recouvrements monotones dans un run sont interdits.

L'API ne fournit que l'entrée de la session Windows appelante. Microsoft précise
que son timestamp peut régresser, notamment avec `SendInput`. Elle ne certifie
pas l'origine physique d'une interaction. L'exactitude de la coupure porte sur
le timestamp Windows accepté et sa résolution native, pas sur une précision
physique à la microseconde.

Références :

- [GetLastInputInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getlastinputinfo)
- [Horloges Windows](https://learn.microsoft.com/windows/win32/sysinfo/windows-time)

## Préférences et états

Le DWORD `ActivityDetection`, sous `HKCU\Software\Chronox\Preferences`, stocke
atomiquement le couple activation/seuil. Une valeur absente donne activé/5 min.
Les tests utilisent exclusivement des clés temporaires distinctes.

Un changement est appliqué après la clôture de la contribution précédente.
Désactivée, la détection fait suivre le temps de session prospectivement ; ce
mode est affiché explicitement. Réactivée ou avec un nouveau seuil, la mesure
ne revient jamais sur un intervalle déjà clos. Une échéance connue reste une
preuve lors d'un changement de seuil même si la lecture suivante échoue.

Le verrouillage et la suspension ferment immédiatement la contribution
d'activité. La reprise Windows seule n'est pas une nouvelle interaction.
Les notifications répétées ne créent aucune durée supplémentaire.
Les états exposés sont Actif, Inactif, Verrouillé, Veille et Mesure incertaine.
Les compteurs d'incertitude restent consultables après redémarrage.

## SQLite V3

L'ouverture explicite `StorageMode::Activity` effectue la migration additive
V2 → V3 dans une transaction. Le mode `Measured` reste disponible pour les
tests V2. `ReadOnly` accepte V1, V2 et V3 sans migration ni récupération et
refuse les versions futures inconnues.

Tables nouvelles :

| Table | Rôle |
|---|---|
| `activity_tracking_runs` | Run parent, début de suivi, nombre d'incertitudes d'entrée |
| `activity_spans` | Contributions positives, bornes UTC/monotones, époque, checkpoint ouvert, mode et seuil appliqués |
| `activity_coverage` | Segments de disponibilité de la mesure, indépendants des contributions positives |

La troisième table permet de représenter un zéro effectivement mesuré, y
compris dans un intervalle uniquement verrouillé. Elle ne contribue à aucun
cumul. Ses segments ne franchissent pas les corrections UTC ou les périodes
indéterminées. La disponibilité d'une partie d'une période ne signifie pas que
toute cette période est couverte : le dashboard l'indique explicitement.

Les contributions d'activité sont incluses dans un span de session du même run
et de la même époque. Des contraintes et triggers protègent la géométrie,
l'inclusion, la monotonie, le mode/seuil historique et l'absence de double
comptage. Il ne peut exister qu'un span d'activité ouvert. Les écritures de
session, d'activité, de couverture et de qualité sont atomiques. Un checkpoint
met à jour le span courant ; il n'ajoute pas une ligne à chaque polling.

Un échec conserve les contributions en attente. Le nouvel essai est idempotent.
La récupération ferme les indicateurs ouverts au dernier checkpoint durable ;
elle ne prolonge ni activité ni session jusqu'à l'heure du redémarrage. La queue
RAM non checkpointée peut être perdue en cas de crash, comme avec V2 ; elle
n'est jamais reconstituée.

Les tables V1 et contributions V2 historiques ne sont pas réécrites. La seule
modification de récupération autorisée sur une contribution V2 encore ouverte
est son indicateur `active`, sans changement de borne ni de durée. Aucune
activité n'est déduite d'une session historique. Les rapports distinguent
l'absence de couverture d'un zéro mesuré.

## Confidentialité

Aucune touche, position de souris, clic, application, titre de fenêtre, contenu
ou timestamp brut de dernière entrée n'est enregistré. Les références d'entrée
restent en RAM. **Les bornes temporelles des spans d'activité révèlent néanmoins
indirectement des périodes d'interaction.** Le mode/seuil et la couverture du
suivi sont également conservés localement. Les sauvegardes doivent donc être
traitées comme des données personnelles.

## Migration réelle V2 → V3 — procédure préparée, non exécutée

1. Obtenir l'autorisation explicite de déploiement. Vérifier le commit, le build
   Release, CTest et l'empreinte du candidat. Préserver les modifications locales.
2. Archiver durablement le binaire V2, ses ressources et les paramètres de
   démarrage. Fermer proprement Chronox depuis sa notification ; vérifier la fin
   du processus et son checkpoint final. Aucun arrêt forcé.
3. Effectuer une sauvegarde cohérente de la base finale avec `sqlite3_backup`,
   hors `build/`. Vérifier `quick_check`, `foreign_key_check`, le schéma et les
   empreintes des tables historiques. Ne pas copier seulement le fichier d'une
   base active en supposant qu'il soit cohérent.
4. Rejouer la migration sur une copie isolée de cette sauvegarde avec les
   bibliothèques du candidat. Comparer toutes les valeurs V1, les métadonnées V2,
   les bornes/durées des spans et les quatre cumuls. Les nouvelles tables doivent
   être vides avant le premier run d'activité. En cas d'échec, arrêter.
5. Installer par `scripts/install-local.ps1`, vérifier l'empreinte installée,
   puis lancer uniquement l'exécutable installé. L'ouverture Activity applique
   la transaction V3 ; en cas d'échec, SQLite annule la migration.
6. Contrôler schéma 3, intégrité, conservation historique, instance unique,
   préférences, compteurs distincts, seuil, checkpoints et états. Créer une
   sauvegarde V3 cohérente, puis tester un redémarrage propre et la récupération.
7. Ne restaurer aucune V2 par-dessus une V3. Le binaire V2 installé refuse le
   schéma 3. Après la première contribution V3, archiver toutes les données V3
   avant toute action de retour arrière ; privilégier une correction en avant.
   Une fusion/export spécifique doit être validé avant d'envisager un retour.

Pendant cette livraison, aucun de ces gestes d'installation réelle n'est
autorisé ni exécuté. Le candidat ne doit pas être lancé directement contre le
profil utilisateur pour une simple prévisualisation.

## Validation reproductible isolée

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target chronox_activity_migration_probe chronox_activity_resource_probe
python scripts/diagnostics/activity/rehearse_migration.py <archive-v2.db>
build/src/Release/chronox_activity_resource_probe.exe v2
build/src/Release/chronox_activity_resource_probe.exe v3
```

La répétition refuse d'écraser `build/activity-audit/copy-v3.db`. Le probe de
migration refuse toute autre destination. La source est ouverte en lecture
seule et sauvegardée par l'API SQLite. Les probes de ressources créent leur
propre répertoire temporaire et n'ouvrent jamais le profil réel.

Résultats et limites : [P1.3.4_ACTIVITY_RESULTS.md](P1.3.4_ACTIVITY_RESULTS.md).
