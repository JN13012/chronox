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
- Sessions : contributions selon le modèle existant, début décroissant. La vue générale affiche toutes les contributions de la semaine locale en cours, sans limite de 50 lignes, avec une barre de défilement interne Windows ; les périodes plus anciennes se consultent dans Historique. La session ouverte conserve En cours. Le passage de date est indiqué par (+N j) ; les heures ambiguës conservent leur décalage UTC.
- Filtre : Du et au sont inclusifs en dates locales. Conversion en [minuit local début, minuit local après fin). Sessions en intersection ; durées et bornes limitées à cette plage. Les cartes et graphiques de l'Historique suivent les mêmes bornes. Les graphiques de plages longues regroupent au plus environ 32 fenêtres locales ; chaque groupe somme les contributions.
- Les boutons jour/semaine/mois/année sont conservés. Les flèches de navigation et le calendrier sortent du filtre. Réinitialiser rétablit la période courante de navigation. Cliquer un groupe filtré affine le filtre à ce groupe.
- Dans Historique, pages de 50 lignes : chargement borné, offset recalculé sur les données courantes. Une nouvelle session peut déplacer les pages pendant la consultation. Les agrégats de période réutilisent l'API existante, qui lit les contributions de toute la période.

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

Le banc est autonome : données fictives, aucun moteur, aucune base réelle, aucun registre. Il contient 125 lignes fictives réparties sur plusieurs dates de la même semaine. Tester aussi -State missing, zero, uncertain et disabled. Maximiser manuellement le banc ; vérifier textes, boutons, sept valeurs et frise. À 720 × 520, utiliser le défilement du dashboard pour atteindre les sessions, puis la molette dans la liste pour déplacer seulement ses lignes ; les en-têtes restent fixes dans la liste.

- F1 ou clic sur l'état : définitions et limites de couverture.
- F2 ou clic dans l'histogramme : les sept durées exactes, secondes comprises.
- F4 ou Voir tout l'historique : Historique.
- Tab dans Historique : sélection des dates, Appliquer, Réinitialiser ; Entrée active l'action. Les dates ne s'appliquent qu'avec Appliquer.
- Vue générale : molette ou barre verticale dans Dernières sessions ; F6 donne le focus à cette barre, puis flèches et Page précédente/suivante. Tab ou Échap rend le focus au dashboard.
- Historique : Ctrl+Page suivante/précédente ou boutons de page pour une autre fenêtre de 50 sessions.
- Glisser sur le texte puis Ctrl+C : sélection ; Ctrl+A sélectionne les textes chargés de la page visible.
- Menu contextuel : copier sélection, ligne, durée ou lignes de la page. F3 copie les lignes chargées (toute la semaine dans la vue générale, 50 maximum dans Historique) en texte avec tabulations et en-tête. Le clic droit doit viser la ligne pour la copie d'une ligne/durée.

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

## Correction ciblée — Dernières sessions

Le titre dispose désormais de 26 DIP de hauteur, sans changer sa police ni son alignement. Le tableau de la vue générale affiche uniquement les contributions de la semaine locale (lundi à dimanche), toutes accessibles avec une barre Windows indépendante et la molette. Les en-têtes restent fixes dans cette liste. Les périodes plus anciennes restent dans Historique. Aucun ajout de requête SQLite : les contributions hebdomadaires déjà lues sont réutilisées.

Validation : build Release PASS ; 27/27 CTest PASS (13,66 s). Régressions : 80 sessions hebdomadaires sans limite de 50, exclusion de la semaine précédente, découpage à la frontière du lundi, métriques DirectWrite du titre. Banc fictif avec 125 lignes : défilement jusqu'à la dernière ligne puis retour en haut sans déplacement du dashboard, PASS à 1050 × 700 et 720 × 520 ; filtre inclusif et cache de dessin toujours PASS.

L'installation officielle et son processus observé pendant cette correction n'ont pas été remplacés ni arrêtés. Capture Windows toujours indisponible ; inspection visuelle du résultat à confirmer dans le banc.

## Icône de notification — ouverture et menu

