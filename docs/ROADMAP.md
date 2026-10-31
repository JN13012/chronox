# Roadmap Chronox

## Statut et lecture

Les versions et fonctionnalités ci-dessous sont des **propositions de
planification**, pas des décisions de livraison, des dates promises ni un
engagement sur le contenu final d’une version. Le périmètre pourra évoluer
après validation des besoins, des prototypes et des contraintes de qualité.

État documentaire :

- **Implémenté** : application console Windows native, mesure du temps de
  session déverrouillée et PC éveillé, stockage SQLite local et rapports par
  périodes calendaires.
- **Décidé** : préserver ce périmètre fiable, local et léger avant d’ajouter
  une interface ou des mesures de nature différente. Les règles actuelles sont
  décrites dans la [spécification V0](./SPEC.md).
- **Proposé** : les étapes ci-dessous.
- **À étudier** : faisabilité, expérience utilisateur, sécurité, confidentialité
  et critères de sortie de chaque étape avant de la confirmer.

## Étapes proposées

### V0.4 — Interface personnelle

Proposition de périmètre :

- widget compact affichant le compteur et pouvant être déplacé ou masqué ;
- zone de notification Windows ;
- widget étendu et dashboard initial ;
- démarrage automatique configurable.

À valider : interactions, accessibilité, comportement multi-écran, contrôles de
visibilité, expérience au démarrage et technologie d’interface. Win32 avec
Direct2D/DirectWrite est une piste à prototyper, pas un choix définitif. Voir
la [spécification UI V0.4](./UI_SPEC.md) et la
[vision produit](./PRODUCT_VISION.md). La référence artistique y est
documentée ; l’interface et ses comportements ne sont pas encore implémentés.

### V0.5 — Chronologie applicative

Proposition de périmètre :

- suivi facultatif des applications au premier plan ;
- chronologie des activités, avec des règles de collecte, de visibilité, de
  conservation et de suppression à définir.

Cette étape nécessiterait une validation spécifique de l’utilité et de la
proportionnalité des informations enregistrées. Une application observée ne
permet pas, à elle seule, de conclure à du travail effectué.

### V0.6 — Organisation et statistiques

Proposition de périmètre :

- catégories d’activités configurables ;
- statistiques avancées ;
- exports de données contrôlés par la personne utilisatrice.

Le modèle de catégories, la séparation entre mesure automatique et déclaration
manuelle, les formats d’export et les limites de données restent à définir.

### V1.0 — Application personnelle stable

Proposition de périmètre :

- stabilisation de l’application personnelle et de ses mises à jour ;
- installateur Windows ;
- paramètres et contrôles utilisateur ;
- préparation d’une distribution personnelle documentée.

Les critères de stabilité, de compatibilité Windows, de mise à jour et de
support devront être définis avant de confirmer cette étape.

### Après V1.0 — Étude éventuelle Business

Une offre Business n’est pas décidée. Une étude pourrait examiner le besoin de
reporting, de gestion des équipes et des projets, ainsi qu’une éventuelle
synchronisation sécurisée. Aucune architecture cloud, collecte centralisée ou
fonction de supervision n’est engagée par cette roadmap.

Avant tout prototype ou déploiement professionnel, étudier au minimum le RGPD,
les recommandations de la CNIL, la proportionnalité du suivi, l’information des
salariés, les droits d’accès, la conservation et la sécurité. La
[vision produit](./PRODUCT_VISION.md) détaille les questions de confiance et
les limites de conception.

## Questions de gouvernance à résoudre

Chaque étape devrait être confirmée séparément après examen de sa valeur
utilisateur, de son coût de maintenance, de ses impacts sur les données et de
ses critères de validation. Les fonctionnalités proposées ne modifient pas les
spécifications actuellement implémentées tant qu’une décision documentée ne
l’a pas établi.
