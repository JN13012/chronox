# Chronox V3 — Audit UX/UI avant refonte

Date : 11 novembre 2026. Périmètre : présentation uniquement, aucune implémentation.

## Verdict et niveau de preuve

**À CORRIGER avant validation graphique. Audit statique terminé ; qualification visuelle Windows NON CONCLUANTE.** Les constats ci-dessous séparent ressources effectivement regardées, code lu, calculs géométriques et essais non exécutés. Ils ne remettent pas en cause la validation du moteur V3.

- HEAD vérifié : `26687685951a6b30a24519a253d372bba733dab2`.
- Instance officielle observée : PID 24500, `%LOCALAPPDATA%\Programs\Chronox\chronox.exe`. SHA-256 : `52B4DEE259F9991C9B6FE50B6AE1A93154B0FDCEA7B4C1C2BF2A7106466BAF44`.
- `ctest --test-dir build -C Release -N` : 26 suites présentes. **Pas de réexécution** ni de nouveau résultat 26/26 revendiqué pendant cet audit documentaire.
- Aucun arrêt/lancement d'application, changement de préférence, accès SQLite ou changement du moteur. Aucun push ni commit.
- Capture et navigation Windows **NON TESTÉES** : tentative `sky.list_windows()` via la compétence Computer Use ; erreur `Computer Use native pipe is unavailable ... (os error 2)`. Aucun contournement par injection Win32.
- `assets/references/screenshot.png` a été regardée : c'est une capture ancienne du 02/11, avec des libellés différents du code actuel. Elle illustre l'intégration des ressources, mais **ne constitue pas une capture de V3**.

## 1. Inventaire des composants existants

| Composant | Implémentation / ressources | Comportement établi par le code |
|---|---|---|
| Widget transparent | `src/windows/chronox_widget.{h,cpp}` | Fenêtre Win32 layered/toolwindow/noactivate ; WIC, Direct2D sur bitmap, composition alpha ; canevas 250 × 250 DIP. |
| Habillage | `assets/ui/chronox-shell.png`, `chronox-dial-background.png` | Deux PNG RGBA 1254 × 1254 ; coque à l'échelle du canevas, fond dans un rectangle 200 × 200 centré. Ressources provisoires documentées dans `UI_SPEC.md`. |
| Compteur widget | `widget_presentation.{h,cpp}` | Aujourd'hui / Semaine / Mois, cumul de **session**, secondes affichées ; session courante, état du suivi et incertitude. Aucun champ d'activité dans `WidgetSnapshot`. |
| Navigation widget | `chronox_widget.cpp`, `widget_preferences.*` | Flèches, trois points ; page mémorisée en registre. Clic compteur différé par `GetDoubleClickTime`, double-clic dashboard ; glisser hors compteur/flèches ; languette de repli. |
| Rétraction et notification | `chronox_widget.cpp`, `windows_startup.*` | Languette 34 × 76 DIP ; menu afficher/masquer, dashboard, premier plan, démarrage, quitter ; pas de mode agrandi implémenté. |
| Coordination | `window_coordinator.*`, `main.cpp` | Un dashboard créé à la demande puis réutilisé ; fermeture masque la fenêtre ; raccordement aux snapshots et commandes existantes. |
| Dashboard | `dashboard_window.{h,cpp}` | HWND + Direct2D/DirectWrite ; Segoe UI ; client initial 1050 × 700 DIP, minimum demandé 640 × 420, ajusté à l'écran. |
| Navigation générale | `dashboard_window.cpp` | Trois pages ; barre latérale 232 DIP ou 72 sous 820 ; focus dessiné, Tab/flèches/Entrée/Espace ; historique réserve certaines touches. |
| Vue générale | `drawOverview`, `drawTimeline`, `drawWeekChart`, `drawRecentSessions` | Quatre cartes jour/semaine/mois/année avec session et activité ; état V3 ; frise de session, barres hebdomadaires de session, dernières sessions. |
| Historique | fonctions `drawHistory*` | Jour/semaine/mois/année ; précédent/suivant, date via MonthCalendar Win32, aujourd'hui ; quatre résumés, frise/grille/calendrier, sessions défilables. |
| Paramètres | `layoutActivityControls`, `activity_preferences.*` | Checkbox et ComboBox Win32 ; activation, 1/5/10/15/30 min ; application immédiate via commande existante. Texte expliquant le mode désactivé et la confidentialité. |
| Projection de données | `dashboard_data.*` | Agrégats session/activité distincts, indicateurs de mesure disponible, état et incertitude. Graphiques/buckets actuels exposés pour la session ; activité historique agrégée uniquement. |
| Ressources Windows | `chronox.rc`, `resource.h`, `assets/icons/` | Icône native ; les illustrations du widget sont des ressources externes. |
| Tests liés à l'UI | `widget_presentation_tests`, `widget_preferences_tests`, `dashboard_data_tests`, `activity_dashboard_tests`, `activity_preferences_tests` | Formatage, navigation logique, projections, commandes de paramètres, états, réutilisation des contrôles, cycles afficher/masquer et ressources. Pas de certification esthétique ou de matrice de captures. |
| Banc manuel existant | `src/tests/dashboard_window_smoke.cpp` | Cible `chronox_dashboard_smoke`, hors build par défaut ; fixtures et option base. À utiliser sans base réelle ; fixture actuelle ne démontre pas tous les états d'activité V3. Non lancé ici. |