Le clic gauche sur l'icône près de l'horloge ouvre ou réactive immédiatement le dashboard. L'activation clavier suit la même action ; les notifications répétées d'un double clic sont absorbées. Ces activations ne montrent ni ne masquent le widget.

Le menu présente Ouvrir le tableau de bord en premier et comme action par défaut, puis les commandes Afficher/Masquer le widget et Widget toujours au premier plan, le lancement avec Windows et Quitter Chronox. Les séparateurs distinguent l'accès principal, les préférences du widget et la sortie. Les états cochés et les informations de démarrage Windows sont conservés.

Validation : build Release PASS ; 28/28 CTest PASS (les 27 suites existantes et le nouveau test natif d'activation). Test isolé sans base ni écriture de préférences : clic immédiat avant timer, notification d'une autre icône ignorée, double clic absorbé, activation clavier, visibilité du widget conservée et commande du dashboard intacte. L'installation officielle reste inchangée.

## Défilement assorti et correction UTF-8

La barre des dernières sessions conserve son contrôle Win32 pour le focus et les valeurs de défilement, avec un rendu Direct2D personnalisé : piste fine, curseur arrondi de 6 DIP au repos et 8 DIP au survol/focus, cyan pendant l'interaction, sans boutons fléchés blancs. Le glissement, les clics dans la piste, la molette, les flèches clavier et les touches Début/Fin/Page précédente/suivante restent utilisables.

L'encart clavier/souris a été supprimé de la vue générale ; sa place agrandit la liste de sessions. Les détails restent dans Historique et F1. Le libellé devient jours travaillés, avec singulier pour 1 ; sa définition conserve le comptage de jours locaux avec durée de session positive. Aucun changement des règles de comptabilisation.

Cause de l'encodage : fichiers sources UTF-8 sans BOM interprétés selon la page de codes locale de MSVC. Les 66 fichiers sources contrôlés étaient valides en UTF-8 ; l'ancien exécutable contenait réellement la chaîne corrompue du titre. L'option /utf-8 est désormais imposée pour C/C++ sous MSVC. Un test compare accents et ponctuation compilés à leurs codes Unicode explicites, et vérifie aussi l'encodage UTF-8 des chaînes étroites. Le nouveau binaire contient le titre correct et plus sa variante corrompue.

Validation : Release PASS, 28/28 CTest PASS (15,64 s), géométrie du curseur et glissement jusqu'à la dernière ligne puis retour clavier en haut PASS dans le banc à 1050 × 700 et 720 × 520. Journaux et métriques : build/review-e14/premium-scroll-*. Capture Windows encore indisponible ; appréciation visuelle à confirmer dans le banc.

## Proposition graphite — total du jour intégré à la frise

La carte Aujourd'hui et le sous-titre Session du jour local sont supprimés. Un seul panneau Aujourd'hui réunit le total exact (secondes conservées) et les portions de la journée. Les trois synthèses restantes sont semaine, mois, année. La grille mesure les vrais textes et revient à deux puis une colonne pour les valeurs exceptionnelles ; le calcul de hauteur utilise le nombre réel de lignes, y compris une dernière ligne incomplète.

Style : fond graphite, surfaces unies, contours de 0,7 DIP atténués, angles de 12 DIP, espacement de 14 DIP, libellés secondaires neutres et repère cyan limité au panneau du jour. Retrait du dégradé métallique, du halo des cartes et des rayures de la navigation. La frise est amincie à 10 DIP. La navigation et les autres pages reprennent les surfaces sobres.

Validation : Release PASS ; 28/28 CTest PASS (13,93 s). Mesures DirectWrite des durées du jour jusqu'à seconds::max(), de 640 à 1600 DIP, sans recouvrir la frise. Vue ordinaire 1050 × 700 dans la hauteur disponible. Banc natif 1050 × 700 et 720 × 520 : glissement, clavier, absence de déplacement du parent, filtre inclusif et cache de dessin PASS. Résultats sous build/review-e14/graphite-*. Capture visuelle NON TESTÉE : service Computer Use natif indisponible. La proposition est disponible dans le banc de présentation avec données fictives, sans base ni préférences.
