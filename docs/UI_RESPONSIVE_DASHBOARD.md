# Dashboard adaptatif — 12 novembre 2026

## Différence entre les compteurs

**Session** : temps comptabilisé pendant que Windows est éveillé et déverrouillé. **Clavier/souris** : temps classé actif suivant les interactions et le seuil d'inactivité ; avec le réglage par défaut, les cinq minutes suivant la dernière interaction sont incluses, puis ce compteur s'arrête jusqu'à une nouvelle interaction validée. Verrouillage et veille le suspendent immédiatement. Si la détection est désactivée, il suit la session prospectivement.

Les quatre cartes ont des périodes distinctes : aujourd'hui, semaine, mois, année. La mesure d'activité n'est pas reconstruite avant V3. Des cumuls identiques sur plusieurs périodes ne constituent donc pas à eux seuls un double comptage.

## Disposition livrée

- Barre latérale plafonnée à196 DIP au lieu de232, marges externes16 au lieu de24, gouttières10 DIP.
- Quatre cartes sur une rangée à1050×700 ; deux colonnes à720×520. La décision utilise la largeur disponible ; des cumuls extrêmes peuvent déclencher un retour à deux colonnes pour conserver les caractères.
- Cartes courantes100 DIP de haut. Valeurs principales24–28 DIP conservées. Format `h:mm:ss` à heures cumulatives, sans retour à zéro à24h ni suppression des secondes. Exemple : `85 h34 min34 s` devient `85:34:34`. Historique garde ses formats détaillés.
- Une légende globale « TEMPS DE SESSION · h:mm:ss » remplace les quatre répétitions ; chaque seconde ligne reste identifiée Clavier/souris. Périodes uniformisées : Aujourd'hui, Cette semaine, Ce mois, Cette année.
- Graphiques à hauteur dépendant de la fenêtre (120..160 DIP), disposés côte à côte ou empilés sous600 DIP de largeur utile. La frise empilée utilise84 DIP. Les légendes courantes de la semaine passent à12 DIP ; les durées restent sur une ligne supérieure commune.
- Zone Dernières sessions proportionnée à la hauteur (160..300 DIP) ; le résumé peut présenter moins de lignes dans une petite fenêtre, l'Historique conserve les sessions complètes.
- Pour les valeurs usuelles testées, Vue générale tient dans1050×700 sans débordement vertical. En720×520, le défilement reste nécessaire pour atteindre toutes les sections : l'adaptation ne réduit pas artificiellement les chiffres ni ne masque les secondes.

## Validation

MSVC Release PASS et CTest27/27 PASS. Tests DirectWrite sur valeurs usuelles et maximales, format cumulatif, quatre/deux colonnes, absence de débordement du cas usuel1050×700. Fenêtres natives aux tailles1050×700,720×520,819/820×520,867/868×520,900×600 et1440×1000 ; navigation, sélection, défilement et100 cycles afficher/masquer PASS. Preuve : `build/ui-responsive/ctest.xml`.

Capture Windows automatique tentée : canal natif Computer Use indisponible (os error2). La revue esthétique réelle et les captures restent NON TESTÉES. Pour les captures manuelles, utiliser `scripts/review-dashboard.ps1` avec les tailles ci-dessus et `-State reference`, puis comparer vue complète et après défilement. Le banc ne lit ni SQLite ni les préférences.

Mesures avant/après sur le même banc isolé, mêmes données fictives, processus distincts, pages préchauffées puis3s d'attente et20s de mesure. Valeurs brutes conservées dans `build/ui-responsive/visible-before-*` et `visible-after-*`, puis archivées avec le déploiement. Aucun nouveau timer, hook ou thread. Moteur, ActivityTracker, SQLite, données et widget inchangés. Aucun push.