La minuterie widget est de 1 s seulement lorsqu'il est déployé, visible et que le suivi compte. Le dashboard a un rafraîchissement de 15 s et reçoit aussi les notifications applicatives. Préserver ces principes et l'absence de boucle graphique permanente.

## 2. Références et comparaison du widget

### Références disponibles et effectivement examinées

- [Chronomètre Chronox futuriste en titane](../assets/references/Chronomètre%20Chronox%20futuriste%20en%20titane.png), SHA-256 `B8460C1220D27FE01B24933E9E44A019BDC15335D0CC9EF8C0784A4E03C4C695`.
- [Référence nommée dans UI_SPEC](../assets/references/chronox-widget-reference.png), SHA-256 `6435987CE01D83F2F03353C7B2881CA927C5B014747D29EF901E29DC3FA935AF`.
- Les deux images ne sont pas identiques : positions des flèches, libellés horaires, organisation basse et graduations diffèrent. Conserver la version titane comme référence de comparaison demandée ; faire approuver une composition unique avant production.
- Une autre image, `Chronomètre futuriste CHRONOX en métal précieux.png`, est déjà supprimée localement. Ne pas la restaurer : **la référence titane demandée est disponible**.

| Sujet | Référence titane observée | Widget actuel établi par ressources/code | Direction proposée |
|---|---|---|---|
| Proportions | Grand boîtier circulaire, couronne haute, poussoir oblique, espace intérieur structuré | Canevas carré 250 DIP incluant couronne/halo ; cadran bitmap 200 DIP ; panneau central 154 × 87 DIP | Conserver la silhouette ; réserver séparément les zones portrait, valeurs, état et navigation ; valider au format réel. |
| Temps principal | Gros chiffres au centre, période au-dessus, informations secondaires en dessous | 25 DIP au jour, 19 semaine/mois ; texte long `X h Y min Z s` dans 146 DIP sans retour à la ligne | Priorité aux chiffres, unités plus discrètes ; chiffres tabulaires ; format stable et test des grands cumuls sans modifier les valeurs. |
| Portrait Chronos | Visage lisible décalé à droite, relief visible derrière les informations | Illustration adaptée au langage visuel ; panneau sombre d'opacité 0,86 posé de y=91 à178 masque une partie du portrait | Recomposer le cadrage et les fonds locaux ; garder un visage reconnaissable sans réduire le contraste des chiffres. |
| Cadran / graduations | Chiffres romains, repères fins, anneaux et libellés de début/fin | Coque métallique et portrait seulement : pas de graduations/arcs horaires dessinés dans `drawExpanded` | Dessiner des repères décoratifs cohérents en vectoriel ; aucun faux arc de mesure ou d'objectif. |
| Matières / lumière | Métal brossé argenté, cadran sombre, éclairage bleu/cyan marqué | Même famille métallique et bleue ; halo inclus dans les PNG, rendu réduit à environ 1/5 de la résolution source | Garder les textures statiques, contrôler reflets et halo sur fonds clair/sombre/texturé ; ne pas ajouter de 3D permanente. |
| Navigation | Flèches basses et points dans la variante titane | Flèches proches du titre, cibles 20 × 18 DIP ; pages jour/semaine/mois, pas année | Cibles au moins 32 × 32 DIP proposées ; rendre les périodes explicites ; ajouter Année si approuvée, via cumul existant. |
| État | Capsule « Session active », voyant, durée secondaire | « Suivi actif / Suivi en pause / Mesure incertaine » ; `counting` représente la session | Séparer « Session en cours » de « Activité : Actif / Inactif / Verrouillé / Veille / Incertain ». Ne jamais déduire l'activité de `counting`. |
| Temps d'activité | La référence artistique précède la distinction V3 | Pas affiché dans le widget ; correctement présent dans le dashboard | Session reste la mesure principale proposée ; activité identifiée en secondaire ou vue dédiée, choix à approuver. |
| Mode rétracté | Non spécifié par l'image seule | Languette 34 × 76 avec chevron, sans valeur/état ni accès clavier propre | Préserver la discrétion ; nom accessible, état textuel dans le menu/infobulle, accès clavier par une voie native. |
| Interactions | L'image ne définit aucun geste exécutable | Fenêtre sans activation, clic/double-clic/glisser/repli avec zones différentes ; pas de gestion clavier du widget | Éviter les conflits, documenter les actions, fournir un accès explicite au dashboard ; ne pas rendre l'application intrusive. |

