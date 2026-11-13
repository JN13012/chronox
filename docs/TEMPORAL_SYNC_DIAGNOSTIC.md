# Chronox — diagnostic de synchronisation temporelle

Diagnostic du **9 novembre 2026**, sur HEAD `d6db29d`, avant P1.3.4.

**Verdict global : NON CONCLUANT sur l'origine exacte de l'avance historique.**
Le mécanisme qui conserve un décalage est caractérisé et reproductible :
**FAIL pour la synchronisation avec l'heure civile après correction**, avec
**PASS pour les durées actives et l'absence de double comptabilisation dans les
scénarios simulés exécutés**. Aucun correctif moteur appliqué.

## 1. Cause établie et hypothèses ouvertes

### Fonctionnement constaté dans les sources

- [`UtcTimestampClock`](../src/core/utc_timestamp_clock.cpp) produit
  `UTC_ancre + (steady_now - steady_ancre)`, en microsecondes. `at()` ne relit
  jamais l'heure système ; il impose un résultat non décroissant.
- `reanchor()` impose aussi `UTC_ancre >= dernier_timestamp_délivré`. Une
  correction vers l'arrière peut donc rester sans effet sur l'heure Chronox.
- Au démarrage, `alignWithPersistedTime()` prend le maximum de l'UTC système et
  du dernier timestamp SQLite. Le constructeur de `SessionDatabase` récupère
  d'abord les intervalles interrompus à leur checkpoint ; `latestRecordedTimestamp()`
  lit ensuite le maximum des fins enregistrées. Une ancienne fin future peut
  ainsi imposer une avance au nouveau processus.
- [`main.cpp`](../src/windows/main.cpp) réancre au lancement et à une transition
  effective `Resumed`, puis persiste les transitions et les checkpoints. Il
  n'existe pas de recalage périodique à l'UTC système ni de traitement de
  `WM_TIMECHANGE` dans [`WindowsSessionMonitor`](../src/windows/windows_session_monitor.cpp).
  Les reprises redondantes sont ignorées avant réancrage.
- [`SessionTracker`](../src/core/session_tracker.cpp) mesure séparément les durées
  en `steady_clock`, seulement pendant `déverrouillée && éveillée`. Les rapports
  SQLite additionnent des différences de timestamps des intervalles ; ils
  n'enregistrent pas une durée monotone indépendante.
- SQLite reçoit des entiers UTC en microsecondes du moteur. Aucun `CURRENT_TIMESTAMP`,
  `datetime('now')` ou recalage autonome SQLite n'intervient dans ces écritures.

Le `steady_clock` du MSVC installé utilise `QueryPerformanceCounter` (vérifié
dans `__msvc_chrono.hpp`, version 14.44.35207). Microsoft précise que QPC est
indépendant de l'UTC et inclut la veille/hibernation. L'exclusion du sommeil
dépend donc des transitions, pas de la seule propriété monotone de l'horloge.
[Documentation QPC](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).

Ce fonctionnement est compatible avec la protection contre les durées négatives
décrite dans SPEC.md ; il ne garantit pas que les timestamps restent synchronisés
à l'heure Windows. Une avance constante décale les dates civiles sans ajouter,
à elle seule, cette avance à chaque durée active.

### Ce que disent les événements Windows

`build/audit-p133/clock-changes.json` a été confronté aux événements System
Kernel-General 1 et au journal Time-Service/Operational en lecture seule.

- Le **09/11 à 09:57:05 UTC** : correction explicite de **−1,9210013 s**, par
  `svchost.exe`, avec synchronisation `time.windows.com` confirmée par Time-Service.
  Elle explique au plus une composante de l'avance, pas les ~86 s entières.
- Les grands sauts positifs de raison 2 sont décrits par Windows comme une
  synchronisation avec l'horloge matérielle. Ils coïncident avec les cycles
  veille/hibernation/reprise : par exemple **+2 767,735220 s** le 09/11 à
  08:27:48 UTC et **+74 530,579319 s** à 05:13:15 UTC. Leur somme ne constitue
  pas une mesure d'erreur Chronox ; elle comprend des périodes de suspension.
