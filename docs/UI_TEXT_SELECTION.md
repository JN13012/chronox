# Dashboard — légendes et sélection du texte

12 novembre 2026.

- Durées de la semaine centrées horizontalement au-dessus du sommet de chaque barre bleue, y compris les valeurs nulles. Les jours sont centrés sur le même axe. Les calculs et formats de durée restent identiques.
- Sur un texte de lecture du dashboard, un clic sélectionne le bloc complet. Un glissement sélectionne des caractères dans ce bloc, avec surbrillance suivant les positions DirectWrite. Ctrl+C ou Ctrl+Inser copie la sélection en Unicode ; Ctrl+A sélectionne le bloc entier ; Échap efface d'abord la sélection.
- Les boutons, onglets, cellules de calendrier navigables et contrôles natifs conservent leur comportement. La sélection ne traverse pas plusieurs blocs. Le widget n'est pas modifié.
- La sélection est effacée lors d'un changement de page, d'un défilement, d'un redimensionnement ou si le texte sélectionné change au rafraîchissement. Un texte masqué par le viewport n'est pas sélectionnable. Le presse-papiers n'est modifié qu'à la demande de copie de l'utilisateur.
- Cache de mises en page DirectWrite limité à 1024 blocs de la page courante, réutilisé pendant les déplacements de souris. Aucun contrôle EDIT ajouté, aucun timer, hook, thread ou collecte d'entrée supplémentaire.

## Vérifications

Release MSVC : PASS. CTest : **27/27 PASS**, avec conservation des suites existantes. Les tests ajoutés couvrent le centrage des glyphes au-dessus des barres, clic et glissement, texte copié exact, limites du viewport, texte multiligne, réutilisation des layouts et invalidation lors d'une mise à jour.

Les tests Win32 envoient des messages uniquement aux fenêtres du test : sélection et capture/libération souris, Échap sans masquer le dashboard, puis navigation/défilement des trois pages à 720×520, 819×520, 820×520, 1050×700 et 1440×1000. Les 100 cycles afficher/masquer conservent des handles/GDI/USER stables. Le test de copie vérifie la chaîne produite sans écraser le presse-papiers utilisateur. L'écriture réelle dans le presse-papiers et la revue visuelle restent à vérifier manuellement.

Preuves : `build/ui-text-selection/ctest.xml`, mesures `visible-before-*` / `visible-after-*` et binaire de référence `before-review.exe` conservés dans ce même répertoire. Les mesures courtes montrent des variations opposées de CPU selon le format, avec initialisations graphiques différentes ; **comparaison de performances NON CONCLUANTE**, pas de gain revendiqué. Aucun test d'endurance exécuté.

## Revue manuelle

```powershell
.\scripts\review-dashboard.ps1 -Page Overview -Width 1050 -Height 700 -State reference
```

Le banc affiche des données fictives sans SQLite ni préférences utilisateur. Faire défiler jusqu'au graphique : les sept durées doivent être centrées sur les sept barres. Cliquer un compteur ou une légende, vérifier sa surbrillance, puis Ctrl+C et coller manuellement dans un éditeur. Vérifier aussi un glissement partiel, Ctrl+A, Échap et la navigation des trois pages. Répéter à 720×520. Les textes des boutons restent des actions ; ils ne doivent pas être capturés par la sélection.

Le déploiement réel, ses sauvegardes et ses vérifications sont consignés séparément sous `deployment-archives/ui-selection-20261112/`.
