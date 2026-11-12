# Dashboard compact — 12 novembre 2026

- Cartes de cumul : hauteur usuelle de 114 DIP au lieu de154, soit environ26 % de moins. L'espace des valeurs principales passe de64 à40 DIP lorsqu'elles tiennent sur une ligne ; les chiffres restent entre24 et28 DIP. DirectWrite conserve64 DIP pour les très grands cumuls et jusqu'à38 DIP pour une activité sur deux lignes : aucune troncature pour réduire artificiellement la carte.
- État « Session en cours / Activité : Actif » flottant, sans fond ni bordure ; seul le point d'état est conservé.
- Les paragraphes de couverture et d'incertitude demandés sont supprimés de Vue générale et Historique, sans laisser leur espace vide. « Non mesurée » reste distinct de zéro dans les compteurs ; l'état courant « Mesure incertaine » reste possible.
- Zone avant les cartes :40 DIP au lieu de112. En disposition deux colonnes et avec des valeurs courantes, les graphiques remontent de152 DIP. Historique :84 DIP récupérés au-dessus de la navigation.
- Disposition deux/quatre colonnes, couleurs, sélection/copie du texte, légendes des graphiques et alignement supérieur commun des durées conservés. Widget, moteur temporel, calculs et SQLite inchangés.

MSVC Release : PASS. CTest :27/27 PASS, avec mesures DirectWrite des durées courantes et extrêmes, absence de chevauchement valeur/activité, navigation, défilement et100 cycles afficher/masquer. Preuve : `build/ui-text-selection/ctest-compact.xml`.

La mesure des cartes est réutilisée tant que la largeur et les valeurs ne changent pas ; aucun timer, thread ou contrôle natif supplémentaire. Aucune nouvelle mesure d'endurance ni comparaison CPU/RAM effectuée pour cette itération. Captures et validation esthétique réelle restent NON TESTÉES après compilation.

Déploiement et sauvegardes : `deployment-archives/ui-compact-20261112/`. Aucun push et aucune migration.
