# Vérification manuelle du prototype

État au 29 septembre 2026 : **validation réelle non terminée**. Aucune ROM
commerciale ni aucun fichier système privé n’a été fourni. Aucun fichier privé
n’a été recherché. Les contrôles ci-dessous ne contiennent ni chemin privé,
ni hash de ROM, capture de jeu, sauvegarde ou donnée extraite.

| Critère avec une vraie ROM HeartGold | Résultat | Motif |
| --- | --- | --- |
| Démarrage jusqu’à un monde extérieur jouable | Non vérifié | ROM privée absente |
| Écran supérieur visible, écran inférieur jamais visible | Non vérifié en jeu réel | ROM privée absente ; surface supérieure seule couverte automatiquement |
| Sauvegarde native conservée après fermeture/réouverture ; sauvegarde précédente copiée avant remplacement | Non vérifié en jeu réel | ROM privée absente ; dépôt et pont melonDS couverts automatiquement avec données synthétiques |
| Tab/Start ouvre le menu et fige le jeu | Non vérifié en jeu réel | ROM privée absente ; session/faux cœur couverts automatiquement |
| Clavier et une manette parcourent chaque tuile | Non vérifié avec une manette physique | Aucun essai matériel ; routage simulé couvert automatiquement |
| Débranchement d’une manette conserve la sélection et revient aux indications clavier | Non vérifié avec une manette physique | Retrait simulé couvert automatiquement |
| Options modifie volume, filtrage nearest/linear et plein écran | Non vérifié en jeu réel | ROM privée absente ; réglages et rendu couverts automatiquement |
| Reprendre rend le contrôle sans entrée fantôme | Non vérifié en jeu réel | ROM privée absente ; neutralisation couverte automatiquement |
| 1280 × 720, 1920 × 1080 et 3840 × 2160 conservent le ratio 4:3 | Non vérifié visuellement en jeu réel | Géométrie/rendu couverts automatiquement ; ROM privée absente |
| Fermeture propre après dix cycles pause/reprise | Non vérifié en jeu réel | ROM privée absente ; worker et pause/reprise FreeBIOS couverts automatiquement |

Les essais avec `--fake-core` utilisent uniquement une mire synthétique. Ils
ne valident ni l’exécution de HeartGold, ni ses sauvegardes, ni sa compatibilité.
Le mode de production utilise le direct boot FreeBIOS et un firmware vierge
généré localement. Le sélecteur de récupération pour fichiers système manquants
est câblé, mais un mode utilisant des BIOS privés reste hors de ce prototype.

## Contrôles disponibles sans données privées

- Suites automatisées Debug et Release : 16/16 CTest dans chaque configuration.
- Tests melonDS avec FreeBIOS, firmware vierge et données de sauvegarde
  synthétiques : inclus dans les suites ; aucune ROM commerciale exécutée.
- Paquet Windows : construit dans `dist/HeartGoldPCRemastered/`, avec Qt/QML,
  SDL3, DLL UCRT64 importées, licences et notices.
- Lancement `--fake-core` sur PATH limité au paquet et aux dossiers système :
  processus actif après cinq secondes, aucune erreur sur stderr. Le processus
  de contrôle a été arrêté ; cela vérifie le démarrage, pas le rendu visuel.
- Scan des extensions `.nds`, `.sav`, `.dsv`, `.bin`, `.rom` : zéro correspondance.
- Vérification visuelle de la démonstration : non effectuée dans ce contrôle.

Un échec de sauvegarde bloque les commandes et la reprise jusqu’à acquittement.
Si le cœur s’est déjà arrêté pour protéger la sauvegarde, l’acquittement revient
à l’accueil ; il faut corriger la cause puis sélectionner de nouveau le jeu.
