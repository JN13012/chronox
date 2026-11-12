# Sélection de page, état et lisibilité — 12 novembre 2026

## Défauts vérifiés et corrections

La sélection précédente était limitée à un bloc et Ctrl+A ne sélectionnait que celui-ci. Le modèle possède maintenant deux extrémités dans le document de blocs DirectWrite : glissement entre textes, copie dans l'ordre de lecture avec retours de ligne, Ctrl+A sur le document complet et maintien de la sélection pendant le défilement. Les actions des boutons restent prioritaires au clic simple. Les textes et valeurs des contrôles Win32 natifs de Paramètres conservent le comportement de ces contrôles : ils ne font pas partie du document DirectWrite. Une couverture absolue de tous les contrôles natifs n'est donc pas revendiquée.

L'état session/activité est déplacé dans l'en-tête de Vue générale, aligné à droite, hors du contenu défilant. Son rectangle possède34 DIP de haut et une colonne dédiée à droite du fil d'Ariane. Le titre est placé dessous sans chevauchement aux petites tailles. La zone d'état auparavant réservée au-dessus des cartes est supprimée, ce qui libère40 DIP supplémentaires.

Les lignes de durée d'activité sont renommées « Clavier/souris » pour distinguer la mesure de l'état « Actif ». Les calculs, périodes et données ne changent pas : `dashboard_data.cpp` remplit chaque `activityDurations[i]` via `queryActivity(periods[i],now,...)`. Les quatre valeurs concernent aujourd'hui, la semaine, le mois et l'année. Elles peuvent coïncider si l'activité mesurée se trouve entièrement dans leurs périodes communes. Le suivi V3 ne reconstitue pas l'activité antérieure à son activation.

Le compteur clavier/souris inclut le délai précédant le seuil d'inactivité (5 minutes par défaut). Il se suspend après expiration de celui-ci, au verrouillage et en veille. Lorsque la détection est désactivée, il suit prospectivement le temps de session. Le temps de session reste le temps éveillé et déverrouillé comptabilisé par le moteur V3.

## Critiques et améliorations proposées

| Priorité | Observation | Proposition non implémentée dans cette correction |
|---|---|---|
| Haute | Deux mesures répétées dans chacune des quatre cartes rendent la lecture dense et « activité » reste un terme ambigu | Un sélecteur global Session / Clavier-souris pour les quatre périodes, avec un seul grand compteur par carte ; conserver les deux mesures dans Historique |
| Haute | Les libellés de périodes mélangent Aujourd'hui/Cette semaine et mois/année explicites | Employer Aujourd'hui / Cette semaine / Ce mois / Cette année, puis afficher la plage de dates en information secondaire |
| Moyenne | Les légendes des graphiques utilisent encore10 DIP | Passer les légendes à12–14 DIP, vérifier les sept valeurs à720 DIP et ajouter une infobulle de détail si nécessaire |
| Moyenne | La frise réserve180 DIP pour une piste de18 DIP et laisse beaucoup de vide | Une frise plus mince dans une rangée dédiée ; garder une hauteur utile pour l'histogramme |
| Moyenne | Beaucoup de surfaces encadrées et plusieurs accents concurrencent les valeurs | Réserver le cyan saturé à la sélection et aux données importantes, atténuer les bordures secondaires |
| À valider | La sélection est faite dans un rendu DirectWrite personnalisé | Revue Windows du clic/glissement/copie, puis exposition UI Automation et vérification Narrateur ; ne pas confondre sélection visuelle et accessibilité complète |

Ces propositions modifient seulement la présentation ; aucune modification de SQLite, des durées ou du moteur n'est nécessaire. Le sélecteur de mesure constitue une prochaine décision de présentation et n'est pas ajouté implicitement.

## Validation

MSVC Release PASS, CTest27/27 PASS. Les régressions couvrent les sélections entre blocs et leur copie exacte, Ctrl+A incluant un bloc sous le viewport, maintien au défilement, invalidation après mise à jour, état long dans la colonne droite, valeurs extrêmes et navigation sur les trois pages. Les tests natifs envoient des messages uniquement à leurs propres fenêtres et ne modifient pas le presse-papiers utilisateur. Preuve : `build/ui-page-selection/ctest.xml`.

Capture automatique et validation esthétique réelle : NON TESTÉES. Mesure comparative CPU/RAM et endurance : NON TESTÉES pour cette itération. Cache limité à1024 blocs, aucun nouveau polling, thread ou framework. Moteur, widget et SQLite inchangés ; modifications locales préexistantes conservées et aucun push.
