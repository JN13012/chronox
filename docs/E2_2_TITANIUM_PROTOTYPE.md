# Chronox — E2.2 : prototype Titanium Premium

13 novembre 2026. **Prototype isolé de la Vue générale, en attente de validation visuelle.** Le dashboard de production conserve E2.1 ; aucun thème généralisé aux pages Historique ou Paramètres.

## Revue visuelle

[Ouvrir la comparaison avant/après](e22/comparison.html) — fichier local autonome, sans dépendance réseau, avec choix du format.

| Zone cliente à 100 % | Avant E2.1 | Après Titanium |
|---|---|---|
| 1050 × 700 | [Image](e22/before-1050x700.jpg) | [Image](e22/after-1050x700.jpg) |
| 720 × 520, haut | [Image](e22/before-720x520.jpg) | [Image](e22/after-720x520.jpg) |
| 720 × 520, sessions après End | [Image](e22/before-720x520-bottom.jpg) | [Image](e22/after-720x520-bottom.jpg) |

Images incluant le cadre natif : 1052 × 732 et 722 × 552 pixels. Les deux exécutables utilisent exactement le même fichier de fixture et l'état standard : durées 5:27:15, 28:16:00, 134:52:00 et 1234:59:59 ; semaine 5/4/6/3/7/0/2 h ; 125 sessions. L'activité est inactive tandis que la session courante continue : cette distinction V3 est conservée. Les captures comprennent les modifications de présentation locales préexistantes, préservées.

Référence premium examinée : assets/references/chronox-dashboard-reference-fr.png. Inspiration retenue : métal froid, hiérarchie et contours cohérents ; textures, halos décoratifs et réorganisation de la maquette écartés pour ce prototype. Les halos autour du pointeur sur certaines captures appartiennent à l'outil de contrôle, pas au rendu Chronox.

## Palette Titanium Dark

| Rôle | Couleur |
|---|---|
| Graphite profond / bleu nuit | #0D1219 / #101720 |
| Surface, haut / bas du dégradé | #202A35 / #17212B |
| Argent froid, texte principal | #E9EFF5 |
| Texte secondaire | #B3C0CE |
| Bordure / bordure au survol | #465667 / #71889A |
| Cyan / bleu des graphiques | #69D6E4 / #3488B2 |
| Ligne en cours / ligne alternée / survol | #203A4B / #1C2935 / #263746 |

Les textes principal, secondaire et cyan atteignent un contraste d'au moins 4,5:1 sur les surfaces testées, y compris sélection et survol. Les bordures restent volontairement discrètes ; elles ne portent pas seules une information essentielle.

## Modifications

- Cartes : dégradé vertical discret, contour uniforme de 0,75 DIP, arête supérieure à faible opacité et ombre fixe de 2 DIP. Bordure plus lisible au survol, accent cyan limité ; aucun flou, texture ou glow applicatif.
- Typographie : titre 30 DIP, valeurs 24–28 DIP selon la mesure existante, libellés et informations de tableau/graphiques au minimum à 12 DIP. Segoe UI conservée. La petite signature de marque reste non essentielle.
- Chiffres tabulaires : fonction OpenType tnum appliquée aux layouts DirectWrite mis en cache, notamment heures, dates, valeurs et durées. Les chiffres sont testés avec des avances égales. [Documentation DirectWrite](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_font_feature_tag).
- Graphiques : cyan moins saturé, barres homogènes, repères horizontaux discrets et légendes hebdomadaires de 12 DIP. Sept positions, rapports de hauteur, durées et conversions de dates inchangés. Les jours à zéro restent à zéro.
- Tableau : en-têtes et cellules à 12 DIP, hauteurs de ligne conservées, durées alignées à droite, alternance atténuée, ligne en cours discrète et marque cyan fine. Sélection de texte et défilement existants réutilisés.
- En-tête : indication Actif/Inactif présentée dans un badge sobre avec repère de forme. Le libellé et les autres états proviennent toujours de la logique V3 existante ; aucun calcul d'activité ajouté.
- Cadre sombre, icône officielle, scrollbar E2.1 et raccourcis de défilement conservés.

## Isolation et limites

Le prototype est compilé dans **chronox_dashboard_titanium_review**, avec une définition de compilation dédiée. Ses ressources et raccords de rendu sont exclus du binaire normal. Sans ces blocs conditionnels, le fichier dashboard correspond exactement au point de départ E2.1 de la copie de travail, en ignorant les lignes vides. Les sources TemporalEngine, ActivityTracker, stockage SQLite et widget sont inchangées.

