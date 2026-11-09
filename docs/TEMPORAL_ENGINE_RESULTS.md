# Chronox — validation du moteur temporel V2

9 novembre 2026. Livraison locale en cinq étapes, sans déploiement.

**PASS pour la validation automatisée et le budget du banc isolé.**
**Veille/reprise matérielles : NON TESTÉES.** La mise en service et la migration
de la base utilisateur restent hors de cette livraison et nécessitent une
autorisation explicite. L'origine exacte des 86 s historiques n'est pas
reconstituée ; aucun ancien timestamp n'a été corrigé rétrospectivement.

## Résultat fonctionnel

Le moteur utilise des échantillons UTC Windows/QPC/compteur d'éveil injectables.
Les durées actives ne dépendent pas des corrections UTC. Une correction ouvre
une époque civile ; les compteurs et séquences restent monotones. Les dates
civiles peuvent se recouvrir entre époques/exécutions ; chaque microseconde
mesurée appartient à une seule contribution comptable.

Les rapports, buckets, widget et dashboard utilisent les contributions explicites.
L'identité active ne dépend plus du dernier timestamp UTC dans une liste triée.
Les historiques futurs restent consultables et ne déplacent pas l'heure actuelle.
Les journées changent selon le calendrier local ; la durée continue reste indépendante.

Une période ambiguë détectée par les compteurs est exclue et signalée. Le suivi
reste suspendu tant qu'une reprise ou un déverrouillage explicite n'a pas fourni
de nouvelle preuve ; le timer ne réactive pas silencieusement le compteur.
Le nombre d'incertitudes est durable et visible dans l'interface, même si le
journal optionnel est désactivé. Cela peut sous-estimer une période ambiguë :
la durée inconnue n'est jamais remplacée par une estimation présentée comme exacte.

## Tests exécutés

MSVC Release : build complet PASS. Dernier CTest principal : **22/22 PASS**, 4,34 s.
Il contient les **7 suites existantes**, les **12 scénarios temporels**, et
3 nouvelles suites noyau/persistance/diagnostic. Le banc autonome compile aussi
les sources courantes : **12/12 PASS**, 0,74 s.

| Scénario temporel | Résultat vérifié |
|---|---|
| Référence sans correction | PASS : 120 s mesurées |
| UTC +120 s en activité | PASS : UTC corrigée, durée 120 s |
| UTC −120 s en activité | PASS : UTC corrigée, durée 120 s |
| Minuit | PASS : 10/10 s, total 20 s |
| Minuit, projection initiale obsolète +86 s | PASS : 10/10 s après échantillonnage civil initial |
| Veille 1 h, QPC avance | PASS : seulement 30 s actives |
| Veille 1 h, monotone simulé arrêté | PASS : seulement 30 s actives |
| Reprise, UTC +120 s | PASS : UTC corrigée, durée 30 s |
| Reprise, UTC −120 s | PASS : UTC corrigée, durée 30 s |
| Interruption puis redémarrage | PASS : 45 s durables + 20 s nouvelles |
| Redémarrage, historique futur | PASS : UTC Windows, historique retenu, total 65 s |
| Notifications répétées et reprise verrouillée | PASS : 30 s, aucun double départ |

Les fixtures ont été portées sur la vraie chaîne TemporalEngine/SessionDatabase :
elles ne réimplémentent plus l'ancien `max(historique, UTC)` dans le test.
L'exigence de non-chevauchement concerne les coordonnées monotones d'une
exécution, conformément au plan validé. La monotonie de la durée remplace
l'ancienne assertion imposant à l'UTC civile de ne jamais reculer.
Le test de projection initiale obsolète corrige l'ancre depuis l'échantillon
Windows avant la première durée comptabilisée ; il ne répare pas des données historiques.

Essais complémentaires PASS :

- 10 000 transitions/corrections pseudo-aléatoires reproductibles : somme SQL
  des durées exactement égale au temps physique simulé déverrouillé/éveillé,
  zéro chevauchement monotone ;
- refus des compteurs régressifs et des durées/checkpoints invalides ;
- détection d'une veille simulée courte et prise en compte de l'incertitude de lecture ;
- exclusion d'une période sans notification, persistance de son signalement,
  absence de reprise silencieuse ;
- journées DST V2 de 23 et 25 heures ;
- concordance Vue générale/Historique/buckets après recouvrement civil ;
- messages Windows synthétiques et inscription/désinscription native aux
  notifications d'alimentation ;
- journal désactivé, rotation, plafond de taille, limite de débit, snapshot UTC
  direct, erreur d'écriture sans interruption du moteur.

## SQLite et préservation

**Aucun accès à la base utilisateur pendant cette implémentation/validation.**
Tous les essais de migration, récupération et sauvegarde ont utilisé des bases
synthétiques temporaires et leurs copies. Les fichiers du diagnostic historique
ne sont pas utilisés comme bases d'essai.

Le schéma V2 ajoute `temporal_runs` et `measured_spans`. La table V1 est laissée
intacte, y compris ses lignes interrompues ; leur borne durable de lecture reste
`COALESCE(ended_at_us, checkpoint_at_us)`. Aucune durée monotone ancienne n'est inventée.

Vérifications PASS sur les fixtures :

- lecture V1 et V2 en READONLY/query_only, ainsi que compatibilité de lecture V2
  par l'API existante ; écritures V1 refusées sur V2 ;
- comparaison de toutes les valeurs des lignes V1 avant/après migration,
  écriture V2, récupération et réouverture : identiques ;
