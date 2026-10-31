# Chronox V0 — spécification

## Périmètre

Chronox est une application native Windows 10/11, écrite en C++20. Le prototype
P1 du widget V0.4 affiche le temps mesuré et l’état de session. Le moteur mesure
la durée pendant laquelle **la session Windows courante est déverrouillée et
l’ordinateur éveillé simultanément**.

Inclus :

- état initial de verrouillage interrogé au lancement ;
- notifications Windows de verrouillage/déverrouillage et de veille/réveil ;
- temps cumulé et état de session affichés dans le widget ; rapports du moteur
  par jour, semaine, mois et année ;
- historique local persistant SQLite ;
- noyau de transition et calculs calendaires indépendants de Win32, avec tests
  automatisés ;
- widget Win32 natif avec ressources PNG transparentes, rendu WIC,
  Direct2D/DirectWrite, languette rétractable et icône de notification.

Exclus : dashboard, historique graphique, suivi des applications, service
Windows, serveur, service cloud et mesure antérieure au démarrage du programme.

## Règles de comptabilisation

- Le compteur tourne uniquement lorsque `déverrouillée && éveillée`.
- Il s’arrête sur verrouillage ou entrée en veille et reprend uniquement lorsque
  les deux conditions sont de nouveau réunies.
- Les transitions et points de sauvegarde sont horodatés à partir d’une horloge
  monotone ancrée à l’heure UTC au lancement. Les ajustements de l’horloge
  système pendant l’exécution ne produisent pas de durées négatives.
- Le temps de veille, verrouillé et hors exécution n’est jamais compté.
- Au lancement, WTS (`WTSSessionInfoEx`) fournit l’état de verrouillage de la
  session courante. Le processus démarre éveillé puisqu’il est en cours
  d’exécution. Une erreur de lecture WTS est signalée et empêche un démarrage
  avec un état supposé.
- Les intervalles fermés sont enregistrés dans
  `%LOCALAPPDATA%\Chronox\chronox.db`, en UTC, dans des transactions SQLite.
  Une contrainte d’unicité n’autorise qu’un seul intervalle ouvert.
- Le schéma SQLite utilise `PRAGMA user_version` et des migrations
  transactionnelles. Une version de Chronox ne modifie pas une base dont le
  schéma est plus récent.
- L’intervalle actif est sauvegardé au démarrage, à chaque transition et au
  moins chaque minute en fonctionnement normal. Après un arrêt inattendu, il
  est fermé au dernier point de sauvegarde durable ; le temps depuis ce point
  jusqu’au prochain lancement n’est pas compté. La dernière période peut être
  sous-estimée du temps écoulé depuis ce point de sauvegarde.
- Un mutex Windows interdit deux instances simultanées dans la même session,
  afin qu’un démarrage ne récupère pas l’intervalle actif d’une autre instance.
- Les rapports jour, semaine (lundi à dimanche), mois et année calculent
  l’intersection des intervalles UTC avec les limites calendaires du fuseau
  local. Les journées de changement d’heure peuvent durer 23 ou 25 heures.
- Les événements redondants ne changent pas l’état ni le cumul. Les événements
  de verrouillage/déverrouillage reçus pendant la veille mettent à jour l’état
  de session sans faire tourner le compteur.
- Le widget P1 affiche le cumul local du jour, l’état de session et la durée
  cumulée par le tracker depuis le lancement courant. Il rafraîchit l’affichage
  une fois par seconde seulement lorsque le widget est déployé et que le suivi
  compte ; le timer de persistance reste à une minute.
- La fenêtre est positionnée au démarrage en bas à droite de la zone de travail
  du moniteur principal, au-dessus de la barre des tâches. Une languette permet
  de la rétracter ou de la déployer. L’icône de notification permet de masquer,
  restaurer, épingler temporairement au premier plan ou quitter Chronox.
- Les bitmaps PNG sont décodés et mis en cache à la taille du widget ; les
  éléments textuels et l’état sont dessinés par Direct2D/DirectWrite. Les
  ressources fournies restent provisoires ; leur composition est documentée
  dans la [spécification UI](./UI_SPEC.md).
