# Chronox V0.4 — spécification de l’interface

## Statut et périmètre

Cette spécification formalise la cible visuelle et fonctionnelle proposée pour
l’interface V0.4. La référence artistique ci-dessous a été sélectionnée ; les
choix de rendu et d’intégration restent à prototyper. Aucun élément graphique
n’est encore implémenté.

Les éléments existants sont décrits comme **implémentés**. Les exigences de
présentation de cette spécification sont la **cible validée pour le prototype
V0.4**, sans préjuger de la faisabilité ni de la livraison. L’architecture et
les comportements non confirmés sont **à valider**.

La spécification technique du suivi et des données reste celle de la
[spécification V0](./SPEC.md). Cette interface ne change ni le moteur de suivi,
ni les règles de comptabilisation, ni le schéma SQLite.

## Référence artistique

![Référence visuelle du widget Chronox](../assets/references/chronox-widget-reference.png)

Copie de travail : [`assets/references/chronox-widget-reference.png`](../assets/references/chronox-widget-reference.png).
Le fichier téléchargé original est conservé hors du dépôt. SHA-256 de la copie :
`6435987CE01D83F2F03353C7B2881CA927C5B014747D29EF901E29DC3FA935AF`.

Cette image est une **référence artistique**, pas une ressource de production
définitive. Son filigrane, ses textes, chiffres, proportions et détails ne
doivent pas être incorporés tels quels dans l’application. Les ressources
finales, leur licence, leur transparence et leur optimisation devront être
validées séparément.

## Identité graphique cible

- Chronomètre tridimensionnel premium, en titane argenté brossé.
- Cadran sombre, avec un relief de Chronos en arrière-plan.
- Bouton mécanique compact en haut à gauche et couronne supérieure discrète.
- Éclairage bleu électrique, traits lumineux et accents cyan.
- Contour extérieur transparent autour du boîtier afin de laisser voir le
  bureau ; la gestion de l’alpha et des effets lumineux reste à valider sur
  Windows.

La cible reprend le langage visuel de l’image, sans figer la direction
artistique au-delà de cette référence ni imposer ses détails décoratifs.

## Cadran et informations

### Échelle et horaires

- Cadran circulaire de **12 heures**, avec les douze divisions principales et
  des subdivisions plus fines.
- Les valeurs représentent les heures de la journée au format local sur
  24 heures. Les positions du cadran sont cycliques : par exemple, 17 h se
  place à la position 5 h. Les libellés de début et de fin gardent leur valeur
  non ambiguë, par exemple `08 h — Début` et `17 h — Fin`.
- Plage de suivi configurable ; valeur initiale proposée : **08:00–17:00**,
  soit neuf heures. Le fuseau local et les changements d’heure doivent être
  cohérents avec les limites calendaires du moteur existant.
- Le comportement de plages traversant minuit et celui d’une heure inexistante
  ou répétée lors d’un changement d’heure restent à spécifier avant
  implémentation.

### Anneaux et repères

- Le temps écoulé dans la plage configurée est représenté par un arc bleu
  continu.
- Le temps restant dans cette plage est représenté par des pointillés cyan.
- Les heures hors de la plage sont grisées.
- Le début et la fin sont repérés et libellés explicitement.
- Ces arcs expriment la progression dans la plage horaire configurée ; ils ne
  constituent pas une mesure de travail effectif ni une preuve d’activité.

### Contenu central et navigation

- Afficher au centre le temps du jour, avec un libellé précisant qu’il s’agit
  du temps mesuré par Chronox.
- Afficher distinctement l’état de session (active ou arrêtée) et la durée de
  la période courante lorsque le suivi est actif.
- Permettre de naviguer entre plusieurs vues au moyen de flèches. Le contenu
  précis et l’ordre des vues restent à confirmer par prototype.
- La distinction entre la progression horaire des anneaux et le cumul réel
  mesuré par Chronox doit rester compréhensible dans chaque vue.

Le moteur actuel mesure seulement le temps pendant lequel la session Windows
est déverrouillée et le PC éveillé simultanément, pendant l’exécution de
Chronox. Ce temps ne correspond pas nécessairement à du temps de travail
effectif. Le widget ne doit pas présenter une durée mesurée comme preuve de
présence humaine continue ou de productivité.

## États et fenêtres

Les tailles ci-dessous sont des dimensions indicatives à l’échelle Windows
100 % ; elles devront être vérifiées sur plusieurs résolutions et facteurs
DPI.

