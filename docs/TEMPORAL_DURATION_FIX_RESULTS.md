# Correction de duration() — 10 novembre 2026

**Verdict : PASS pour le correctif et la répétition isolée.** Aucune mise en
service réelle effectuée ; autorisation explicite toujours requise.

## Correction

Les deux surcharges publiques de `SessionDatabase::duration()` délèguent à
`queryHistory().sessionDuration` lorsque la base est V2 ou le mode ReadOnly.
Il existe ainsi une seule interprétation des contributions durables V1, des
spans V2 et de la contribution V2 active en RAM. Le span courant remplace son
checkpoint enregistré sans être ajouté deux fois.

Le chemin SQL historique est explicitement nommé `legacyDuration()` et reste
réservé à Legacy V1. Sa sémantique est conservée : agrégation de toute la période
enregistrée et plancher du checkpoint actif. Pour ce mode, une ancre placée avant
la fin d'un intervalle clos ne limite pas `duration()` à cette ancre ; sa référence
`queryHistory(period, anchor, now)` doit utiliser un `now` couvrant la partie
enregistrée comparée. Les tests existants de périodes complètes restent inchangés.
ReadOnly V1 utilise uniquement la borne durable des sessions interrompues et
n'extrapole aucune activité. Aucun changement de moteur ni de schéma SQLite.

## Résultats

| Vérification | Verdict |
|---|---|
| Build MSVC Release de l'application et du validateur console | PASS |
| CTest intégral avec régressions ajoutées aux suites existantes | PASS — 22/22, 6,07 s |
| Scénarios temporels inclus dans CTest | PASS — 12/12 |
| Deux surcharges, V1 Legacy/ReadOnly, V2 seule et mixte | PASS |
| Activité RAM, checkpoint idempotent, queue non durable exclue à la reprise | PASS |
| Redémarrage avec UTC historique futur, corrections UTC avant/arrière | PASS |
| Frontière commune jour/semaine/mois/année et précision à la microseconde | PASS |
| Convertisseur injecté, journées DST de 23/25 h | PASS |
| Rejeu des 24 anciens écarts, par phase/période/bornes | PASS — 24/24 |
| Répétition sur une nouvelle copie de la sauvegarde figée | PASS |
| Cumuls comparés indépendamment et jours actifs / sessions | PASS — 144/144 |
| Cases du fournisseur du dashboard comparées indépendamment | PASS — 936/936 |
| Intervalles V1, colonnes, valeurs et NULL inchangés | PASS — 46/46 |
| Intégrité, backups et rollback de migration injectée sur copies | PASS |
| Durées négatives / chevauchements monotones intra-run sur la répétition | PASS — zéro |
| Processus, binaire installé, ressources, raccourci, Run et préférences | PASS — inventaires identiques |
| Installation réelle, interface candidate, veille/reprise matérielles | NON TESTÉES |

Les durées réelles de la répétition sont simulées : 10 s checkpointées,
5 s supplémentaires en RAM abandonnées, puis un nouveau run de 7 s. La somme
récupérée finale est exactement 17 s. Les chevauchements civils entre runs
restent permis. Les tests de veille simulés ne qualifient pas le matériel.

## Preuves et candidat

Preuves privées exclues de Git sous `build/duration-fix-evidence/` :
`ctest.xml`, `previous-24-comparisons.json`, `candidate.json`, inventaires et
comparaison de l'environnement ; `rehearsal/validation.json`, `reports.tsv`,
`buckets.tsv`, bases de copie, archives et logs.

La répétition part exclusivement de
`build/deployment-preflight/rehearsal-20261110/user-v1-snapshot.db`.
Cette sauvegarde est restée intacte, SHA-256 :
`bb238ec118058099cf04e8271d72cca0f1390c72a3d4a52348ab633d46f39179`.
Empreinte des 46 lignes V1 avant/après :
`60a043d0f2180b347a4a3919a8cec8e25f427dbc3dc903979ce11b2b08776f13`.

Candidat Release : `build/src/Release/chronox.exe`.
SHA-256 : **`ED9E90B021B93F2EBA50F69E149A73799F17FDE87FD194D76AB6332C4E309D42`**.

Le FAIL de préparation du rapport précédent est levé pour cette incohérence.
L'installation réelle reste distincte de cette validation : aucun arrêt de
l'instance installée, aucun accès à la vraie base par les essais, aucune migration
réelle et aucun push. Les limites matérielles et les précautions de sauvegarde /
retour arrière restent applicables. Arrêt avant déploiement pour validation.