- Les préférences de position, d’état et d’épinglage ne sont pas persistées
  dans P1. Le dashboard, les anneaux horaires et la navigation entre vues ne
  sont pas implémentés.

## Cas limites

| Situation | Comportement |
|---|---|
| Démarrage avec session déverrouillée | Le compteur commence immédiatement, sans attendre une notification d’ouverture. |
| Démarrage avec session verrouillée | Le compteur reste à zéro jusqu’au déverrouillage. |
| Ordre initial « veille/réveil » absent | Le processus démarre éveillé ; il ne compte qu’à partir de son lancement. |
| Notifications identiques répétées | Aucun double départ ni ajout de durée. |
| Verrouillage pendant la veille | L’état reste inactif ; le réveil ne reprend pas le compteur. |
| Arrêt normal | L’intervalle est fermé et sauvegardé avant la fermeture du widget. |
| Arrêt inattendu | La récupération conserve au plus le temps jusqu’au dernier point de sauvegarde ; le temps hors processus n’est pas compté. |
| Redémarrage de Chronox | Les anciens intervalles restent disponibles et une nouvelle période débute à l’état Windows initial. |
| Changement d’heure | Les intervalles restent en UTC ; les rapports suivent les limites calendaires locales, y compris les journées de 23 ou 25 heures. |
| État de session inaccessible | Erreur explicite et arrêt, sans état initial de repli. |

## Base de données et tests

CMake télécharge l’amalgame SQLite 3.54.1 depuis la source officielle avec une
empreinte SHA-256 épinglée. Aucun serveur ni bibliothèque SQLite installée sur
la machine n’est requis. Les tests automatisés couvrent la durabilité après
réouverture, les transitions idempotentes, la récupération après interruption,
les sessions traversant minuit, les quatre rapports calendaires et le passage
à l’heure d’été et à l’heure d’hiver.
Sous Windows, le test DST utilise un convertisseur de fuseau déterministe
pour ne pas dépendre de la conversion d’offset de `_tzset` dans le CRT du
runner ; les autres plateformes exercent les règles POSIX du runtime.

## Vérifications manuelles sous Windows

Ces scénarios doivent être exécutés sur une session Windows interactive. La CI
Windows vérifie la compilation et les tests automatisés ; elle ne simule pas les
événements matériels/réels de veille et de réveil. Ces tests manuels ne sont pas
validés par une exécution Linux :

1. Lancer Chronox depuis une session déjà déverrouillée. Confirmer que le cumul
   évolue dès le lancement, sans attendre une notification de déverrouillage.
2. Depuis une session déjà verrouillée, démarrer Chronox si l’environnement le
   permet ; confirmer un cumul nul jusqu’au prochain déverrouillage.
3. Verrouiller avec `Win+L`, attendre au moins quelques secondes, puis
   déverrouiller. Confirmer l’arrêt, la reprise et l’exclusion du temps verrouillé.
4. Depuis une session déverrouillée, mettre Windows en veille, attendre, puis
   réveiller le PC. Confirmer que le temps de veille est exclu.
5. Verrouiller avant la veille, réveiller puis déverrouiller. Confirmer que le
   compteur reste arrêté jusqu’au déverrouillage.
6. Vérifier que les transitions et les quatre rapports s’affichent, puis envoyer
   Ctrl+C. Confirmer une sortie propre, le cumul final et la présence de la base
   dans `%LOCALAPPDATA%\Chronox\chronox.db`.
7. Relancer Chronox et vérifier que les rapports conservent les périodes déjà
   enregistrées.
8. Tester une fermeture de session et un arrêt/redémarrage. Le démarrage suivant
   doit conserver l’historique sans ajouter le temps pendant lequel le
   programme était arrêté.

Ces scénarios de verrouillage, veille et arrêt sur un vrai PC Windows ne sont
pas validés par les seuls tests automatisés ou la CI.