- base V1 originale non migrée ; migration de sa seule copie ;
- échec DDL injecté : rollback du schéma/version, lignes V1 inchangées ;
- récupération V2 au dernier checkpoint, sans ajout du temps hors processus ;
- sauvegarde en ligne V1/V2 cohérente, sans capture du simple cache RAM ;
- échec de checkpoint injecté : transaction annulée, contributions en attente
  conservées ; retry et répétition sans double comptage ;
- contraintes rejetant une durée régressive et un chevauchement monotone ;
- `quick_check=ok`.

Un ancien exécutable refuse le schéma V2. Aucun retour arrière destructif ni
réparation automatique des anciens horodatages n'est fourni.

## Mesures CPU/RAM

Banc **natif isolé**, sans widget/dashboard, sans mutex Chronox, sans installation.
Référence exacte extraite avec `git show d6db29d:<fichier>`, sans checkout/reset.
Les deux variantes compilent en MSVC Release contre le même amalgame SQLite
3.54.1 du projet. Le banc crée ses propres bases/répertoires temporaires.

Trois profils, deux répétitions avec ordre inversé : 200 000 appels de noyau,
200 consultations des quatre périodes sur 100 000 lignes historiques, puis
30 observations à 1 Hz avec quatre rapports par observation. La cadence donne
60 s observées par variante. CPU = somme des temps CPU / somme des durées
murales, en pourcentage d'un cœur. Mémoire = maximum des octets privés mesurés.
Les mesures commencent après initialisation ; le coût de migration/démarrage
et celui du rendu graphique ne sont pas inclus.

| Variante, cadence 1 Hz | CPU d'un cœur | Privée maximale du banc | p95 maximum d'une observation avec rapports |
|---|---:|---:|---:|
| Référence d6db29d | 0,154 % | 1,059 Mio | 1,965 ms |
| V2, diagnostic désactivé | 0,103 % | 1,055 Mio | 2,235 ms |
| V2, diagnostic activé | 0,488 % | 1,004 Mio | 7,060 ms |

Écart mesuré par défaut : **−0,051 point CPU et −0,004 Mio**. Le budget proposé
de +0,1 point / +2 Mio est respecté dans ce banc. Ces petites différences CPU/RAM
restent sensibles à la granularité et au bruit ; elles ne démontrent pas un gain
CPU ou mémoire du produit. L'écriture optionnelle à 1 Hz a, elle, un coût visible.
Les 47 handles restent stables pendant chaque passage ; pas de qualification de
fuite prolongée à partir de ces seuls échantillons.

Le p95 du noyau seul passe de **0,1 à 0,3 µs**. Les pourcentages CPU de ces
micro-benchmarks trop courts ne servent pas à l'acceptation : la granularité de
GetProcessTimes peut même produire un résultat supérieur à 100 %.

Sur 100 000 lignes anciennes, les quatre rapports ont une médiane de
**99,2–133,7 ms** pour la référence contre **0,946–1,089 ms** pour V2 ; p95 maximum
164,127 ms contre 1,559 ms. Le chemin V2 force les index de fin/intervalle ouvert
pour éviter le scan historique induit par le tri et calcule les jours actifs
à partir des seules contributions présentes. Ce résultat vaut pour ce jeu
synthétique et ce poste, pas comme seuil universel de performance.

## Limites et arrêt de livraison

**NON TESTÉS matériellement :** veille moderne réelle, S3, hibernation, reprise
par maintenance/bouton/clavier, livraison/perte réelle de notifications WTS/power,
transition DST du poste et changement d'heure réel. Aucune heure Windows n'a
été modifiée. L'extinction de l'écran n'est pas assimilée automatiquement à une veille.

**NON TESTÉS sur la nouvelle application en fonctionnement réel :** rendu visuel,
profil CPU/RAM du widget/dashboard, installation, bascule et migration utilisateur.
L'inscription aux API et les simulations ne transforment pas ces points en PASS.
Une correction détectée est datée à son observation ; la résolution de lecture
et le seuil de détection ne permettent pas de reconstituer un instant manquant.

L'instance installée demeure PID **29868**, démarrée le 09/11 à 16:21:52,988
heure de Paris. Empreinte installée inchangée :
`B52594DC4B3C24DF250F5B0FD9C8755049F68FF37B37A69B0B92A59C28E3BEB1`.
Seul le build local a été remplacé. Aucun lancement de `chronox.exe`, installateur,
arrêt d'instance, migration réelle, commit de pixels, P1.3.4 ou push.
La suppression locale de l'image et les artefacts préexistants sont conservés.

## Commits et reproduction

1. `5e3aba2` — `feat(core): separate measured time from civil clock epochs`
2. `1eb5c4d` — `feat(storage): preserve legacy history alongside measured spans`
3. `f796cda` — `feat(windows): integrate civil epochs and explicit power evidence`
4. `1bf6bde` — `feat(diagnostics): expose bounded clock and uncertainty telemetry`
5. Commit de cette validation — `test(time): harden temporal invariants and benchmark measured storage`

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python scripts/diagnostics/temporal-sync/benchmark/run_benchmark.py
python scripts/diagnostics/temporal-sync/benchmark/summarize.py
```

Ces commandes ne lancent pas l'application installée. Preuves ignorées par Git :
`build/temporal-v2-evidence/ctest.xml`, `scenarios-standalone.xml`, `benchmark.json`,
`benchmark-summary.json`, `active-process.json`, `installed-hash.json`.
Les sources de référence du benchmark sont dans `build/temporal-reference/`.
Politique détaillée et activation future du diagnostic : [TEMPORAL_ENGINE.md](TEMPORAL_ENGINE.md).