Les chiffres horaires incohérents/doublés de l'image titane sont des défauts de référence artistique à ne pas recopier. Les mentions « Objectif 8h / 70% », horaires 08–17 et anneaux ne sont pas des fonctionnalités existantes. Leur éventuelle introduction demande une spécification distincte ; ils ne font pas partie de cette refonte de présentation.

## 3. Problèmes classés par priorité

P1 : compréhension ou accès à une fonction essentielle. P2 : lisibilité/adaptation. P3 : finition. Aucun P0 de perte de données identifié dans ce périmètre ; ce n'est pas un nouvel audit du stockage.

| ID | Priorité | Problème et effet | Preuve / statut | Critère de correction |
|---|---|---|---|---|
| U01 | P1 | Widget « Suivi actif » alors que l'utilisateur peut être inactif ; absence des états V3 | `WidgetSnapshot` et `drawExpanded`, constat statique | Deux sémantiques clairement nommées ; 5 états fournis par le moteur, mode détection désactivée explicite. |
| U02 | P1 | Navigation clavier de Paramètres boucle parent → checkbox → délai → parent ; Tab parent repart sur checkbox, même avec Maj | `settingsControlProcedure` et WM_KEYDOWN ; séquence déduite, essai réel NON TESTÉ | Parcours avant/arrière permettant de sortir vers la navigation sans souris ni fermeture ; focus toujours visible. |
| U03 | P1 | Graphiques proches des deux compteurs mais sans mention explicite « session » ; couleurs pouvant suggérer l'activité | Frise et semaine utilisent uniquement les données session | Titre/légende « Temps de session » sur chaque graphique ; activité seulement si véritable projection disponible. |
| U04 | P1 | Message permanent « couverture partielle » dans Vue générale, absence de précision de couverture dans Historique | Chaîne inconditionnelle `drawOverview` ; résumé historique ne distingue que mesuré/non mesuré | Explication exacte de l'indicateur disponible ; zéro, absence de mesure et incertitude distincts ; ne pas inventer un taux de couverture. |
| U05 | P1 | Alerte « Mesure incertaine (voir Historique) » sans détail visible de cette incertitude dans la page cible | `timeUncertain` affiché dans Vue générale ; pas de panneau explicatif Historique repéré | Explication accessible, portée connue, durées certaines conservées ; aucune nouvelle déduction ou réparation de données. |
| U06 | P1 | Onglets historiques sélectionnés : texte clair sur bleu trop peu contrasté | Couleurs opaques du code : ratio 2,27:1 | Cible proposée 4,5:1 pour petits textes ; vérifier aussi sélection, focus et contrôles natifs sur captures. |
| U07 | P2 | KPI principaux rendus avec la même police 10 DIP que légendes/navigation compacte | `createTextFormats`, `drawOverview`, `drawHistorySummary` | Chiffres 24–32 DIP proposés, corps 14–16, légendes 12–13 ; mesure DirectWrite des chaînes extrêmes avant acceptation. |
| U08 | P2 | 4 cartes restent sur une rangée en petit format, labels et durées peuvent être coupés | Géométrie calculée, DrawText CLIP ; découpe exacte des glyphes NON TESTÉE | Repli 2 × 2 selon largeur utile ; ni troncature d'un temps ni chevauchement. |
| U09 | P2 | À 820 DIP, élargir la fenêtre réduit brutalement le contenu de 159 DIP | Seuil sidebar et géométrie calculés ci-dessous | Définir le mode selon largeur minimale du contenu ; tester les deux côtés de chaque seuil. |
| U10 | P2 | Historique calendrier : cellules de 13 DIP à faible hauteur, plusieurs informations sur une ligne | Calcul des 6 rangées ; hauteur minimale, dates/durées peu confortables | Cellules sélectionnables lisibles ; détails séparés ; défilement vertical si nécessaire. |
| U11 | P2 | Graphique hebdomadaire relatif au maximum, sans valeurs numériques ni axe quantifié ; dimanche cyan sans légende | `drawWeekChart` | Valeurs ou lecture détaillée accessible ; expliquer l'échelle et la couleur ; ne pas faire croire que cyan = activité. |
| U12 | P2 | Petites flèches widget, aucun focus clavier propre ; pointeurs/infobulles non établis | Cibles 20 × 18, WS_EX_NOACTIVATE ; inspection réelle NON TESTÉE | Cibles 32 DIP proposées, retour visuel, nom accessible et voie clavier équivalente. |
| U13 | P2 | Latence du clic compteur jusqu'au délai système de double-clic ; action peu découvrable | `queueWidgetClick` avec `GetDoubleClickTime` | Commande explicite dashboard ; geste et glisser non ambigus ; test physique clic/double-clic. |
| U14 | P2 | Paramètres mélange contrôle Win32 DEFAULT_GUI_FONT et texte DWrite sur thème sombre ; police native non reconstruite au DPI | `layoutActivityControls` et WM_DPICHANGED | Police contrôlée par DPI, couleurs/contraste et alignement cohérents tout en conservant la sémantique native. |
| U15 | P2 | Accessibilité des zones custom : pas de fournisseur UIA / WM_GETOBJECT propre repéré ; calendrier et cellules sans parcours complet démontré | Recherche statique ; lecteur d'écran NON TESTÉ | Noms, rôles, valeurs, focus et activation clavier pour zones actionnables ; vérifier Narrateur sur Windows. |
| U16 | P3 | Panneau rectangulaire dominant, portrait occulté, pas de graduations, décalage entre référence/documentation/V3 | Ressources regardées et rendu lu | Composition approuvée en compact et agrandi ; documentation mise à jour sans copier les défauts de l'image. |

