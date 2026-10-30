# Chronox V0 — spécification

## Périmètre

Chronox V0 est un programme console natif Windows 10/11, écrit en C++20. Il
mesure la durée pendant laquelle **la session Windows courante est déverrouillée
et l’ordinateur éveillé simultanément**.

Inclus :

- état initial de verrouillage interrogé au lancement ;
- notifications Windows de verrouillage/déverrouillage et de veille/réveil ;
- temps cumulé affiché avec les transitions d’état ;
- noyau de transition indépendant de Win32 et tests automatisés.

Exclus : interface graphique, service Windows, réseau, base de données,
persistance, historique et mesure antérieure au démarrage du programme.

## Règles de comptabilisation

- Le compteur tourne uniquement lorsque `déverrouillée && éveillée`.
- Il s’arrête sur verrouillage ou entrée en veille et reprend uniquement lorsque
  les deux conditions sont de nouveau réunies.
- Les durées sont mesurées avec une horloge monotone. Le temps de veille,
  verrouillé et hors exécution n’est pas compté.
- Au lancement, WTS (`WTSSessionInfoEx`) fournit l’état de verrouillage de la
  session courante. Le processus démarre éveillé puisqu’il est en cours
  d’exécution. Une erreur de lecture WTS est signalée et empêche un démarrage
  avec un état supposé.
- Le cumul est en mémoire : un arrêt ou redémarrage termine la mesure. Au
  prochain lancement, un nouveau cumul commence ; le temps écoulé programme
  fermé n’est jamais reconstitué.
- Les événements redondants ne changent pas l’état ni le cumul. Les événements
  de verrouillage/déverrouillage reçus pendant la veille mettent à jour l’état
  de session sans faire tourner le compteur.
- Le cumul courant est affiché au démarrage, à chaque transition, puis chaque
  minute pendant l’exécution.

## Cas limites

| Situation | Comportement |
|---|---|
| Démarrage avec session déverrouillée | Le compteur commence immédiatement, sans attendre une notification d’ouverture. |
| Démarrage avec session verrouillée | Le compteur reste à zéro jusqu’au déverrouillage. |
| Ordre initial « veille/réveil » absent | Le processus démarre éveillé ; il ne compte qu’à partir de son lancement. |
| Notifications identiques répétées | Aucun double départ ni ajout de durée. |
| Verrouillage pendant la veille | L’état reste inactif ; le réveil ne reprend pas le compteur. |
| Arrêt/redémarrage | Le dernier cumul en mémoire est affiché à la fin normale ; aucun temps hors processus n’est compté. |
| État de session inaccessible | Erreur explicite et arrêt, sans état initial de repli. |

## Vérifications manuelles sous Windows

Ces scénarios doivent être exécutés sur une session Windows interactive. La CI
Windows vérifie la compilation et les tests du noyau ; elle ne simule pas les
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
6. Vérifier que les transitions et le cumul périodique/final s’affichent une
   seule fois, puis envoyer Ctrl+C. Confirmer une sortie propre et le cumul final.
7. Tester également une fermeture de session et un arrêt/redémarrage : l’arrêt
   normal doit afficher le cumul final et un nouveau lancement repartir à zéro.
