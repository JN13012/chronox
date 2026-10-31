# Vision produit Chronox

## Statut de ce document

Ce document distingue les capacités **implémentées**, les choix déjà
**décidés**, les évolutions **proposées** et les sujets **à étudier**. Les
éléments de roadmap ne constituent pas un engagement de livraison.

## Vision

Chronox est une application native Windows, développée en C++20, destinée au
suivi du temps et de l’activité sur un ordinateur. Sa priorité initiale est
d’être un outil personnel léger, local et respectueux de la vie privée.

À plus long terme, deux orientations pourraient être explorées :

- **Chronox Personal** : suivi individuel, historique privé et outils de
  compréhension de son temps ;
- **Chronox Business** : éventuelle offre professionnelle pour des équipes,
  sous réserve d’une étude approfondie des usages, des garanties et du cadre
  légal.

Ces noms et orientations sont des propositions de travail, pas des produits ni
des engagements commerciaux décidés.

## Ce que les durées signifient

Chronox doit distinguer explicitement des mesures de nature différente :

1. **Temps de session** — durée pendant laquelle une session Windows est
   déverrouillée et l’ordinateur éveillé simultanément. C’est la mesure
   actuellement implémentée. Elle ne prouve ni présence humaine continue, ni
   activité professionnelle.
2. **Activité applicative** — informations temporelles sur les applications
   au premier plan. Cette capacité est proposée pour une version future ; elle
   n’est pas encore implémentée et pourrait révéler des informations
   sensibles.
3. **Temps de travail déclaré** — durée qu’une personne choisit de déclarer
   comme travail, par exemple pour un projet ou une tâche. Cette déclaration
   aurait un sens distinct des mesures automatiques. Elle n’est pas
   implémentée et son modèle fonctionnel reste à étudier.

Ces données ne doivent pas être fusionnées ni présentées comme des preuves
équivalentes. Une durée de session ou d’application ne doit jamais être
qualifiée automatiquement de temps de travail réellement effectué.

## Principes de conception

- **Performance** : faible consommation CPU, mémoire et disque au repos ;
  traitements et actualisations limités au nécessaire.
- **Modularité** : séparer les règles métier, la persistance et les intégrations
  propres à Windows.
- **Fiabilité** : rendre les transitions et les limites de mesure explicites,
  préserver les données et signaler les erreurs sans les masquer.
- **Transparence** : expliquer ce qui est mesuré, à quel moment, et les limites
  d’interprétation des résultats.
- **Contrôle des données** : privilégier le stockage local, documenter toute
  collecte ou synchronisation éventuelle et laisser à la personne le contrôle
  des données qui la concernent.

## Interface proposée

La version actuelle est une application console ; aucune interface graphique
n’est implémentée. Une référence artistique a été sélectionnée pour le
prototype V0.4 : chronomètre en titane sombre, accents bleus et cadran
circulaire. Elle ne constitue pas un choix de ressources de production ni une
validation de l’architecture graphique. Les exigences et questions ouvertes
sont détaillées dans la [spécification UI V0.4](./UI_SPEC.md).

### Widget flottant compact

La cible V0.4 décrit un widget facultatif, compact et déplaçable, avec des
états rétracté, compact et agrandi. Son intégration et ses interactions restent
à prototyper ; les tailles indiquées par la spécification UI sont indicatives.

### Widget étendu et application principale

Un mode plus développé pourrait combiner des statistiques rapides, l’état de
session et l’accès à l’application principale. La relation exacte entre widget
et fenêtre principale reste à définir.

### Dashboard complet

Une vue approfondie pourrait présenter l’historique, des graphiques, un
calendrier ainsi que des répartitions par applications et par périodes. La
répartition par application dépendrait de la mise en œuvre préalable du suivi
applicatif et de choix explicites sur les données conservées.

### Piste technique

Une interface native Win32 utilisant Direct2D et DirectWrite est une piste
technique à valider par un prototype, notamment pour l’accessibilité, la
maintenance, les performances et les besoins réels de rendu. Cette piste
n’est pas une décision d’architecture définitive. La référence artistique
sert de cible de conception, mais les ressources finales, les dimensions,
l’apparence détaillée et les interactions restent à valider.

## Données et confiance

### Implémenté

- Mesure locale du temps pendant lequel la session courante est déverrouillée
  et le PC éveillé, pendant l’exécution de Chronox.
- Persistance dans SQLite sous `%LOCALAPPDATA%\Chronox\chronox.db`.
- Rapports calendaires locaux par jour, semaine, mois et année.
- Aucun serveur, service cloud ou suivi des applications au premier plan dans
  le périmètre actuel.

### Décidé

- Conserver pour le périmètre initial une application Windows native, sans
  serveur ni service cloud requis.
- Ne pas compter les périodes pendant lesquelles Chronox ne fonctionne pas ;
  après un arrêt inattendu, récupérer au plus le dernier point de sauvegarde
  durable.
- Garder séparés le temps de session, une éventuelle mesure d’activité
  applicative et tout temps de travail déclaré.

Pour le comportement détaillé déjà validé, voir la
[spécification V0](./SPEC.md).

## Perspective professionnelle — sujets à étudier

Une éventuelle offre Business nécessiterait une étude dédiée avant tout
développement ou déploiement. Elle devrait notamment examiner :

- les finalités et la base légale applicables au traitement, ainsi que les
  obligations du RGPD ;
- les recommandations et les formalités ou ressources applicables de la CNIL ;
- la nécessité et la proportionnalité de chaque donnée suivie au regard de la
  finalité annoncée ;
- l’information préalable, compréhensible et effective des salariés ;
- les rôles et les modalités d’accès aux données par la personne concernée,
  l’employeur, les responsables et les administrateurs ;
- les durées de conservation, la suppression et l’export des données ;
- les mesures de sécurité, le contrôle des accès, le chiffrement éventuel, la
  traçabilité et la gestion des incidents ;
- la distinction entre suivi du temps, activité d’un poste et évaluation du
  travail, ainsi que les risques de réutilisation des données.

Cette liste est une première grille de questions, pas un avis juridique ni une
garantie de conformité. Les exigences devraient être précisées avec des
spécialistes compétents et les parties concernées avant de définir une offre.

**Limite de conception :** Chronox ne prévoit pas de surveillance cachée, de
capture d’écran permanente ni de keylogging. Toute évolution du périmètre de
données devrait être explicite, justifiée, proportionnée et soumise à une
révision de ses garanties de confidentialité.

## Questions ouvertes

- Quels besoins personnels justifient une interface graphique et quel est le
  meilleur parcours entre widget compact, mode étendu et dashboard ?
- Quelle technologie d’interface répond aux besoins sans alourdir l’application
  ni sa maintenance ?
- Quelles données d’activité applicative seraient réellement utiles, visibles
  par la personne et conservées localement ?
- Faut-il permettre une déclaration manuelle de travail, et comment l’afficher
  sans la confondre avec le temps de session ou l’activité mesurée ?
- Existe-t-il un besoin réel pour une offre Business, et quelles garanties
  seraient nécessaires avant de l’envisager ?

Ces questions restent ouvertes jusqu’à validation par des besoins utilisateurs,
des prototypes et, pour l’usage professionnel, une étude spécifique.