### Contrastes calculés

Calcul sRGB : linéarisation par composante puis luminance `0,2126 R + 0,7152 G + 0,0722 B`, ratio `(Lmax + 0,05)/(Lmin + 0,05)`. Ce sont des ratios des couleurs opaques déclarées, pas une certification d'accessibilité globale.

| Paire | Ratio |
|---|---:|
| Texte `#CAD7E4` / panneau `#1D2A3C` | 9,91:1 |
| Secondaire `#91A1B4` / panneau | 5,50:1 |
| Cyan `#54D7F5` / panneau | 8,57:1 |
| Texte `#CAD7E4` / accent `#248DFF` | **2,27:1** |
| Bordure `#304054` / panneau | 1,37:1 |

Les bases sombres sont utilisables ; l'écart majeur concerne les petits textes sur accent. Une bordure décorative peut rester discrète ; les contours nécessaires à l'identification d'un contrôle doivent être plus visibles. Le widget translucide exige des mesures sur captures compositées : aucun ratio réel revendiqué ici.

## 4. Adaptation du dashboard

**Convention : dimensions CLIENT en DIP**, pas dimensions extérieures de fenêtre. À 100 %, 1 DIP = 1 px ; les bordures Win32 s'ajoutent. Les demandes 1050 × 700 et 720 × 520 devront aussi être vérifiées en dimensions extérieures si c'est la taille attendue par l'utilisateur.

