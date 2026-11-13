# Chronox — E2.1 : revue du cadre natif et du défilement

Date : 13 novembre 2026. Arrêt pour revue visuelle avant toute refonte artistique supplémentaire.

## Livré

- Barre de titre entièrement native : DWM immersive dark mode, couleurs graphite/bleu nuit de légende, texte et bordure. Les couleurs sont réappliquées à l'activation et lors des changements de thème/composition. Icône officielle et styles Windows conservés.
- Les attributs DWM sont optionnels : un système incompatible conserve son cadre natif. Le mode de contraste élevé restitue les couleurs système de la légende. Les attributs de couleur sont documentés à partir de Windows 11 build 22000 ([Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)). Aucun attribut interne ni hook système.
- Barre de page : contrôle Win32 SCROLLBAR enfant de 10 DIP, curseur de 6 DIP au repos et 8 DIP au survol/glissement/focus. Rail discret, curseur gris bleuté, focus cyan, aucune animation permanente. Réutilisation de la géométrie DIP des sessions et du calcul contentScroll existant.
- Barre des sessions : mécanique conservée ; curseur harmonisé en gris bleuté. Le focus clavier de cette barre reste distinct de celui de la page.
- Aucun changement artistique supplémentaire : typographie, organisation et surfaces locales existantes conservées. Moteur, stockage et widget exclus des changements E2.1.

## Commandes clavier

- PageUp/PageDown défilent la page ; Home/End atteignent ses extrémités depuis sa scrollbar.
- Maj+F6 donne le focus à la scrollbar de page quand elle est visible. F6 conserve son rôle existant pour le tableau des sessions.
- Dans une scrollbar : flèches, PageUp/PageDown, Home/End ; Tab ou Échap rendent le focus au dashboard.
- La touche Home de la page Historique conserve son action existante de retour à la date courante. Pour atteindre le début de cette page, utiliser Maj+F6 puis Home.

## Validation

| Contrôle | Résultat | Preuve / limite |
|---|---|---|
| MSVC Release, espace de travail | PASS | build/e21/build-release.log et build-review.log ; application et deux bancs compilés |
| Suites CTest existantes | PASS | 28/28 ; build/e21/ctest.log |
| Cadre actif/inactif | PASS | Captures réelles aux deux formats et lecture DwmGetWindowAttribute de CAPTION_COLOR dans le banc |
| Icône et styles natifs | PASS | Assertion du banc sur icône, WS_CAPTION, WS_THICKFRAME, WS_SYSMENU, boutons réduire/agrandir |
| Alt+Espace, Alt+F4 | PASS | Menu système ouvert, fermeture de la seule fixture ; aucune instance installée manipulée |
| Agrandir/restaurer | PASS | Menu système et double-clic de titre, rendu conservé |
| Snap Layouts : conservation | Architecture native conservée | Aucun remplacement de cadre, de hit-test ou de bouton Windows |
| Snap Layouts : panneau au survol | NON TESTÉ | Le survol du bouton n'a pas été exercé avec les outils disponibles |
| Scroll page : rail, glissement, molette et clavier | PASS | Banc 720 × 520 ; glissement et clavier aussi exercés par saisie réelle |
| Scroll sessions | PASS | Rail/glissement/clavier dans le banc, molette réelle sur tableau ; page immobile pendant le scroll des lignes |
| Focus/accessibilité clavier | PASS | Entrée/sortie du focus, contour visible, aucun rectangle natif résiduel ; rôle scrollbar et plage exposés dans l'arbre d'accessibilité |
| Lecteur d'écran complet | NON TESTÉ | Le contrôle conserve son fournisseur Win32 ; aucun audit NVDA/JAWS complet effectué |
| Navigation et redimensionnement | PASS | Test existant : 8 dimensions de 720 × 520 à 1440 × 1000, 3 pages, sélection texte conservée ; 100 cycles masquer/rouvrir sans croissance handles/GDI/USER |
| DPI physique 100 % | PASS | Fenêtres capturées à 96 DPI ; tailles clientes exactes contrôlées par la fixture |
| DPI physique 150 / 200 % | NON TESTÉ | Aucun passage réel sur écran à ces échelles ; aucune modification des réglages Windows |
| Contraste élevé réel | NON TESTÉ | Repli implémenté, réglage système non modifié |

La première exécution après remplacement du scroll a échoué parce que le test lisait encore SB_VERT sur la fenêtre. Le test lit désormais SB_CTL sur le nouveau contrôle ; les bornes sont quantifiées de façon identique dans le contrôle et le contenu. La suite complète est passée après ces corrections.