| État | Présentation cible |
|---|---|
| Rétracté | Languette discrète ancrée au bord droit de l’écran ; elle donne accès au widget sans masquer inutilement le bureau. |
| Compact | Cadran d’environ 220 px, affichage essentiel du temps du jour et de l’état de session. |
| Agrandi | Cadran d’environ 350 px, davantage d’informations et navigation entre vues. |
| Dashboard | Fenêtre indépendante avec statistiques détaillées et historique disponible. |

Le dashboard V0.4 peut présenter les cumuls et périodes déjà fournis par le
moteur. Le suivi des applications au premier plan n’est pas implémenté et ne
fait pas partie de cette interface V0.4 ; aucune répartition applicative ne
doit être simulée comme une donnée réelle.

Les gestes exacts de déploiement, repli, redimensionnement, fermeture et retour
du dashboard au widget restent à confirmer durant le prototypage.

## Intégration Windows

- Position initiale en bas à droite, au-dessus de la barre des tâches.
- Animation de déploiement depuis le bord droit ; elle doit rester courte,
  discrète et pouvoir être désactivée si nécessaire.
- Icône et commandes d’accès dans la zone de notification Windows.
- Option utilisateur pour afficher le widget au premier plan ; ce comportement
  ne doit pas être imposé par défaut sans validation.
- Gérer les déplacements entre écrans, les écrans aux résolutions ou facteurs
  DPI différents, les changements de configuration et la restauration d’une
  position devenue invalide.
- Définir un comportement non intrusif en plein écran, avec barre des tâches
  masquée ou affichée automatiquement.
- Mémoriser les états et préférences pertinents. Le format, l’emplacement, les
  règles de migration et le périmètre exact des préférences restent à décider.

## Architecture envisagée — à valider

Une approche native hybride est à prototyper :

- PNG ou WebP avec alpha pour les éléments 3D statiques ;
- Direct2D pour les anneaux, graduations, arcs et indicateurs ;
- DirectWrite pour les textes et compteurs ;
- Win32 pour les fenêtres, interactions, événements et zone de notification ;
- conservation du moteur C++20, des calculs calendaires et de la base SQLite
  existants.

Ce choix n’est pas une décision technique définitive. Un prototype devra
valider la composition des éléments, le rendu transparent, l’accessibilité, le
comportement DPI, la maintenance et les dépendances nécessaires sur une
installation Windows standard. Les vues devront consommer les données du
moteur sans déplacer la logique métier dans le code de rendu. Une actualisation
visuelle ne devra pas déclencher une écriture SQLite.

## Performance et stabilité

Objectifs à mesurer, sans résultat présumé :

- ne redessiner que lors d’un événement ou d’un changement de valeur affichée ;
- limiter les mises à jour périodiques au strict nécessaire lorsque le widget
  est visible ; suspendre les animations et le rendu inutile lorsqu’il est
  masqué ;
- ne réaliser aucun rendu 3D permanent ; privilégier des ressources statiques
  et un rendu 2D événementiel ;
- maintenir une faible consommation CPU au repos et éviter les écritures disque
  liées aux seules actualisations graphiques.

Le protocole de validation devra mesurer séparément, sur une version Release :

- CPU au repos, widget compact visible, widget masqué et dashboard ouvert ;
- mémoire privée/RAM et son évolution ;
- nombre de handles Win32 et de threads ;
- fréquence et volume des accès disque, ainsi que l’évolution de la base ;
- stabilité pendant plusieurs heures, avec navigation, verrouillage/réveil et
  changements d’écran/DPI lorsque le matériel le permet.

Aucun de ces résultats n’est revendiqué par cette spécification. Les essais
nécessitant une session interactive devront être exécutés sur un vrai PC
Windows et consignés séparément des tests automatisés.

## Points restant à valider

- Lisibilité et usage du cadran 12 h lorsque les valeurs sont des heures locales
  24 h, en particulier autour de minuit et lors des changements d’heure.
- Sens exact du temps central et des arcs, et compréhension de leur différence
  avec le temps de session réellement mesuré.
- Plages de suivi traversant minuit et traitement des heures DST ambiguës ou
  inexistantes.
- Contenu, ordre et interactions des vues accessibles par les flèches.
- Dimensions finales, accessibilité clavier, contraste, réduction des
  animations et comportement au premier plan.
- Persistance des préférences et stratégie pour plusieurs écrans.
- Faisabilité et coût de l’approche hybride, dépendances et mesures de
  performance sur une version Release.
