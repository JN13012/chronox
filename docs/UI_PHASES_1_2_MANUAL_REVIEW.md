# Chronox — Revue visuelle manuelle des phases UI 1 et 2

Le banc ci-dessous affiche le véritable rendu Win32/Direct2D/DirectWrite du dashboard candidat, avec des données fictives. Il ne crée aucun moteur de suivi, n'ouvre aucune base et ne lit ni n'écrit les préférences utilisateur. Il ne lance pas `chronox.exe` et ne remplace pas l'installation active.

La capture automatique a été tentée avec la compétence Computer Use : canal natif indisponible (`os error 2`). **Les captures réelles et la validation esthétique restent NON TESTÉES.** Ne pas prendre les tests de géométrie pour une approbation graphique.

## Ouvrir une page isolée

Depuis la racine du dépôt, dans PowerShell :

```powershell
cmake --build build --config Release --target chronox_dashboard_review
.\scripts\review-dashboard.ps1 -Page Overview -Width 1050 -Height 700
```

La fenêtre porte le titre « Chronox - revue isolee », suivi de la taille client physique et du DPI. Fermer cette fenêtre de revue avec sa croix avant la capture suivante. Ne pas quitter Chronox depuis sa zone de notification.

Les dimensions demandées sont les dimensions **client en DIP**, hors barre de titre, bordures et barre de défilement. À 100 %, le titre doit indiquer 1050 × 700 px ou 720 × 520 px. À 150 %, 720 × 520 DIP correspond à1080 ×780 px. Le banc vérifie la taille obtenue et échoue si elle ne correspond pas.

## Six captures principales

Ouvrir successivement chaque combinaison, puis utiliser manuellement l'Outil Capture d'écran de Windows pour capturer la fenêtre entière. Aucun raccourci ni entrée ne sont injectés par les scripts.

| Fichier à fournir | Arguments de review-dashboard.ps1 |
|---|---|
| overview-1050x700.png | `-Page Overview -Width 1050 -Height 700` |
| history-1050x700.png | `-Page History -Width 1050 -Height 700` |
| settings-1050x700.png | `-Page Settings -Width 1050 -Height 700` |
| overview-720x520.png | `-Page Overview -Width 720 -Height 520` |
| history-720x520.png | `-Page History -Width 720 -Height 520` |
| settings-720x520.png | `-Page Settings -Width 720 -Height 520` |

Conserver aussi une capture complémentaire après défilement de Vue générale et Historique : les grandes cartes et leurs valeurs restent lisibles grâce au défilement, elles ne sont pas toutes forcées à tenir simultanément dans720 ×520. Capturer la frise, la semaine et le tableau des sessions. Utiliser la barre verticale ou Page suivante/précédente ; sur les lignes de sessions de l'Historique, la molette conserve le défilement du tableau.

Le répertoire recommandé est `build/ui-phases12/manual-captures/`. Ne pas y mélanger des captures de données réelles.

## Vérifications ciblées

- Valeurs principales : chiffres24–28 DIP, heures/minutes/secondes complètes, aucun « ... » ni chiffre masqué ; session principale et activité secondaire identifiables.
- Onglets Jour/Semaine/Mois/Année : sélection lisible ; contraste des couleurs opaques testé à7,64:1 après l'itération de la référence française. Cliquer les onglets puis les flèches précédent/suivant, DATE et Aujourd'hui.
- Chaque graphique porte explicitement la mention SESSION. Ne pas interpréter sa couleur comme une mesure clavier/souris.
- Barre latérale : comparer `-Width 819 -Height 520` et `-Width 820 -Height 520`, puis759/760. Le contenu doit rester stable sans compression brutale.
- Paramètres : depuis le focus de navigation, Tab entre dans la checkbox, puis le délai ; le Tab suivant rejoint la navigation, puis permet d'accéder à une autre page. Maj+Tab parcourt les contrôles dans l'autre sens puis sort également. Désactiver la détection dans **le banc uniquement** : le délai est ignoré par le parcours et le clavier peut toujours sortir. Les paramètres de Chronox installé restent inchangés.
- Vérifier les valeurs1/5/10/15/30, la sélection souris, le focus et Échap ; absence de changement de compteur ou d'écriture réelle.
- Refaire les contrôles à150 %/200 % si ces DPI sont disponibles, sans changer la configuration d'écran uniquement pour ce test si cela perturbe votre travail.

## États sémantiques supplémentaires

```powershell
.\scripts\review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State missing
.\scripts\review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State zero
.\scripts\review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State uncertain
.\scripts\review-dashboard.ps1 -Page Overview -Width 720 -Height 520 -State disabled
.\scripts\review-dashboard.ps1 -Page History -Width 720 -Height 520 -State uncertain
```

Attendu : « non mesurée » ne devient pas0 ; un zéro mesuré reste0 ; couverture complète jamais présumée ; l'incertitude est expliquée sur la page, avec sa portée éventuellement antérieure, sans lien vers un détail inexistant. Les contrôles de Paramètres sont des commandes locales au banc.

Noter pour chaque capture : page, état, dimensions/DPI, version candidate, anomalie éventuelle et verdict PASS/FAIL. Garder les captures non réalisées au statut NON TESTÉ. Le Narrateur, les autres écrans et la longue durée ne sont pas certifiés par ces six captures.

**Point d'arrêt : revue visuelle des phases1–2. Aucun déploiement ni modification du widget n'est autorisé par ce protocole.**
