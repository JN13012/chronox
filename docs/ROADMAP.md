# Roadmap Chronox

État documentaire : 13 novembre 2026. Ordre de construction proposé, **sans
calendrier de livraison ni engagement commercial**. Les offres, l'inventaire
vérifié du dépôt et les règles de confidentialité sont centralisés dans la
[vision produit](PRODUCT_VISION.md) ; les règles techniques restent dans les
spécifications et rapports liés depuis celle-ci.

## Lecture et garde-fous

- **En cours** : socle déjà implémenté, finalisation ou qualification restante.
- **Envisagé** : périmètre à concevoir et valider, sans implémentation métier
  identifiée dans le dépôt. Les critères ci-dessous sont des conditions futures
  de sortie, pas des validations déjà obtenues.
- Chaque jalon requiert une revue de ses critères avant passage à « validé ».
  Aucun jalon commercial complet n'est déclaré validé ici.
- Préserver le moteur de session, l'activité V3, les calculs calendaires,
  l'historique SQLite et les préférences. Toute future évolution de stockage
  devra prévoir sauvegarde, compatibilité, migration testée sur copies et
  protection des contributions existantes ; aucune n'est effectuée ici.
- Préserver les travaux UX/UI et leur revue humaine : Titanium reste isolé
  jusqu'à décision explicite. Les futures fonctions s'intègrent sans refonte
  implicite des écrans actuels.
- Internationalisation et confidentialité se préparent dès Personal et se
  vérifient à chaque jalon. L'analyse juridique par marché précède la
  commercialisation du périmètre concerné.

## Vue synthétique

| Jalon | Offre concernée | Statut | Dépendance principale |
|---|---|---|---|
| 1. Personal | Personal, socle commun | **En cours** : UX/UI, stabilité, internationalisation et distribution à finaliser | V3 existante et qualification Windows |
| 2. Work Core | Pro, base de Teams | **Envisagé** | Socle Personal stabilisé ; modèle de travail déclaré |
| 3. Reporting | Pro, puis Teams | **Envisagé** | Work Core et validation/partage explicites |
| 4. Business | Pro | **Envisagé** | Work Core + Reporting ; règles financières par marché |
| 5. Teams | Teams | **Envisagé** | Work Core + Reporting ; identité, droits et analyse juridique |
| 6. Intégrations et fonctions avancées | Selon l'usage | **Envisagé** | Modules concernés stabilisés et interfaces contrôlées |

Cet ordre privilégie un produit local utilisable avant les services partagés.
Teams ne dépend pas obligatoirement de la facturation Business ; les
affectations et rapports validés constituent son prérequis fonctionnel.

## 1. Personal — UX/UI, stabilité et distribution

**Statut : en cours.** Session, activité, historique, widget et dashboard sont
implémentés. Les validations consignées et leurs limites sont dans la vision ;
elles ne suffisent pas à déclarer une version commerciale finalisée.

**Objectifs :** finaliser les parcours widget/dashboard/paramètres, la
lisibilité et l'accessibilité ; définir objectifs personnels, statistiques
avancées et personnalisation ; préparer localisation, stabilité et distribution
Windows reproductible.

**Dépendances :** calculs et stockage V3 existants, ressources UI, chaîne
CMake/MSVC/SDK/runtime, revue des travaux E2.1 et du prototype E2.2. Décider des
langues et versions Windows prises en charge avant qualification finale.

**Critères d'acceptation :**

- Revue humaine des écrans et décisions de design consignées ; clavier,
  lecteur d'écran, contraste élevé, petits formats, multi-écrans et DPI
  physiques qualifiés sur la matrice Windows retenue.
- Non-régression des durées et données V3 démontrée ; verrouillage, veille,
  reprise, redémarrage, récupération et stabilité prolongée qualifiés, avec
  anomalies bloquantes résolues et limites restantes explicites.
- Objectifs et statistiques personnels définis et testés sans qualifier la
  session de travail effectif ; personnalisation sans altération des mesures.
- Langues retenues et formats locaux vérifiés, y compris textes longs et
  calendrier ; aucune réinterprétation des durées historiques.
- Installation, mise à jour, désinstallation et restauration documentées et
  vérifiées sans perte de données ni réactivation silencieuse des préférences.
  Paquet de distribution, signature, politique de mise à jour et support décidés.

## 2. Work Core — clients, projets, tâches et journal

**Statut : envisagé.** Aucun module Pro existant à considérer comme disponible.

**Objectifs :** organiser clients/projets/tâches ; saisir et modifier un journal
de travail, y compris hors ordinateur ; affecter des durées et distinguer
travail déclaré, mesuré et candidat à la facturation.

**Dépendances :** socle Personal stabilisé, modèle métier et règles de
correction/validation décidés, stratégie de persistance additive et d'archivage.

**Critères d'acceptation :**

- Créer, modifier et archiver clients, projets et tâches sans perdre les liens
  du journal ni les données de mesure historiques.
- Saisir/corriger/affecter du travail, signaler chevauchements et plages non
  affectées ; conserver une distinction visible entre sources et déclarations.
- Prouver qu'une correction du journal ne change aucun cumul brut V3 ; gérer
  travail hors ordinateur et absence de mesure sans inventer d'activité.
- Conserver le journal après redémarrage ; démontrer sur copies l'intégrité,
  la sauvegarde et la compatibilité des évolutions de stockage prévues.

