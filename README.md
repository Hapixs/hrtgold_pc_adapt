# HeartGold PC Remastered

Prototype Windows 11 x64 d’une présentation PC pour une ROM Pokémon HeartGold
fournie localement par son propriétaire. Le programme ne fournit, ne copie et ne
distribue aucune ROM, BIOS privé, firmware privé, sauvegarde ou ressource de jeu.

## Construire et tester

Installer les dépendances MSYS2 UCRT64, puis construire et lancer les tests :

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1 -Preset ucrt-debug
./scripts/build.ps1 -Preset ucrt-release
```

Qt Widgets fournit le sélecteur natif ; Qt Quick affiche uniquement l’écran
supérieur au ratio 4:3. SDL3 gère le son et les manettes. Le cœur de production
est melonDS 1.1, fixé à `b86390e4428bf38ce4c1ce0e9ca446d6d25955e8`.
Le checkout nécessite `git submodule update --init`.

## Lancer

Le mode production est utilisé par défaut. Le bouton **Choisir un jeu** ouvre
un sélecteur local et accepte les fichiers `.nds` identifiés HeartGold (`IPK`).
Un chemin fourni en ligne de commande doit être absolu et n’est pas mémorisé :

```powershell
./build/ucrt-release/heartgold_pc.exe
./build/ucrt-release/heartgold_pc.exe --rom 'D:\Private\Pokemon HeartGold.nds'
```

La démonstration explicite fonctionne hors ligne sans ROM ni fichier privé :

```powershell
./build/ucrt-release/heartgold_pc.exe --fake-core
```

Elle affiche une mire synthétique puis ouvre le menu. Elle ne valide pas la
compatibilité du jeu réel. Les six tuiles de jeu sont sélectionnables et
signalent leur indisponibilité ; **Options** et **Reprendre** fonctionnent.

- **Tab / Start** : menu et pause ; **Échap / bouton est** : retour/reprise.
- **Flèches ou WASD / croix directionnelle** : navigation.
- **Entrée ou E / bouton sud** : confirmation ; **F11** : plein écran.
- **Options** : volume, filtrage nearest/linear, plein écran et réouverture
  facultative du dernier jeu. Seule cette option autorise à conserver le chemin
  d’une sélection faite dans le sélecteur. `--rom` reste temporaire.

Les sauvegardes natives sont stockées dans
`%LOCALAPPDATA%/HeartGoldPCRemastered/saves/<sha256>/game.sav`, avec une copie
datée avant remplacement. Les journaux bornés et réglages sont dans le dossier
application. Les erreurs affichées et le journal application ne reprennent
jamais les chemins de fichiers privés. Si le son est indisponible, la session
continue silencieusement. Une erreur de sauvegarde bloque la reprise jusqu’à
acquittement ; un cœur déjà arrêté revient ensuite à l’accueil.

La production utilise le direct boot avec FreeBIOS et un firmware vierge
créé localement. Aucun mode BIOS privé n’est activé dans cette version.
La validation manuelle avec une vraie ROM reste **non vérifiée** : voir
[la checklist détaillée](docs/testing/manual-prototype-checklist.md).

## Paquet Windows interne

```powershell
./scripts/package.ps1
./dist/HeartGoldPCRemastered/heartgold_pc.exe --fake-core
```

Le script reconstruit Release, exécute CTest, déploie Qt/QML, SDL3 et les DLL
UCRT64 réellement importées, puis ajoute les licences. Il refuse les fichiers
`.nds`, `.sav`, `.dsv`, `.bin` ou `.rom` dans le paquet. `dist/` est un artefact
local ignoré par Git. Le paquet est destiné à Windows 11 x64 ; son contrôle
sur PATH minimal ne remplace pas un essai sur une autre machine propre.

Projet sous GPL-3.0-or-later, car lié à melonDS. Voir
[la licence](LICENSES/GPL-3.0-or-later.txt) et
[les composants tiers](THIRD_PARTY.md). Le paquet est interne ; toute diffusion
externe doit accompagner les binaires du code source correspondant et des
notices/licences applicables.
