# Dashboard — légendes et sélection du texte

12 novembre 2026.

- Les sept durées de la semaine partagent une même ordonnée en haut du graphique, au-dessus des pistes sombres, indépendamment de la hauteur des barres bleues et y compris pour les valeurs nulles. Chaque durée est centrée horizontalement sur sa barre. Les jours sont centrés sur le même axe. Les calculs et formats de durée restent identiques.
- Sur un texte de lecture du dashboard, un clic sélectionne le bloc complet. Un glissement peut traverser plusieurs blocs, avec surbrillance suivant les positions DirectWrite. Ctrl+C ou Ctrl+Inser copie la sélection en Unicode avec séparateurs de lignes ; Ctrl+A sélectionne les textes dessinés de toute la page, y compris les sections sous le viewport ; Échap efface d'abord la sélection.
- Les boutons, onglets, cellules de calendrier navigables et contrôles natifs conservent leur action au clic. La sélection de lecture n'est pas un remplacement des interactions de ces commandes. Les contrôles natifs Paramètres gardent leur propre comportement ; leurs valeurs ne sont pas intégrées au texte DirectWrite. Le widget n'est pas modifié.
- La sélection reste active pendant le défilement. Elle est effacée lors d'un changement de page, d'un redimensionnement ou si le texte sélectionné change au rafraîchissement. Les glyphes masqués ne réagissent pas au clic, mais Ctrl+A inclut le document dessiné complet. Une invalidation en cours de rendu déclenche une peinture supplémentaire pour retirer les anciennes surbrillances. Le presse-papiers n'est modifié qu'à la demande de copie de l'utilisateur.
- Cache de mises en page DirectWrite limité à 1024 blocs de la page courante, réutilisé pendant les déplacements de souris. Aucun contrôle EDIT ajouté, aucun timer, hook, thread ou collecte d'entrée supplémentaire.

## Vérifications

Release MSVC : PASS. CTest : **27/27 PASS**, avec conservation des suites existantes. Les tests ajoutés couvrent le centrage des glyphes au-dessus des barres, clic et glissement, texte copié exact, limites du viewport, texte multiligne, réutilisation des layouts et invalidation lors d'une mise à jour.

Les tests Win32 envoient des messages uniquement aux fenêtres du test : sélection et capture/libération souris, Échap sans masquer le dashboard, puis navigation/défilement des trois pages à 720×520, 819×520, 820×520, 1050×700 et 1440×1000. Les 100 cycles afficher/masquer conservent des handles/GDI/USER stables. Le test de copie vérifie la chaîne produite sans écraser le presse-papiers utilisateur. L'écriture réelle dans le presse-papiers et la revue visuelle restent à vérifier manuellement.

Preuves : `build/ui-text-selection/ctest.xml`, mesures `visible-before-*` / `visible-after-*` et binaire de référence `before-review.exe` conservés dans ce même répertoire. Les mesures courtes montrent des variations opposées de CPU selon le format, avec initialisations graphiques différentes ; **comparaison de performances NON CONCLUANTE**, pas de gain revendiqué. Aucun test d'endurance exécuté.

## Revue manuelle

```powershell
.\scripts\review-dashboard.ps1 -Page Overview -Width 1050 -Height 700 -State reference
```

Le banc affiche des données fictives sans SQLite ni préférences utilisateur. Faire défiler jusqu'au graphique : les sept durées doivent être centrées sur les sept barres et alignées sur une même ligne en haut. Cliquer un compteur ou une légende, vérifier sa surbrillance, puis Ctrl+C et coller manuellement dans un éditeur. Glisser entre plusieurs textes, puis vérifier que Ctrl+A inclut aussi les sections sous le viewport. Faire défiler avec une sélection active : elle doit rester présente. Vérifier Échap et la navigation des trois pages. Répéter à 720×520. Un clic simple sur un bouton doit conserver son action de navigation.

Le déploiement réel, ses sauvegardes et ses vérifications sont consignés séparément sous `deployment-archives/ui-selection-20261112/`.