Recalcul direct des constantes et rectangles, sans simulation de rendu :

| Client | Largeur intérieure des cartes | Largeur d'une carte de Vue générale | Hauteur dernières sessions | Rangée calendrier sur 6 semaines |
|---|---:|---:|---:|---:|
| 1050 × 700 | 718 DIP | 172,75 DIP | 201 DIP | 34 DIP |
| 720 × 520 | 580 DIP | 138,25 DIP | 83 DIP | 13 DIP |
| 819 × 520 | 647 DIP | 155 DIP | 83 DIP | 13 DIP |
| 820 × 520 | 488 DIP | 115,25 DIP | 83 DIP | 13 DIP |

- La sidebar passe de 72 à232 DIP à820 ; le padding change séparément à760. Tester 759/760 et819/820, pas seulement les deux tailles demandées.
- Le minimum annoncé de106 DIP pour les dernières sessions n'est pas garanti à520 de haut : le minimum graphique92 force une carte finale de83.
- Historique à520 : le graphique a116,16 DIP de haut. Six rangées de13 DIP après40 DIP d'en-têtes occupent118 DIP : le bas de la grille dépasse son panneau de1,84 DIP, avant d'évaluer les glyphes. Ce défaut géométrique ne prouve pas à lui seul un chevauchement visible avec le panneau suivant.
- Les textes DrawText sont clippés à leurs propres rectangles ; le clipping évite certains débordements mais peut masquer une valeur. La largeur réelle des glyphes doit être mesurée avec DirectWrite et confirmée en capture.
- Paramètres reste positionné en coordonnées verticales fixes jusqu'à460 DIP. À520, le texte tient géométriquement dans le panneau, mais la lisibilité des retours à la ligne reste NON TESTÉE. Le minimum420 ne garantit pas l'accès à tout le texte ; aucun défilement global n'est implémenté.
- Les frises utilisent les bornes réelles des jours locaux et des repères horaires : conserver cette projection. Une frise civile peut superposer des époques UTC ; ne jamais reconstruire un cumul par sa longueur colorée.

## 5. Architecture visuelle proposée

### Frontière avec le moteur

Conserver TemporalEngine, ActivityTracker, SessionDatabase, calendrier et commandes de préférences existantes. La présentation consomme des snapshots et valeurs déjà calculés. Aucune écriture SQL, migration, lecture applicative de contenu, nouvelle horloge ou règle d'arrondi comptable dans le rendu.

Raccorder au widget les états et agrégats d'activité existants par une projection de présentation. Si une vue réclame des buckets d'activité absents du snapshot, adapter uniquement la projection en lecture via les API existantes ; ne pas synthétiser une série en répartissant le total. Pour une première refonte, conserver des graphiques de session explicitement légendés.

### Couches natives

1. **Thème partagé** : couleurs sémantiques, tailles typographiques, espacements 4/8/12/16/24 DIP, rayons et règles de focus ; caches de formats DWrite et ressources par DPI.
2. **Présentation** : libellés, formatage des durées, disponibilité et incertitude ; source de vérité unique pour le sens session/activité, sans modifier les calculs.
3. **Disposition** : fonctions déterministes produisant rectangles de rendu et cibles identiques ; repli selon largeur utile, hauteur et DPI. Les graphismes ne décident pas des périodes.
4. **Rendu** : coque et portrait statiques WIC + D2D ; repères vectoriels sobres ; texte DWrite indépendant. Pas de texte ni donnée opérationnelle incorporés aux PNG.
5. **Interaction et accessibilité** : commandes Win32 existantes, contrôles natifs de Paramètres, parcours clavier cohérent et fournisseurs d'accessibilité pour zones custom. Les actions explicites de réglage conservent leur commande actuelle.