Dans le banc Titanium, passer à Historique ou Paramètres restitue le rendu E2.1. Les blocs et leurs géométries ne sont pas réorganisés. À 720 × 520, le tableau nécessite toujours le défilement ; c'est pourquoi une comparaison supplémentaire est fournie en bas de page. Le rail compact de navigation conserve ses abréviations existantes.

Le survol ne redessine qu'au changement de carte/ligne ciblée. Les dégradés et la typographie sont mis en cache et récréés uniquement si nécessaire. Aucun timer ni rendu permanent supplémentaire.

La sélection reste fondée sur les mêmes layouts que le texte rendu. Les chemins existants Ctrl+C et F3 sont conservés ; le presse-papiers de l'utilisateur n'a pas été utilisé pour cette revue.

## Validation

- MSVC Release : **PASS**, produit normal, fixture de référence, fixture Titanium et tests.
- CTest : **30/30 PASS**, soit les 28 suites existantes plus contraste/typographie et interactions du prototype.
- Contraste, chiffres tabulaires, légendes 12 DIP dans les sept colonnes et cellules : **PASS**.
- Sélection de texte, navigation, 8 dimensions et 100 cycles masquer/rouvrir de la Vue générale dans la cible prototype : **PASS**.
- Bancs avant/après, deux dimensions : cadre DWM actif/inactif, icône, styles natifs, rail/glissement/molette/clavier de page et de sessions : **PASS**.
- Contrôle visuel réel : deux formats, focus E2.1, End vers le tableau et sélection d'une durée : **PASS** pour les scénarios exercés. La validation artistique finale reste à faire par l'utilisateur.
- DPI physiques 150/200 %, contraste élevé du nouveau style et audit lecteur d'écran : **NON TESTÉS**. Les captures correspondent à 96 DPI.

Qualification des cycles : une première assertion d'égalité des compteurs a échoué après le renforcement du scénario sur la Vue générale. Le passage isolé a ensuite donné handles 1064 → 1064, GDI 15 → 15 et USER 32 → 32. Le dernier CTest complet donne handles 1070 → 1070, GDI 15 → 15 et USER 32 → 32 pour le prototype. La variation initiale n'a pas été reproduite ni attribuée à une cause démontrée ; aucune correction spéculative du rendu n'a été faite pour la masquer.

[Compilation Release](e22/build-release.log), [compilation des fixtures](e22/build-fixtures.log), [CTest complet](e22/ctest.log), [compteurs des cycles](e22/resource-cycles.log) et données brutes : dossier e22 ; vérification d'isolation reproductible : build/e22/verify_scope.py. Aucun déploiement, migration, accès à la base de production ou push.

## Mesures comparatives de la fixture

16 secondes de chauffe puis 20 secondes de mesure par exécutable et format, avec les mêmes données et le même parcours. CPU en pourcentage d'un seul cœur ; il comprend la boucle de messages de la fixture et les interactions d'instrumentation, pas le moteur installé.

| Variante / largeur | CPU un cœur | Privé final | Working set final | Handles | GDI | USER |
|---|---:|---:|---:|---:|---:|---:|
| before-720 | 2.58 % | 35.51 MiB | 51.25 MiB | 1136 → 1137 | 20 → 20 | 49 → 49 |
| after-720 | 1.48 % | 36.87 MiB | 52.04 MiB | 1136 → 1137 | 20 → 20 | 49 → 48 |
| before-1050 | 1.02 % | 38.23 MiB | 51.14 MiB | 1140 → 1141 | 20 → 20 | 50 → 50 |
| after-1050 | 1.64 % | 39.79 MiB | 51.59 MiB | 1140 → 1141 | 20 → 20 | 50 → 50 |

Le surcoût privé observé est d'environ 1,4 à 1,6 MiB. Un handle apparaît dans chaque intervalle, y compris avant Titanium ; les GDI restent constants. Le test de cycles vérifie séparément l'égalité de ses compteurs avant/après. Les échantillons CPU courts varient selon le format : ils ne démontrent ni un gain ni une absence absolue de régression sur une session longue.

## Reproduire la revue

~~~powershell
cmake --build build --config Release --target chronox_dashboard_review chronox_dashboard_titanium_review
./scripts/review-titanium.ps1
./scripts/review-titanium.ps1 -Width 720 -Height 520
./scripts/review-titanium.ps1 -Baseline
~~~

Point d'arrêt : **revue visuelle du prototype Vue générale uniquement**. Aucune généralisation du design system avant validation.

Commit de code local : 7f380ef — feat(ui): add isolated Titanium overview prototype. Les modifications locales préexistantes sont préservées et exclues de ce commit.
