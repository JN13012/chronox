# Vision produit Chronox

État documentaire : 13 novembre 2026. Les offres ci-dessous définissent une
**cible produit**, sans annoncer leur disponibilité commerciale. La
[roadmap](ROADMAP.md) porte les jalons, dépendances et critères d'acceptation,
sans dates de livraison.

## Positionnement et offres cibles

Chronox vise une application Windows native, légère et internationale pour
comprendre son temps, organiser ses projets et rendre compte du travail
professionnel. Le fonctionnement personnel reste local et utilisable sans
compte ni synchronisation obligatoire. La confidentialité et la maîtrise des
données par la personne font partie du produit.

| Offre cible | Besoin et périmètre | Situation actuelle |
|---|---|---|
| **Chronox Personal** | Sessions, activité, historique, objectifs, statistiques et personnalisation. | Socle session/activité/historique implémenté ; objectifs et personnalisation étendue envisagés ; UX/UI, qualification et distribution à finaliser. |
| **Chronox Pro** | Clients, projets, tâches, journal de travail modifiable, temps facturable, budgets, rapports, exports et Daily Reports automatiques. | Envisagé ; aucun module métier Pro identifié dans le dépôt. |
| **Chronox Teams** | Multi-utilisateur, projets partagés, rapports collectifs, permissions, suivi des charges et synchronisation professionnelle facultative. | Envisagé ; aucun service d'équipe ni synchronisation implémentés. |

Pro s'appuierait sur le socle Personal ; Teams réutiliserait le modèle de
travail Pro. Le jalon **Business** désigne budgets, rentabilité et facturation
de Pro : il ne constitue pas une quatrième offre. Prix, licences, abonnements,
quotas et frontières commerciales exactes restent ouverts.

## Existant et niveau de validation

Analyse du README, de la SPEC, des sources et des rapports présents au dépôt.
Les résultats ci-dessous sont **consignés dans ces rapports**, sans nouvelle
exécution de l'application ou des tests pour cette mission documentaire.
Implémentation, qualification isolée et déploiement sont des états distincts ;
les rapports datés ne certifient pas l'état actuel du poste installé.