- Les événements opérationnels 261 du 07–09/11 ne révèlent pas une autre
  correction explicite de ~86 s. L'absence de saut journalisé ne prouve pas
  l'absence d'ajustements fins : Windows applique des seuils de journalisation.
  [Traçabilité du temps Windows](https://learn.microsoft.com/en-us/windows-server/networking/windows-time-service/windows-time-for-traceability).
- Au relevé actuel, `w32tm /query /status /verbose` retourne « service non
  démarré » (`0x80070426`). Aucun service n'a été démarré et aucune resynchronisation
  demandée. Comparer à Windows ne certifie donc pas l'exactitude UTC absolue du poste.

### Conclusion causale

**Établi :** un ancrage devenu ancien, ou une borne historique future, est
conservé intentionnellement par le moteur. Les corrections Windows en cours
d'exécution ne sont pas suivies automatiquement. Ce mécanisme reproduit une
avance après correction arrière et sa propagation possible au redémarrage.

**Non établi :** l'événement précis qui a créé la totalité de l'avance du
processus historique 8932. Il manque les couples UTC/QPC et les décisions de
réancrage de ce processus. Les pistes encore ouvertes sont un ancrage déjà
décalé, une borne héritée de SQLite, et le comportement des reprises/ajustements
sur une exécution de plusieurs jours. La correction explicite de −1,921 s ne
suffit pas ; aucune dérive progressive de plusieurs dizaines de secondes n'est
observée dans les séries disponibles. Aucun élément ne désigne un bug d'unité,
un changement de fuseau ou SQLite comme origine du décalage.

## 2. Reproduction et relevés

### Instance actuelle et méthode

L'instance actuelle est **PID 29868**, démarrée à **16:21:52,988 heure de Paris**,
avant les mesures de ce diagnostic. Le processus précédent 8932 n'est plus
disponible. Aucun redémarrage n'a été effectué par ce diagnostic.

Le binaire installé et celui du build ont la même empreinte SHA-256 :
`B52594DC4B3C24DF250F5B0FD9C8755049F68FF37B37A69B0B92A59C28E3BEB1`,
également consignée dans l'audit précédent. L'intervalle 41 est maintenant fermé
à **15:21:23,044758 UTC** ; l'intervalle 42 commence à **15:21:53,038857 UTC**.
La dernière fin historique est donc antérieure au nouveau départ : elle ne
force plus une heure future lors de cet alignement.

Le script `sample_readonly.py` a recueilli **719 relevés sur 180,179350 s**,
avec trois nouveaux checkpoints. La connexion SQLite utilise `mode=ro` et
`PRAGMA query_only=ON`, sans `immutable`, sans constructeur `SessionDatabase`
et sans opération de récupération. L'UTC est échantillonné avec `time.time_ns()`
et la durée monotone avec `time.perf_counter_ns()`, autour de chaque requête.

**Limite de mesure :** le processus n'expose pas directement son `UtcTimestampClock`.
La colonne Chronox ci-dessous est une projection d'un checkpoint nouvellement
observé par la durée monotone écoulée depuis sa détection. Ce n'est pas une
lecture de sa mémoire ni une télémétrie interne. L'erreur comprend le délai
de polling (0,251 s environ aux transitions, maximum global 0,274 s) et la
latence non mesurée entre calcul du timestamp et visibilité de sa transaction.
Une faible valeur négative de cette projection est attendue à cause du polling.

Toutes les heures du tableau sont en **UTC, le 09/11/2026**. La projection est
réancrée à chaque nouveau checkpoint détecté.

| Windows UTC | Chronox UTC projetée | Chronox − Windows | Dernier checkpoint SQLite | Monotone depuis le début |
|---|---|---:|---|---:|
| 15:34:18,589642 | indisponible avant observation d'une écriture | — | 15:33:53,111803 | 0,000553 s |
| 15:34:53,231065 | 15:34:53,100750 | −0,130315 s | 15:34:53,100750 | 34,641974 s |
| 15:35:53,199942 | 15:35:53,111591 | −0,088351 s | 15:35:53,111591 | 94,610852 s |
| 15:36:53,179663 | 15:36:53,104464 | −0,075199 s | 15:36:53,104464 | 154,590573 s |
| 15:37:18,768992 | 15:37:18,693793 | −0,075199 s | 15:36:53,104464 | 180,179902 s |

Avec **un seul ancrage fixe**, la projection reste entre −0,130342 et
−0,130295 s ; les petites différences entre lignes réancrées sont liées à
l'observation des écritures. Windows et le monotone progressent de
180,179350 s et 180,1793497 s respectivement. **Aucune avance de 58–86 s,
aucune dérive significative et aucun saut observés sur ces trois minutes.**
Cette conclusion porte sur le nouveau processus et cette fenêtre uniquement.

### Réinterprétation des mesures historiques

| Windows UTC historique | Checkpoint − Windows |
|---|---:|
| 10:16:13,751890 | +60,039069 s |
| 10:16:54,416385 | +79,389610 s |
| 13:21:16,532893 | +58,344481 s |
| 13:23:48,528352 | +86,403233 s |
| 15:09:12,697953 | +62,983881 s |
| 15:16:04,006319 | +71,722721 s |

Ces valeurs décrivent **le checkpoint**, pas directement l'heure courante du
moteur. Pour une avance interne constante `D` et un checkpoint âgé de `A`
secondes, la valeur observée est `D − A`. Le timer de persistance est à
60 000 ms ; la phase de lecture crée donc une variation en dents de scie.
Ces six valeurs ne démontrent pas une dérive de 58 à 86 secondes.

Le fichier `sql-display-consistency.json` donne en plus une avance estimée
d'au moins **86,276829 s** à 15:13:20,842 UTC, déduite du début de l'intervalle
et de la durée active affichée à la seconde. Le rafraîchissement et le moment
de capture empêchent d'en faire une borne supérieure précise.

La série historique de 3 600 relevés progresse de **3 745,440932 s en UTC**
contre **3 745,4766714 s au monotone**, soit **35,739 ms** de différence nette.
Elle est compatible avec un décalage d'ancrage persistant ; elle ne mesure
pas l'avance absolue du processus, faute d'ancre UTC interne connue.

**Distinction :** offset persistant plausible et mécanisme reproduit ; variation
des checkpoints principalement liée à leur âge ; dérive importante non observée ;
saut Windows de −1,921 s effectivement journalisé, distinct des sauts de reprise.

## 3. Gravité pour les données enregistrées

**Intégrité structurelle : PASS sur les lectures réalisées.** `quick_check=ok`
avant/après observation, 42 intervalles dont un ouvert, aucune borne inversée,
aucun chevauchement entre les intervalles sauvegardés. Les 41 lignes fermées
ont exactement la même empreinte avant/après les trois minutes. Le checkpoint
de la ligne ouverte continue de progresser normalement.

**Durées actives : pas de surcomptage démontré par le seul décalage UTC.**
Dans les scénarios simulés, une correction avant/arrière conserve bien
120 s pour 120 s actives ; veille/reprise conserve 30 s actives ; récupération
après interruption conserve 45 s durables puis 20 s nouvelles, sans ajouter
la période hors processus ni créer de chevauchement. Ce résultat ne certifie
pas rétrospectivement tous les cumuls utilisateur.

**Dates civiles : risque réel, reproduit.** Avec une avance simulée de 86 s,
une session de 20 s autour du vrai minuit est affectée à **0 s / 20 s** entre
les deux journées au lieu de **10 s / 10 s**. Le total de 20 s reste correct,
mais la répartition jour/semaine/mois peut être erronée près d'une frontière.
Les timestamps peuvent aussi désigner un futur apparent et compliquer les
comparaisons avec d'autres journaux.

**Veille réelle : réserve indépendante.** Windows journalise un épisode de
veille moderne le 09/11 entre **14:07:09,762891 et 14:08:38,554967 UTC**
(88,792076 s), alors que l'intervalle historique 41 reste continu. L'exclusion
effective de cette période n'est pas démontrée. Sans trace des notifications
reçues par Chronox ni durée monotone historique de référence, on ne peut pas
affirmer si cet épisode a été compté, ni l'attribuer à l'avance UTC. Les tests
avec notifications simulées ne valident pas leur livraison sur ce matériel.

**Gravité retenue : modérée pour l'horodatage et l'affectation calendaire ;
exactitude historique des durées NON CONCLUANTE**, avec une réserve spécifique
sur la veille moderne. Aucune corruption SQLite constatée. Aucune correction
automatique des données historiques n'est justifiée par les preuves présentes.

## 4. Tests déterministes exécutés

Banc autonome dans `scripts/diagnostics/temporal-sync/`, compilé avec MSVC Release
et l'amalgame SQLite 3.54.1 déjà présent dans le build. Il compile les sources
de production inchangées. Chaque scénario utilise des temps explicites et une
base neuve dans un répertoire temporaire, retiré à la fin. Aucun changement de
l'heure du poste, aucune utilisation de la base utilisateur par les tests.
Les échecs ci-dessous sont de vrais échecs d'exigence de synchronisation civile,
pas des échecs de compilation ni des exceptions masquées comme des succès.

| Scénario | Résultat | Observation |
|---|---|---|
| Référence sans correction | PASS | UTC et durée SQLite/monotone correctes, 120 s |
| Correction système +120 s pendant l'activité | FAIL UTC ; PASS durée | Chronox reste 120 s derrière ; durée 120 s |
| Correction système −120 s pendant l'activité | FAIL UTC ; PASS durée | Chronox reste 120 s devant ; durée 120 s |
| Minuit sans offset | PASS | 10 s de chaque côté, total 20 s |
| Minuit avec ancre +86 s | FAIL calendrier ; PASS total | 0/20 s au lieu de 10/10 s, total 20 s |
| Veille 1 h, QPC avance | PASS | Veille exclue, total actif 30 s |
| Veille 1 h, monotone simulé arrêté | PASS | Réancrage correct, total actif 30 s |
| Reprise avec correction avant +120 s | PASS | UTC recalée pendant l'inactivité, total 30 s |
| Veille 30 s puis reprise avec correction arrière −120 s | FAIL UTC ; PASS durée | Plancher conservé : avance 90 s ; total 30 s |
| Interruption, checkpoint 45 s, redémarrage 1 h plus tard | PASS | 45 + 20 = 65 s, aucun temps hors processus |
| Redémarrage avec historique futur | FAIL UTC ; PASS durée/non-chevauchement | Avance héritée 105 s, total 65 s |
| Notifications répétées, reprise verrouillée puis déverrouillage | PASS | Aucun double comptage, total 30 s |

**Bilan : 7/12 scénarios PASS, 5/12 FAIL**, tous les échecs portant sur l'UTC
ou la répartition calendaire. Les assertions de durées actives passent dans
les 12 scénarios. Durée du dernier CTest : 0,73 s, code de sortie 8.

Le redémarrage simulé exerce le même `max(UTC système, dernière fin)` puis
`reanchor()` avec des échantillons injectés ; le wrapper de production
`alignWithPersistedTime()`, qui appelle directement les horloges réelles, est
vérifié par lecture du code. Les tests ne prétendent pas simuler le matériel.

La **suite existante est également PASS : 7/7**, 2,12 s, exécutables du build
Release existant. Elle couvre notamment les limites calendaires et les jours
de changement d'heure 23/25 h avec convertisseur déterministe. Les tests UTC
existants vérifient la non-décroissance, sans exiger le suivi d'une correction
système : leur succès ne contredit pas les nouveaux échecs.

## 5. Corrections proposées, sans implémentation

1. **Rendre l'ancrage observable** : journal local borné des couples UTC/QPC,
   dernière borne persistée, ancre choisie et motif (départ, reprise, correction).
   Exposer un relevé de diagnostic permettant une comparaison directe du
   timestamp interne avec Windows. Cela permettra d'identifier le déclencheur
   historique lors d'une prochaine occurrence.
2. **Séparer explicitement durée et heure civile** : conserver le monotone pour
   les durées et une UTC système observée pour les dates. Définir une politique
   de correction avant/arrière et de distribution calendaire avant de changer
   la persistance ; prévoir des segments/époques et une durée mesurée indépendante
   si les corrections imposent des discontinuités de dates.
3. **Détecter les changements** : traiter `WM_TIMECHANGE` et surveiller l'écart
   UTC/monotone avec tolérance aux ajustements fins. Windows diffuse ce message
   lors d'un changement d'heure ([documentation](https://learn.microsoft.com/en-us/windows/win32/sysinfo/wm-timechange)).
   La notification seule ne définit pas la politique de correction.
4. **Revoir la borne historique au redémarrage** : elle protège actuellement
   contre les chevauchements, mais peut perpétuer une date future. Traiter
   explicitement ce cas plutôt que réutiliser silencieusement une pseudo-UTC.
5. **Qualifier la veille moderne** : relever les callbacks réellement reçus
   et vérifier l'exclusion du sommeil sur une session dédiée, sans toucher à
   l'instance actuelle. Maintenir les tests de reprises redondantes.

Un simple appel périodique à `reanchor()` ne suffit pas : la borne interdit
toujours le recul, et un saut vers l'avant au milieu d'un intervalle actif
pourrait gonfler les durées SQLite. Une modification de schéma ou du calcul
des rapports demande une décision distincte ; aucune migration proposée ici
ne doit réécrire automatiquement l'historique existant.

## Rejouer et retrouver les preuves

```powershell
cmake -S scripts/diagnostics/temporal-sync -B build/temporal-sync-diagnostic -A x64
cmake --build build/temporal-sync-diagnostic --config Release --target temporal_sync_tests
ctest --test-dir build/temporal-sync-diagnostic -C Release --output-on-failure
# Le code courant doit exposer les cinq échecs documentés.

# Observation passive ; choisir un nouveau dossier de sortie à chaque exécution.
python scripts/diagnostics/temporal-sync/sample_readonly.py --database "$env:LOCALAPPDATA\Chronox\chronox.db" --output build/temporal-sync-evidence-new --seconds 180
```

Preuves locales ignorées par Git dans `build/temporal-sync-evidence/` :
`live-samples.jsonl`, `live-metadata.json`, `summary.json`,
`readonly-intervals.json`, `windows-clock-events.json`, `windows-power-events.json`,
`timeservice-operational-events.json`, `windows-timeservice-events.json`,
`w32tm-status.txt`, `process-before.json`, `process-after.json`,
`binary-hashes.json`, `source-hashes-after.json`, `deterministic-tests.xml`,
`existing-tests.xml`. Sortie détaillée de chaque test dans
`build/temporal-sync-diagnostic/Testing/Temporary/LastTest.log`.
`analyze_evidence.py` reconstruit `summary.json` à partir des fichiers enregistrés.

Les sources moteur et d'intégration sont inchangées (empreintes contrôlées),
PID 29868 conservé pendant le diagnostic. La suppression locale préexistante
de l'image de référence a été laissée intacte. Ajouts limités au présent
rapport et au banc de diagnostic. Aucun arrêt, installation, changement
d'heure Windows, écriture dans SQLite utilisateur, commit ou push. P1.3.4
n'a pas été lancé.