## Captures

Dimensions exprimées en **zone cliente**, à 100 %. Les images incluent également la barre native et la bordure ; leurs dimensions totales sont donc supérieures. Fixtures de présentation sans SQLite, registre de préférences ou moteur de production. Les captures reflètent aussi les modifications visuelles locales préexistantes, volontairement conservées et exclues des commits E2.1.

| Format | Active | Inactive |
|---|---|---|
| 1050 × 700 | [Capture](e21/dashboard-1050x700-active.jpg) | [Capture](e21/dashboard-1050x700-inactive.jpg) |
| 720 × 520 | [Capture](e21/dashboard-720x520-active.jpg) | [Capture](e21/dashboard-720x520-inactive.jpg) |

[Focus de page et tableau au bas du petit format](e21/dashboard-720x520-focus-bottom.jpg).

À 1050 × 700, le contenu usuel tient dans la page : la scrollbar de page disparaît. Celle du tableau reste disponible. À 720 × 520, la scrollbar de page donne accès au reste du contenu.

## Ressources de la fixture

Mesure après 16 s de chauffe, puis 30 s par format. CPU exprimé en pourcentage d'un seul cœur. Ce résultat inclut la boucle de messages de la fixture ; ce n'est pas une mesure du moteur installé ni une comparaison avant/après du produit.

| Client | CPU un cœur | Mémoire privée finale | Working set final | Handles | GDI | USER |
|---|---:|---:|---:|---:|---:|---:|
| 720 × 520 | 1.72 % | 35.94 MiB | 51.22 MiB | 1136 → 1137 | 20 → 20 | 49 → 49 |
| 1050 × 700 | 1.72 % | 39.09 MiB | 51.42 MiB | 1138 → 1139 | 20 → 20 | 49 → 49 |

La mémoire privée et les compteurs GDI/USER restent constants sur ces échantillons ; un handle supplémentaire apparaît dans chaque intervalle. Cela ne démontre pas une fuite répétée, ni l'absence de toute fuite à long terme. Le test de 100 cycles de réouverture vérifie séparément l'égalité exacte de ses compteurs avant/après.

Données brutes : [720](e21/resources-720.json), [1050](e21/resources-1050.json). Journaux d'interactions : build/e21/review-720.log et review-1050.log.

## Préservation et commits

- f746dbc — feat(ui): theme native dashboard caption with DWM
- 1e4fc59 — feat(ui): add compact dark native page scrollbar
- Contrôle automatisé : les fragments locaux du dashboard restants sont exactement ceux de la sauvegarde initiale ; les autres fichiers UI déjà modifiés n'ont pas été inclus dans les commits.
- Aucune installation, aucun push, aucun arrêt de l'application installée. Les seuls processus ouverts/fermés pour la revue sont les fixtures.
- Les fichiers de préparation et journaux complémentaires sont dans build/e21/ (ignoré par Git).

## Validation autonome des commits

Une archive Git exacte de 1e4fc59 a été extraite dans build/e21/committed-source, puis compilée séparément avec MSVC 19.44 / SDK Windows 10.0.26100.0. SQLite provient du cache source déjà présent ; aucune nouvelle dépendance.

- Release sur les seuls fichiers committés : **PASS**.
- CTest dans cette copie propre : **28/28 PASS** (16,67 s).
- Espace de travail conservant les modifications locales : **28/28 PASS** (14,47 s).

Les commits de code ne dépendent donc pas des modifications locales non commitées. Les captures de la copie de travail conservent volontairement ces modifications locales.

Journaux conservés : [compilation Release](e21/build-release.log), [bancs](e21/build-review.log), [CTest espace de travail](e21/ctest-workspace.log), [CTest commits seuls](e21/ctest-committed.log), [interactions 720](e21/interaction-720.log), [interactions 1050](e21/interaction-1050.log).

Empreintes SHA-256 de la copie de travail validée :

- chronox.exe : 911366894f0f8a5ab1ed5e9a2ff04731a35618ddd26e15fdf15fd6a599745a0b
- chronox_dashboard_review.exe : ed9424f6429570a814b4a5a9ba50828ff6a5682794f34e07b8da17516bd08dea

**Point d'arrêt : prêt pour revue visuelle E2.1.** Les essais physiques 150/200 %, le panneau Snap au survol et l'audit lecteur d'écran restent NON TESTÉS ; aucune approbation visuelle finale n'est déduite des tests automatisés.