## 3. Reporting — Daily Reports et exports

**Statut : envisagé.** Les cumuls calendaires existants ne sont pas des Daily
Reports professionnels ni des exports métier.

**Objectifs :** rapports par période/client/projet/tâche, exports et brouillon
quotidien automatique local ; modification et validation par le collaborateur
avant tout partage. Le parcours cible est décrit une seule fois dans la
[vision — Daily Reports](PRODUCT_VISION.md#daily-reports-et-confidentialité--fonctionnement-futur).

**Dépendances :** Work Core, provenance/qualité des données, règles de validation,
formats et déclenchement à décider. Le brouillon local ne requiert pas Teams ;
son partage synchronisé requiert les droits et la sécurité du jalon Teams.

**Critères d'acceptation :**

- Générer un brouillon à partir des seules données nécessaires, en distinguant
  session, activité, travail déclaré et heures facturables si disponibles.
- Modifier, prévisualiser et valider le rapport ; toute correction ultérieure
  remet le contenu en attente de validation. Aucun brouillon envoyé seul.
- Exporter uniquement les champs/périodes choisis ; vérifier les totaux contre
  le journal, les encodages, formats locaux et états non mesurés/partiels.
- Choisir contenu et destinataires autorisés avant partage ; aucune donnée
  personnelle brute ajoutée par défaut. Répéter une génération sans doublon
  ni perte de corrections ; gérer un déclenchement manqué hors exécution.

## 4. Business — budgets, rentabilité et facturation

**Statut : envisagé.** Extension Pro, sans calcul financier ou facture existants.

**Objectifs :** budgets temps/montants, tarifs, suivi des écarts, coûts et
rentabilité ; facturation native ou intégration selon la décision produit.

**Dépendances :** Work Core et Reporting ; définition des heures facturables,
validation, arrondis, monnaies et coûts ; analyse comptable/fiscale et juridique
par marché avant commercialisation.

**Critères d'acceptation :**

- Séparer budget, estimation, temps déclaré, temps validé facturable et montant
  facturé ; ne jamais convertir automatiquement l'activité en revenu.
- Vérifier calculs et arrondis sur cas de référence ; expliciter les coûts
  manquants plutôt qu'afficher une rentabilité supposée.
- Démontrer un parcours de validation puis export ou émission selon l'option
  retenue ; prévenir la double facturation et tracer les corrections.
- Qualifier le parcours financier pour chaque marché et monnaie retenus avant
  de revendiquer une offre de facturation disponible.

## 5. Teams — collaboration et synchronisation facultative

**Statut : envisagé.** Aucun multi-utilisateur, serveur ou contrôle d'accès
partagé implémenté. La synchronisation ne devient pas un prérequis de Personal
ou du journal local Pro.

**Objectifs :** identités, espaces professionnels, projets partagés, permissions,
rapports collectifs et suivi des charges prévues/déclarées ; synchronisation
professionnelle optionnelle. Exclure la surveillance permanente des employés.

**Dépendances :** Work Core et Reporting, matrice de droits, analyse juridique
par marché, responsabilités de traitement, choix d'hébergement et modèle de
menaces. Business seulement si des montants sont partagés.

**Critères d'acceptation :**

- Tester les accès permis et refusés par rôle/projet, la révocation et
  l'isolation entre organisations et entre espaces personnels/professionnels.
- Synchroniser seulement les données professionnelles autorisées ; tester
  hors ligne, reconnexion, conflits et répétitions sans perte ni doublon.
- Produire des agrégats à partir du travail validé selon les droits ; ne pas
  exposer l'historique personnel ou les états clavier/souris en continu.
- Démontrer sécurité des échanges et du stockage, traçabilité, conservation,
  export et suppression ; préciser les limites des copies déjà exportées.
- Vérifier information des collaborateurs, validation avant partage et absence
  de collecte cachée ; consigner l'analyse juridique et les conditions de
  déploiement pour chaque marché ciblé.

## 6. Intégrations et fonctions avancées

**Statut : envisagé, à prioriser par usage.**

**Objectifs :** évaluer connecteurs calendrier/gestion de projets/comptabilité,
API contrôlée, statistiques avancées et assistance facultative. Le suivi
applicatif éventuel nécessite une décision séparée de minimisation ; il n'est
pas un prérequis aux rapports quotidiens.

**Dépendances :** modèles et interfaces des jalons concernés stabilisés,
authentification et permissions adaptées à chaque connecteur ; règles des
services tiers et analyse des données transférées avant adoption.

**Critères d'acceptation :**

- Valider une valeur utilisateur et un périmètre minimal pour chaque intégration
  avant développement ; documenter les données entrantes et sortantes.
- Activation volontaire, autorisations limitées, déconnexion et gestion des
  erreurs/limites du tiers testées ; aucun transfert externe implicite.
- Vérifier absence de doublons et préservation des données V3 et du journal ;
  mesurer le coût en ressources et le comportement hors connexion.
- Toute assistance produit une proposition explicable et modifiable, sans
  validation ou partage automatique du travail d'une personne.

Les [décisions produit ouvertes](PRODUCT_VISION.md#décisions-produit-encore-ouvertes)
conditionnent le périmètre final de ces jalons. Cette mission s'arrête à leur
structuration documentaire ; aucun code, schéma, réglage ou déploiement ne fait
partie de cette mise à jour.
