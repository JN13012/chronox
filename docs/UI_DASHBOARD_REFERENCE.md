# Dashboard — essai de rendu de la référence française

12 novembre 2026. Itération visuelle après les phases UI 1 et 2.

## Référence et portée

Image fournie : `C:\Users\Cyber\Downloads\Tableau de bord Chronox en français.png`, conservée dans [assets/references/chronox-dashboard-reference-fr.png](../assets/references/chronox-dashboard-reference-fr.png).
SHA-256 : `6B41198B7C3534008BE73CD1BC1EEE5DD813C83C84AD4F5B6C8DB66012345A4A`.

Le dashboard reprend le fond anthracite, les panneaux en dégradé acier, les contours fins, les accents cyan, la navigation avec pictogrammes, les cartes de périodes, les barres bleues et les lignes alternées des sessions. Les pictogrammes sont des primitives Direct2D ; trois pinceaux de dégradé sont créés par cible et réutilisés, puis libérés avec cette cible. Aucun shader, framework, animation ou polling supplémentaire.

La référence guide le style, sans imposer ses données de démonstration. Les valeurs, formats de durée et fournisseurs de données existants sont conservés. Chaque carte garde un temps de session principal et un temps d'activité secondaire ; la couverture inconnue reste distincte de zéro. Les avertissements d'incertitude et le mode de détection désactivée sont conservés.

Quatre cartes apparaissent lorsque la largeur intérieure atteint 1000 DIP (notamment à 1440 DIP client). Deux colonnes restent nécessaires à 1050 × 700 et 720 × 520 pour préserver les valeurs à 24–28 DIP. La frise et l'histogramme occupent 180 DIP de haut ; le tableau des dernières sessions, 300 DIP, peut afficher six lignes. Le défilement donne accès aux sections en petit format. Le titre passe à 32 DIP et les textes de navigation à 16 DIP.

Le widget, TemporalEngine, ActivityTracker, SQLite, les fournisseurs dashboard et les règles de comptabilisation ne sont pas modifiés. Aucun lancement du candidat complet, installation, migration, arrêt de l'instance officielle ou push.

## Validation

| Contrôle | Résultat |
|---|---|
| MSVC Release, application et banc isolé | PASS |
| CTest complet | PASS 27/27, dont les 26 suites initiales |
| Scénarios temporels existants | PASS 12/12, moteur inchangé |
| Géométrie et valeurs complètes, 640..1600 DIP, dont 1440 | PASS |
| Textes d'état, titres de graphiques et durées des dernières sessions | PASS, mesure DirectWrite des rectangles |
| Contraste sélection Historique | PASS 7,64436:1 |
| Contraste textes principaux et secondaires sur les deux extrémités des surfaces acier | PASS ≥ 4,5:1 |
| Fenêtres réelles du test : 720×520, 819×520, 820×520, 1050×700, 1440×1000 | PASS, dimensions client et défilement |
| Tab/Maj+Tab Paramètres et 100 cycles afficher/masquer | PASS, handles/GDI/USER stables sur ces cycles |
| Captures Windows automatiques et fidélité visuelle finale | NON TESTÉ : Computer Use ne se connecte pas au canal natif (`os error 2`) |
| DPI physiques multiples, lecteur d'écran et endurance | NON TESTÉ |

Preuve CTest : `build/ui-reference/ctest.xml`. Les mesures de géométrie ne constituent pas une validation esthétique.

## Aperçu isolé pour la revue

```powershell
.\scripts\review-dashboard.ps1 -Page Overview -Width 1440 -Height 1000 -State reference
.\scripts\review-dashboard.ps1 -Page Overview -Width 1050 -Height 700 -State reference
.\scripts\review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State reference
```

L'état `reference` présente six sessions fictives et un état Actif. Le banc rend le véritable dashboard candidat, avec des données en mémoire. Il n'ouvre ni base ni registre et ne lance pas le moteur. Le fermer avec sa croix ; ne pas quitter l'instance officielle depuis la zone de notification.

Pour les six captures Windows des trois pages, suivre [le protocole manuel](UI_PHASES_1_2_MANUAL_REVIEW.md). Ajouter une capture de Vue générale à 1440×1000 et des captures après défilement. Les dimensions sont celles du client en DIP, pas celles du cadre extérieur. L'habillage natif de la barre de titre et de la barre de défilement est conservé ; les effets de lumière de l'image sont approchés par des dégradés et traits statiques, sans reproduction photographique du métal.

Arrêt pour revue visuelle avant tout déploiement ou modification du widget.

## Ressources — mesures courtes avant / après

Vue générale avec les mêmes données fictives standard. Ancien banc : commit `5a0c0f3`, copié avant modification. Nouveau banc : cette itération. Chaque processus chauffe les trois pages, attend 3 s puis mesure 20 s au repos ; acquisitions séquentielles, sans build ni CTest simultané. CPU exprimé en pourcentage d'un cœur, pas du PC complet.

| Largeur | CPU % avant / après | Privé MiB avant / après | Working set MiB avant / après | Handles | GDI | USER |
|---|---|---|---|---|---|---|
| 1050 | 0.781 / 0.937 | 33.95 / 39.49 | 45.54 / 46.00 | 1067 / 1067 | 17 / 15 | 21 / 22 |
| 720 | 0.078 / 0.078 | 31.04 / 31.41 | 43.18 / 43.50 | 1042 / 1042 | 15 / 15 | 20 / 20 |

Une valeur CPU de 0 signifie qu'aucun incrément des compteurs CPU n'a été observé durant l'échantillon, pas un coût de rendu nul. Les variations d'allocateur/working set entre processus ne prouvent ni gain ni fuite. Les compteurs de début et de fin sont conservés dans le JSON, y compris les variations éventuelles pendant l'acquisition ; le contrôle distinct des 100 cycles afficher/masquer vérifie la stabilité exacte des trois catégories de ressources.

Valeurs brutes et empreintes : [UI_DASHBOARD_REFERENCE_RESOURCES.json](UI_DASHBOARD_REFERENCE_RESOURCES.json). Endurance et validation visuelle humaine restent NON TESTÉES. Le candidat complet n'a pas été lancé ; seule la fixture isolée a servi aux essais.

Seules les acquisitions `visible-before-*` et `visible-after-*` entrent dans ce bilan. Les premiers essais avec démarrage masqué sont exclus : le premier ShowWindow pouvait être neutralisé. Une acquisition ultérieure interrompue avec le code `0xC000013A` est également exclue. Le lanceur interactif utilise désormais la console existante au lieu d'ouvrir une console supplémentaire, et le banc centre sa propre fenêtre après redimensionnement. La présence d'une fenêtre visible portant la taille attendue a été vérifiée avant chaque acquisition retenue.

Écart maximal de mémoire privée entre processus : +5.55 MiB. Ces mesures courtes permettent de rechercher une régression évidente, pas de conclure sur une fuite à long terme.

À 1050 DIP, hausse observée de +0,156 point de CPU sur un cœur et +5,55 MiB privés. Cette hausse de mémoire dépasse la marge de +5 MiB proposée dans la livraison précédente : **ce critère de performances n'est pas validé sur cet échantillon**. À 720 DIP, CPU comparable et +0,37 MiB privés. Les handles finaux sont identiques entre versions ; GDI/USER diffèrent de quelques objets à 1050 DIP et sont identiques à 720 DIP. Une mesure stabilisée plus longue sera nécessaire avant d'autoriser un déploiement ; ce rendu reste un candidat à la revue visuelle.
