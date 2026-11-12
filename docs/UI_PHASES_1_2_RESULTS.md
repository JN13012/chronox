# Chronox — Livraison UX/UI, phases 1 et 2

Date utilisateur : 12 novembre 2026. Arrêt avant revue visuelle et avant modification du widget.

## Résultat

**PASS technique ; revue visuelle Windows en attente.** Les captures automatiques sont indisponibles et ne sont pas déclarées PASS. Le protocole manuel demandé est prêt dans [UI_PHASES_1_2_MANUAL_REVIEW.md](UI_PHASES_1_2_MANUAL_REVIEW.md).

Commits locaux :

1. `5cde795 refactor(ui): centralize visual tokens and layout`
2. `fix(ui): improve dashboard readability and keyboard navigation` (commit de cette livraison).

Aucun push ni déploiement. L'instance officielle est toujours PID24500, lancée le11/11 à09:52:20+01:00 depuis `%LOCALAPPDATA%\Programs\Chronox\chronox.exe`. Son SHA-256 demeure `52B4DEE259F9991C9B6FE50B6AE1A93154B0FDCEA7B4C1C2BF2A7106466BAF44`.

## Changements livrés

- `ui_theme.h` : palette sémantique, espacements, tailles et poids typographiques. `ui_text.h` : création centralisée des formats DirectWrite et mesure des valeurs avant rendu.
- `dashboard_layout.h` : règles de disposition partagées. La barre latérale grandit progressivement entre720 et1050 DIP ; la largeur utile ne diminue jamais entre640 et1600. Aucun seuil brutal à820 ou760.
- Dashboard : cartes sur deux colonnes aux deux tailles demandées ; chiffres principaux28 DIP, ajustés par mesure réelle à26 ou24 si nécessaire. Aucun chiffre ni unité supprimé, aucune ellipse, aucune diminution sous24 DIP. Les valeurs longues peuvent occuper deux lignes.
- Hiérarchie : période, libellé TEMPS DE SESSION, grande valeur, puis activité clairement nommée en14 DIP. Historique : deux grandes cartes session/activité, puis statistiques secondaires.
- Contenu vertical défilable avec barre native, molette et Page précédente/suivante ; en petit format, les graphiques et les dernières cartes restent accessibles sans réduire les caractères. La molette sur le tableau Historique conserve le défilement des sessions.
- Navigation Historique : onglets de période sur une rangée, date/précédent/suivant/aujourd'hui sur la suivante. Les hit-tests suivent le défilement ; le calendrier est ancré à la bonne position et au bon DPI.
- Sélection : fond bleu dédié `#1B4F86`, texte `#CAD7E4`, contraste calculé **5,71957:1**, objectif4,5:1 satisfait.
- Tous les graphiques sont nommés SESSION ; aucune série d'activité inventée.
- Disponibilité : non mesurée reste distinct de zéro mesuré. La couverture complète n'est jamais affirmée à partir du seul booléen de disponibilité ; un zéro concerne seulement la partie mesurée. Aucun pourcentage de couverture calculé.
- Incertitude : explication sur Vue générale **et** Historique ; portée éventuellement antérieure explicitée, sans renvoi vers une page supposée contenir un détail inexistant.
- Paramètres : Tab/Maj+Tab traversent les contrôles puis rejoignent la navigation ; aucun piège lorsque le délai est désactivé. La police native suit le DPI et les ressources de police sont réutilisées/libérées.
- Banc `chronox_dashboard_review` et lanceur `scripts/review-dashboard.ps1` : rendu réel du dashboard avec fixtures, paramètres locaux au processus, aucun moteur, base ou registre. États standard/non mesuré/zéro/incertain/désactivé disponibles pour la revue.

La géométrie réserve explicitement la largeur de la barre de défilement pour obtenir exactement les dimensions client demandées. L'API d'ajustement Win32 ne réserve pas cet espace automatiquement : [documentation Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrectex), complétée par sa [version DPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrectexfordpi). Les tests vérifient la taille réelle avec GetClientRect.

## Validation

| Contrôle | Résultat |
|---|---|
| Étape1 : MSVC Release et26suites conservées + test ajouté | PASS27/27 |
| Étape2 finale : MSVC Release, cible complète ALL_BUILD et banc de revue | PASS |
| CTest final | **PASS27/27**, dont les26préexistantes |
| Scénarios temporels existants | PASS12/12, moteur inchangé |
| Disposition640..1600 DIP, absence de recul de largeur utile | PASS |
| Valeurs DirectWrite à640/720/759/760/819/820/1050/1370/1600 DIP, jusqu'à un cumul extrême | PASS,24–28 DIP sans perte de glyphes |
| Textes d'état, couverture, incertitude, activité secondaire | PASS, mesure DirectWrite des rectangles |
| Contraste sélection ≥4,5:1 | PASS5,71957:1 |
| Tab/Maj+Tab avant/arrière, sortie et délai désactivé | PASS sur HWND propre au test, état clavier limité au thread |
| Client réel1050×700,720×520,819×520,820×520 et défilement trois pages | PASS sur fenêtres de test |
| Réutilisation des contrôles,100cycles afficher/masquer | PASS, handles/GDI/USER inchangés entre les deux mesures du test |
| Captures réelles des trois pages / deux tailles | **NON TESTÉ**, protocole manuel fourni |
| Revue esthétique humaine, Narrateur, multi-écrans/DPI physiques et longue durée | **NON TESTÉ** |

Commandes exécutées :