Fichiers concernés après validation : `chronox_widget.*`, `widget_presentation.*`, `dashboard_window.*`, `dashboard_data.*`, câblage UI de `main.cpp`/`window_coordinator.*`, ressources `assets/ui/`, tests de présentation et documentation. Extraire au besoin de petits modules thème/layout. **Aucune modification prévue de `src/core/` ni `src/storage/`.**

### Composition proposée

- Widget : coque titane conservée ; portrait visible en partie haute/droite ; valeur principale session, activité secondaire clairement nommée ; état d'activité distinct du suivi de session ; navigation basse dégagée. Conserver250 DIP comme point de départ et comparer un agrandi350 DIP sur prototype, sans imposer la taille à l'utilisateur. Aucun changement automatique de préférence.
- Rétracté : languette discrète conservée, accès dashboard et indication accessible ; pas de petite accumulation de chiffres illisibles.
- Dashboard large : navigation latérale, titres sobres, quatre cartes avec deux lignes explicitement nommées ; frise et semaine légendées session ; tableau de détail.
- Dashboard compact : cartes2 ×2, graphiques empilés si nécessaire, défilement vertical du contenu ; éviter la réduction de la police pour tout faire tenir. Navigation conserve des libellés compréhensibles.
- Historique : filtre période/date séparé du résumé ; « session » et « activité » toujours distinguées ; absence de mesure affichée comme telle. Calendrier avec jour lisible, durée dans détail sélectionné si espace insuffisant ; sélection accessible sans souris.
- Paramètres : formulaire natif aligné et scalable, explication du délai et du mode désactivé ; texte de confidentialité lisible ; pas d'introduction de préférences supplémentaires sans validation.
- États : texte + symbole/couleur redondants ; incertitude prioritaire et expliquée. Une fenêtre ne peut pas être observée pendant la veille réelle : tester la représentation Veille sur fixture, puis la reprise réelle séparément, sans prétendre à une capture pendant sommeil.

## 6. Plan progressif proposé — aucune implémentation engagée

Chaque étape donnera lieu à un commit local Conventional Commits après validation ; aucun push ni déploiement implicite.

| Étape | Commit proposé | Périmètre | Validation avant passage suivant |
|---|---|---|---|
| 1 | `refactor(ui): centralize visual tokens and layout` | Thème, typographie, rectangles partagés et référence de captures ; pas de nouveau calcul | Valeurs identiques aux snapshots ; tests26 conservés ; mesures de disposition, contrastes ; captures initiales avant/après. |
| 2 | `fix(ui): improve dashboard readability and keyboard navigation` | Taille des KPI, contraste, focus, sortie de Paramètres, libellés session/activité/couverture | Parcours souris/clavier complet ; états vide/zéro/non mesuré/incertain ; aucune valeur coupée ; captures des3pages aux2tailles. |
| 3 | `feat(ui): refine Chronox widget presentation` | Composition titane, portrait, cibles, états V3, accès dashboard, mode agrandi si approuvé | Captures jour/semaine/mois, rétracté, états ; alpha sur3fonds ; DPI100/150/200 ; glisser/clic/double-clic sans ambiguïté. |
| 4 | `fix(ui): adapt history and settings to compact layouts` | Repli cartes, calendrier, tableaux, défilement et contrôles natifs DPI | 720×520,1050×700 et seuils759/760/819/820 ; mois6semaines, longues durées, correction UTC/DST sur fixtures ; clavier et accessibilité. |
| 5 | `test(ui): qualify visual behavior and resource usage` | Régressions, protocole de captures, ressources et documentation | MSVC Release, toutes les26suites existantes + tests ciblés ; captures réelles approuvées ; métriques comparables ; aucune installation réelle. |

Les étapes2 et4 peuvent corriger des points de la même page, mais chaque commit doit rester utilisable à ses dimensions prises en charge. Ne pas différer une issue clavier bloquante jusqu'à la fin de la refonte.

### Matrice des captures Windows à fournir lors de la refonte

Captures **à produire, NON EXÉCUTÉES ici**. Employer un banc de présentation isolé sans connexion à la vraie base ni aux préférences réelles, puis une validation de l'instance officielle sur autorisation de livraison. Ne pas installer un prototype ni lancer une seconde instance de suivi contre le profil réel.