| Périmètre | État constaté et limites | Référence faisant autorité pour les détails |
|---|---|---|
| Temps de session et calendrier | Implémentés : durées monotones, périodes locales, incertitude explicite, historique. Correctif de cohérence des cumuls validé sur copies. Verrouillage/veille matériels ne sont pas certifiés par les simulations. | [SPEC](SPEC.md), [moteur temporel](TEMPORAL_ENGINE.md), [résultats du correctif](TEMPORAL_DURATION_FIX_RESULTS.md) |
| Activité clavier/souris V3 | Implémentée : seuil configurable, désactivation, couverture et incertitudes distinctes. Validation isolée P1.3.4 : 26/26 suites PASS ; matériel et longue durée restent à qualifier. Aucune reconstruction de l'activité passée. | [Moteur d'activité](ACTIVITY_ENGINE.md), [résultats P1.3.4](P1.3.4_ACTIVITY_RESULTS.md) |
| Persistance locale | SQLite V3 additive, checkpoints, récupération durable, sauvegarde cohérente et lecture historique ; migrations et invariants testés sur copies. Aucun modèle clients/projets/tâches ou déclarations de travail. | [Moteur d'activité — SQLite V3](ACTIVITY_ENGINE.md#sqlite-v3) |
| Application native | Widget, zone de notification, dashboard, navigation, historique graphique et paramètres d'activité implémentés. Installation locale et démarrage Windows optionnel documentés ; aucun installateur public distribué. | [README](../README.md), [dashboard adaptatif](UI_RESPONSIVE_DASHBOARD.md) |
| UX/UI en cours de finalisation | E2.1 implémenté et revu sur fixtures ; Titanium E2.2 est un prototype isolé de Vue générale, non généralisé. E2.2 consigne 30/30 suites PASS et des contrôles visuels ciblés ; approbation artistique finale, DPI physiques 150/200 %, lecteur d'écran complet et longue durée restent ouverts. | [Revue E2.1](E2_1_NATIVE_FINISH_REVIEW.md), [prototype E2.2](E2_2_TITANIUM_PROTOTYPE.md) |
| Fonctions futures | Objectifs, statistiques avancées, personnalisation étendue, internationalisation complète, Pro, Teams et intégrations : envisagés, sans disponibilité annoncée. | [Roadmap](ROADMAP.md) |

Les anciennes mentions « console uniquement » de la vision et de la roadmap
sont remplacées par cet état. Les spécifications et rapports historiques
conservent leur portée de version ; ce document ne recopie pas leurs règles
techniques ni leurs protocoles de test.

### Architecture et dépendances actuelles

- `src/core` : transitions, horloges, calendrier et activité ; règles métier
  séparées de Win32. `src/storage` : persistance SQLite et requêtes historiques.
- `src/windows` : événements WTS/alimentation, source de dernière entrée,
  préférences HKCU, widget et dashboard dans un même processus. Win32,
  Direct2D/DirectWrite, WIC et DWM constituent l'interface actuelle.
- C++20, CMake 3.24+, MSVC/Visual Studio 2022 et SDK Windows pour l'application ;
  SQLite 3.54.1 liée statiquement, source et empreinte fixées dans CMake.
  Le runtime MSVC et les ressources `assets/ui` font partie des besoins de
  distribution décrits dans le README. Le noyau et le stockage sont testables
  hors Windows ; cela ne constitue pas un portage de l'application.
- Aucun serveur ni service cloud requis. Les nouveaux modèles métier,
  exports, identités, permissions et échanges réseau restent à concevoir.
  La présentation actuelle est française, avec notamment une locale `fr-FR` ;
  une offre internationale n'est pas déjà disponible.

## Quatre notions de temps à garder séparées

| Notion | Sens | Limite d'interprétation |
|---|---|---|
| **Temps de session** | Durée mesurée pendant que Windows est éveillé et déverrouillé, lors de l'exécution de Chronox. | Ne prouve ni présence continue ni travail effectué. |
| **Activité clavier/souris** | Durée classée active à partir de la dernière entrée Windows et du seuil d'inactivité, grâce incluse ; bornée par le temps de session. | Ne mesure pas la productivité ; lecture, réflexion et réunions peuvent ne produire aucune entrée. Détection désactivée : suit prospectivement la session, avec mode explicite. |
| **Travail déclaré** | Futur journal modifiable : durée et description choisies par la personne, affectées à un projet ou une tâche. | Déclaration distincte des mesures ; peut inclure du travail hors ordinateur. Une correction ne réécrit jamais les mesures brutes. |
| **Heures facturables** | Future part du travail déclaré retenue selon les règles du projet/client et une validation explicite. | Ni une session ni une interaction ne deviennent automatiquement facturables ; règles d'arrondi et de tarification à définir. |

L'absence de mesure reste distincte d'un zéro mesuré ; une couverture partielle
ne doit pas devenir une journée complète par extrapolation. Le suivi éventuel
d'applications au premier plan est une piste séparée, non implémentée et non
nécessaire aux Daily Reports de base.

## Daily Reports et confidentialité — fonctionnement futur

Ce parcours est envisagé pour Pro, puis pour les usages partagés de Teams.
Il ne décrit aucune automatisation déjà livrée et n'impose pas de service d'IA.

1. **Collecter localement le nécessaire** : réutiliser les durées et leur qualité,
   les affectations choisies et le journal déclaré. Rendre la collecte visible
   et contrôlable. Ne pas étendre implicitement les données capturées.
2. **Affecter aux projets et tâches** : la personne choisit ou corrige les
   affectations ; les suggestions éventuelles restent explicables et
   facultatives. Les plages non affectées et les chevauchements sont signalés.
3. **Générer automatiquement un brouillon quotidien** : synthèse du travail
   déclaré, projets/tâches et durées autorisées. Identifier les sources, les
   manques et le statut brouillon, sans inventer de travail ni de facturation.
4. **Faire modifier et valider par le collaborateur** : il peut corriger le
   journal et le texte, retirer des éléments privés et prévisualiser le rapport.
   Les corrections restent séparées des mesures d'origine ; une modification
   après validation impose une nouvelle validation avant repartage.
5. **Partager les seules informations professionnelles autorisées** : choix
   explicite du contenu et des destinataires, après validation. Aucun envoi
   automatique d'un brouillon ou d'un historique personnel brut. Sans
   synchronisation, conserver localement le rapport ou l'exporter volontairement.
6. **Produire les rapports d'équipe selon les droits** : accès limités aux
   projets et personnes autorisés, agrégats de travail validé et charges
   déclarées/prévues. Ne pas convertir les interactions clavier/souris en
   indicateur de performance individuelle.

La mesure actuelle ne stocke ni touches, ni positions de souris, ni titres de
fenêtres, ni contenus. Les bornes d'activité et les sauvegardes peuvent cependant
révéler des périodes d'interaction : elles restent des données personnelles
sensibles à protéger, même locales. Voir les limites dans
[ACTIVITY_ENGINE.md](ACTIVITY_ENGINE.md#confidentialité).

Teams est un outil de coordination du travail, **pas un système de surveillance
permanente des employés**. Exclure surveillance cachée, keylogging, captures
permanentes, remontée continue des états individuels et classement de
productivité déduit de l'activité. Un responsable n'obtient pas, par son seul
rôle, l'accès à l'historique personnel détaillé.

La synchronisation professionnelle reste facultative, limitée à un espace
professionnel explicite et aux données autorisées. Avant sa réalisation,
définir identité, séparation personnel/professionnel, permissions, sécurité des
échanges et du stockage, conservation, export, suppression et traçabilité des
accès/partages. Le retrait d'accès ne peut pas promettre d'effacer une copie
déjà exportée chez un destinataire.

**Avant toute commercialisation dans un marché**, prévoir une analyse juridique
locale par des spécialistes : protection des données, droit du travail,
information et consultation applicables, bases légales, proportionnalité,
conservation, transferts et hébergement, droits des personnes ; ajouter les
exigences comptables/fiscales pour la facturation. Ce sont des sujets à analyser,
pas une garantie de conformité ni un avis juridique. Les rôles de traitement et
les responsabilités entre Chronox et les organisations restent à définir.

## Décisions produit encore ouvertes

- Publics et cas d'usage prioritaires ; frontières gratuites/payantes, licences,
  prix, support et engagement de maintenance par offre.
- Marchés/langues de lancement ; formats de dates, semaines, fuseaux et monnaies
  à supporter. Préserver les durées V3 et les invariants calendaires existants.
- Validation finale UX/UI, éventuelle adoption de Titanium, objectifs personnels
  et niveau de personnalisation ; aucun choix de thème imposé par cette vision.
- Affectation manuelle ou assistée, règles de correction et validation du journal,
  distinction estimation/budget/réalisé, temps facturable et arrondis.
- Formats d'export, contenu et déclenchement des Daily Reports ; automatisation
  locale par règles ou assistance facultative, sans partage implicite des données.
- Factures natives ou intégration à un outil spécialisé ; monnaies, taxes et
  exigences par marché avant engagement commercial.
- Modèle d'identité et de permissions Teams, hébergement/synchronisation,
  fonctionnement hors ligne, conflits et périmètre exact des charges partagées.

Cette mise à jour est documentaire : elle préserve Chronox V3, les calculs,
SQLite, les préférences et les travaux UX/UI. Elle n'autorise aucune migration,
installation ou généralisation du prototype.
