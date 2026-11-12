# Chronox E1.4 — livraison A, B et C

Date : 12 novembre 2026. Arrêt obligatoire avant D ; design premium non commencé.

## Commits et périmètre

- A : 60abaa5 — statistiques et activité mesurée.
- B : 57e7c19 — sept durées hebdomadaires et détail à la seconde.
- C : commit contenant ce rapport — lecture paginée, filtre et copie.

TemporalEngine, ActivityTracker, widget, schéma SQLite et règles d'écriture restent inchangés. Les ajouts de stockage sont des SELECT et des surcharges de lecture. Les nouveaux fournisseurs ne font pas avancer le moteur. Aucun lancement du binaire de production, aucune installation, aucun push. Le processus installé 21752 était présent au départ ; il est absent au contrôle final. Les outils de cette tâche ne l’ont ni arrêté ni relancé ; la cause de sa disparition n’a pas été déterminée. Les fichiers Git préexistants non suivis et la suppression préexistante d'une référence PNG sont exclus des commits.

## Définitions

- Session : somme des contributions comptabilisées dans les bornes locales de la période ; jamais calculée à partir de la largeur d'un segment graphique.
- Jours avec session : jours locaux dont la contribution est strictement positive.
- Moyenne mensuelle : durée de session du mois / jours avec session positive. Sans dénominateur : tiret.
- Activité : contributions clavier/souris V3. Non mesurée et activité mesurée nulle restent distinctes. La présence de couverture sur une portion n'atteste pas la couverture complète d'une période. Aucun taux extrapolé ni moyenne d'activité historique.
- Frise : positions dans les intervalles UTC réels, graduations du calendrier local et conservation des règles DST.
- Sessions : contributions selon le modèle existant, début décroissant, toutes années confondues. La session ouverte conserve En cours. Le passage de date est indiqué par (+N j) ; les heures ambiguës conservent leur décalage UTC.
- Filtre : Du et au sont inclusifs en dates locales. Conversion en [minuit local début, minuit local après fin). Sessions en intersection ; durées et bornes limitées à cette plage. Les cartes et graphiques de l'Historique suivent les mêmes bornes. Les graphiques de plages longues regroupent au plus environ 32 fenêtres locales ; chaque groupe somme les contributions.
- Les boutons jour/semaine/mois/année sont conservés. Les flèches de navigation et le calendrier sortent du filtre. Réinitialiser rétablit la période courante de navigation. Cliquer un groupe filtré affine le filtre à ce groupe.
- Pages de 50 lignes : chargement borné, offset recalculé sur les données courantes. Une nouvelle session peut déplacer les pages pendant la consultation. Les agrégats de période réutilisent l'API existante, qui lit les contributions de toute la période.

## Commandes de revue manuelle

Depuis la racine du dépôt, fermer chaque banc avant de reconstruire sa cible :

```powershell
cmake --build build --config Release --parallel 4
cmake --build build --config Release --target chronox_dashboard_review --parallel 4
ctest --test-dir build -C Release --output-on-failure
./scripts/review-dashboard.ps1 -Page Overview -Width 1050 -Height 700 -State reference
./scripts/review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State reference
./scripts/review-dashboard.ps1 -Page History -Width 1050 -Height 700 -State reference
./scripts/review-dashboard.ps1 -Page History -Width 720 -Height 520 -State reference
```

Le banc est autonome : données fictives, aucun moteur, aucune base réelle, aucun registre. Il contient 125 lignes réparties sur plusieurs dates. Tester aussi -State missing, zero, uncertain et disabled. Maximiser manuellement le banc ; vérifier textes, boutons, sept valeurs et frise. À 720 × 520, utiliser le défilement du dashboard pour atteindre les sessions, puis la molette dans la liste pour déplacer seulement ses lignes ; les en-têtes restent fixes dans la liste.

- F1 ou clic sur l'état : définitions et limites de couverture.
- F2 ou clic dans l'histogramme : les sept durées exactes, secondes comprises.
- F4 ou Voir tout l'historique : Historique.
- Tab dans Historique : sélection des dates, Appliquer, Réinitialiser ; Entrée active l'action. Les dates ne s'appliquent qu'avec Appliquer.
- Ctrl+Page suivante/précédente ou boutons de page : autre fenêtre de 50 sessions.
- Glisser sur le texte puis Ctrl+C : sélection ; Ctrl+A sélectionne les textes chargés de la page visible.
- Menu contextuel : copier sélection, ligne, durée ou lignes de la page. F3 copie les lignes chargées (50 maximum) en texte avec tabulations et en-tête. Le clic droit doit viser la ligne pour la copie d'une ligne/durée.

## Validation

- A : build Release et 27/27 CTest PASS après correction de densité verticale.
- B : build Release et 27/27 CTest PASS.
- C : build Release et 27/27 CTest PASS, dernier passage en 13,78 s. Journal : build/review-e14/phase-c-ctest.log.
- Tests ciblés : 125 sessions, pages 50/50/25 sans doublon sur un état fixe, anciennes années, bornes de plage, absence d'écriture de checkpoint, lecture seule, epochs civils qui se chevauchent, activité filtrée et absence de couverture, y compris une couverture qui touche seulement le début de la plage sans la recouvrir.
- Banc automatique : sélecteurs de dates locaux inclusifs, réinitialisation, aucun appel de fournisseur pendant un repaint. Le mode temporisé exerce ces assertions avant ses mesures.
- Un passage de chronox_activity_dashboard_tests a échoué avec son ancien message générique. Un diagnostic avec numéro de ligne a été ajouté ; le test ciblé et la suite complète suivante passent. Aucun invariant n'a été supprimé ou relâché.

## Ressources du banc final

Mesures à DPI 96 (100 %), 16 s de chauffe puis 20 s d'observation, après exercice des dates et des pages :

| Vue | CPU, un cœur | Mémoire privée avant → après | Handles | GDI | USER |
| --- | --- | --- | --- | --- | --- |
| Overview 1050 × 700 | 0,078 % | 37 761 024 → 37 822 464 octets | 1049 → 1049 | 16 → 16 | 26 → 26 |
| History 720 × 520 | 0,078 % | 33 964 032 → 34 025 472 octets | 1050 → 1050 | 16 → 16 | 26 → 26 |

Fichiers : build/review-e14/final-overview-1050.json et final-history-720.json. Stabilité des handles et objets GUI sur cet échantillon ; aucune conclusion de longue durée. Le test existant de 100 masquages/réaffichages reste PASS avec ses invariants stricts de ressources.

## Limites de validation

La capture Windows est indisponible : le service natif répond « native pipe is unavailable ». Les images A et B ne figuraient pas dans la pièce jointe, qui ne contient que le texte. La référence assets/references/chronox-dashboard-reference-fr.png a été consultée. Aucune capture de résultat n'est présentée comme validée.

NON TESTÉ : absence de texte tronqué ou chevauché en inspection visuelle, copie vérifiée depuis une autre application, confort souris réel, maximisation réelle et DPI 150/200 %. Les contrôles automatiques de dimensions et de navigation ne remplacent pas cette revue visuelle.

Les mesures de ressources sont celles du banc fictif, sans garantie sur une longue session de production ou une base de grande taille. Les durées de lecture des agrégats existants sur une très grande plage n'ont pas été profilées.

Verdict visuel : NON CONCLUANT, en attente de validation utilisateur. Ne pas commencer D sans nouvelle autorisation.