| Série | Captures / manipulations | Acceptation |
|---|---|---|
| D01–D06 | Vue générale, Historique, Paramètres ; clients1050×700 et720×520 à100% | Fenêtre entière + dimensions/DPI enregistrés ; textes et temps complets, aucun chevauchement, tous contrôles accessibles. |
| D07 | Largeurs759/760 et819/820 ; mêmes données | Pas de régression brutale du contenu ni cible masquée au changement de mode. |
| H01 | Jour23h/25h, jour civil avec époques qui se recouvrent, semaine, mois6rangées, année | Repères de calendrier corrects, mêmes totaux que les projections existantes ; durée pas inférée de l'aire graphique. |
| H02 | Historique V1/V2 sans mesure, zéro mesuré V3, mélange, incertitude | États sémantiques distincts ; pas de faux zéro historique ni prétention de couverture complète. |
| W01 | Widget250 et prototype350 ; jour/semaine/mois ; longues valeurs | Référence reconnaissable, visage visible, chiffres lisibles, unités non coupées ; Année seulement si approuvée. |
| W02 | Actif, Inactif, Verrouillé, Veille, Incertain, détection désactivée sur fixtures | Texte exact et non ambigu ; aucun état déduit de la couleur seule. |
| W03 | Rétracté/déployé ; fonds clair, sombre, texturé ; DPI100/150/200 | Pas de rectangle opaque ni frange gênante ; cibles et positionnement cohérents. |
| K01 | Tab/Maj+Tab, flèches, Entrée/Espace, Échap, sélection de date, défilement tableau ; Narrateur | Aucune impasse clavier ; focus visible ; noms/rôles et valeurs utilisables ; captures de focus + journal des gestes. |
| M01 | Transfert écrans/DPI, barre des tâches auto-masquée, plein écran | Position accessible et comportement non intrusif ; NON TESTÉ si matériel indisponible. |

Nommer les fichiers par vue/taille/DPI/état/version, conserver la capture avant/après avec mêmes données et même fond. Les captures de données réelles restent privées ; privilégier les fixtures pour le dossier de conception.

### Régression et performances

- Conserver les26suites ; compléter les tests de rectangles/hit-tests/focus/formatage extrême et projection UI, sans tests qui copient simplement l'implémentation.
- Vérifier que navigation, rendu et redimensionnement ne déclenchent pas de nouvelles écritures SQLite ; paramètres uniquement via leur commande explicite. Tester avec base temporaire, jamais migration réelle.
- Mesurer Release sur mêmes fixtures, DPI, écran et état chaud : widget visible, rétracté, masqué, dashboard ouvert ; CPU sur un cœur, private bytes/working set, handles, GDI/USER, threads. Fenêtres60s et100cycles navigation/afficher/masquer après échauffement ; longue durée distincte.
- Seuils proposés à approuver : pas de croissance monotone de handles/GDI/USER après100cycles ; aucune nouvelle boucle/tâche permanente ; CPU moyen ≤baseline+0,2point d'un cœur ; mémoire privée ≤baseline+5MiB hors mode agrandi justifié. Mesurer et expliquer tout dépassement, ne pas comparer des états chauds/froids différents.
- Revalider le processus unique et les garanties temporelles par les tests existants. Aucun essai graphique ne doit modifier l'heure Windows ou la vraie base.

## 7. Synthèse de preuves et arrêt

| Contrôle | Résultat de cet audit |
|---|---|
| Référence titane présente, ouverte et comparée aux ressources | PASS |
| Inventaire / analyse de code / contraste des couleurs opaques / géométrie | PASS, avec problèmes identifiés |
| Préparation d'une architecture et d'un plan de validation | PASS, proposition à valider |
| Validation graphique de V3 installée, tailles réelles et gestes clavier/souris | NON TESTÉ |
| Contrastes réels du widget sur bureau / DPI / lecteur d'écran | NON TESTÉ |
| Nouveau build, CTest exécuté et mesures CPU/RAM de cet audit | NON TESTÉ ; seul inventaire CTest effectué |

**Arrêt avant code et refonte.** Seul ce rapport est ajouté. Les modifications locales préexistantes et les archives sont conservées ; le moteur, les calculs, la vraie base et l'instance installée restent intacts.