```powershell
cmake --build build --config Release --target ALL_BUILD chronox_dashboard_review
ctest --test-dir build -C Release --output-on-failure --output-junit ui-phases12/ctest-phase2.xml
```

Les tests de disposition mesurent les glyphes DirectWrite, pas une approximation de largeur de police. Les espaces de fin de ligne ajoutés par le retour automatique ne sont pas des glyphes : leur largeur n'est pas assimilée à une troncature de valeur.

Trace JUnit : `build/ui-phases12/ctest-phase2.xml`. Les tests Win32 agissent uniquement sur leurs propres fenêtres et données. Aucune entrée clavier/souris globale n'est produite par ces tests.

## Performances

Les mesures exactes sont conservées dans [UI_PHASES_1_2_RESOURCES.json](UI_PHASES_1_2_RESOURCES.json). Elles portent sur le **banc de présentation**, pas sur une installation du nouveau moteur. Base de comparaison : rendu de l'étape1, équivalent au rendu initial pour les couleurs/formats/disposition. Même fixture, mêmes pages et dimensions ; trois secondes d'échauffement puis20secondes par page. CPU exprimé en pourcentage d'un cœur, mémoire privée et working set en MiB, handles et objets GDI/USER par processus.

Le banc pompe ses propres messages toutes les10ms : ce coût de mesure existe des deux côtés et ne correspond pas à une nouvelle boucle du produit. La comparaison est séquentielle, sur une machine partagée, sans isolation des autres programmes ; les petites différences sont à interpréter avec cette limite. Les acquisitions finales sont faites sans compilation/CTest simultanés. Les premiers relevés exploratoires sont conservés dans `build/ui-phases12/after-*.json` mais ne sont pas utilisés comme validation finale.

### Comparaison finale — avant / après

Valeurs mémoire et objets prises à la fin de chaque fenêtre, pas des pics. CPU moyen en % d'un cœur.

| Page / largeur client | CPU % | Privée MiB | Working set MiB | Handles | GDI | USER |
|---|---:|---:|---:|---:|---:|---:|
| Vue generale 1050 | 0.078 / 0.078 | 31.57 / 26.87 | 46.52 / 37.16 | 759 / 758 | 14 / 14 | 16 / 16 |
| Historique 1050 | 0.234 / 0.156 | 27.05 / 28.50 | 37.02 / 38.30 | 756 / 758 | 14 / 14 | 16 / 16 |
| Parametres 1050 | 0.078 / 0.156 | 27.44 / 28.29 | 37.15 / 37.20 | 756 / 758 | 14 / 14 | 16 / 16 |
| Vue generale 720 | 0.234 / 0.312 | 24.90 / 25.31 | 39.33 / 39.64 | 781 / 783 | 14 / 14 | 18 / 18 |
| Historique 720 | 0.156 / 0.078 | 24.86 / 24.92 | 39.77 / 37.38 | 781 / 758 | 14 / 14 | 18 / 16 |
| Parametres 720 | 0.156 / 0.078 | 24.75 / 24.82 | 38.14 / 37.37 | 756 / 758 | 14 / 14 | 16 / 16 |

Écart maximal CPU : **+0.078 point** d'un cœur ; mémoire privée : **+1.45 MiB**. Aucun dépassement des seuils proposés de+0,2point CPU et+5MiB privés sur ces échantillons. GDI stable ; les écarts de handles entre processus ne constituent pas une tendance de fuite. Les acquisitions montrent souvent+1handle avant/après20s aussi bien sur la base que sur le candidat ; cette initialisation différée commune n'est pas assimilée à une fuite ni effacée du rapport.

**PASS pour l'absence de régression évidente sur ces mesures courtes.** Le test100cycles vérifie séparément l'égalité des handles/GDI/USER avant/après. Les variations de working set et d'allocateur ne prouvent pas un gain mémoire causé par la refonte ; aucune qualification longue durée n'est revendiquée.

## Captures et point d'arrêt

La compétence Computer Use a été réessayée ; `sky.list_windows()` échoue avec `Computer Use native pipe is unavailable ... (os error 2)`. Aucun contournement d'automatisation de l'instance installée.

Pour démarrer la revue manuelle :

```powershell
.\scripts\review-dashboard.ps1 -Page Overview -Width 1050 -Height 700
```

Voir le protocole lié pour les six captures, les captures après défilement, les états et le parcours clavier. Fermer uniquement la fenêtre intitulée « revue isolee » après chaque essai.

## Préservation

Comparaison à `2668768` : aucun changement dans `src/core/`, `src/storage/`, `main.cpp`, `dashboard_data.*`, `chronox_widget.*`, `widget_presentation.*`, ressources graphiques ou règles temporelles. Les fichiers modifiés concernent uniquement le dashboard, son thème/disposition/messages/géométrie, les tests, CMake, le lanceur de revue et cette documentation.

La suppression locale de l'image « métal précieux », l'audit préexistant, les documents temporels et scripts de diagnostic non suivis restent hors des commits. Aucune lecture/écriture de la vraie base n'a été nécessaire à cette livraison ; les tests utilisent leurs fixtures et bases temporaires. Aucun réglage réel modifié.

Candidat Release non installé :

```text
AD5965FD1242196689839A018A0502C8A009EBA49506EC3E648B1D6B7E560357
```

Banc de revue :

```text
BEE8E27095E26D517A408372CA43220057F91A93A0A80916F939ED0488FCD394
```

**Arrêt après les phases1–2, avant modification du widget et avant installation.** Le PASS technique ne remplace pas votre revue visuelle.
