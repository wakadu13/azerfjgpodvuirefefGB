# 🐥 ZEFFALIE COME BACK : Opération HK416

FPS en **C** avec **raylib**, jouable **seul**, **à deux sur le même PC** (écran partagé) ou **jusqu'à 50 en ligne** (liste des parties ouvertes, comme sur un serveur de jeu). Disponible pour **Windows**, **macOS (Apple Silicon)**, **Linux**, **Android** et **iOS**. Vous incarnez à la première personne un opérateur armé d'un **HK416** qui protège ses **poussins** contre des vagues de **mercenaires en treillis olive**, le visage peint en vert et les yeux cernés de noir, une **chaîne en métal** autour du cou, qui avancent comme des zombies, bras tendus, bien décidés à tous les enlever.

![Aperçu](docs/apercu.png)

Tout est généré par le code (modèles 3D, sons, textures), sauf l'enregistrement du « Kyrie des gueux » (`assets/kyrie_des_gueux.ogg`), intégré directement dans l'exécutable par `scripts/embed_music.py` : **aucun fichier externe à fournir**.

**Ambiance** : la partie se joue **de nuit** (ciel étoilé, lune, lampadaires et lampe du stand qui éclairent l'arène). Les mercenaires lancent des « **On va croquer les poussins !** » (bulle au-dessus de la tête et voix grognée). En fond, le chant militaire **« Kyrie des gueux »** accompagné d'une **marche au tambour** générée par le code.

## 🎮 Le jeu

- L'arène est un **camp militaire de nuit** : sol de gravier, enceinte en **gabions**, grandes **tentes** beiges à armature noire et une tente vert olive, murets de sacs de sable, caisses militaires, grands lampadaires, panneaux SORTIE et cyprès derrière les murs.
- Au centre de l'arène, **12 poussins** sautillent sur le comptoir du stand.
- Les mercenaires entrent par les **4 portes** de l'arène et foncent vers le comptoir pour attraper les poussins. Chaque poussin enlevé les rend **plus costauds et plus résistants**.
- Un mercenaire qui brandit un poussin met encore un instant à l'emporter : **abattez-le à temps et le poussin est sauvé**.
- Si vous vous approchez trop d'eux, ils vous attaquent.
- **Game over** si vous mourez **ou** si tous les poussins sont enlevés.
- Entre deux vagues : +2 poussins, +90 munitions, +25 PV.
- Les ennemis abattus lâchent parfois des **caisses de munitions** ou une **salade** (soin).

### Les ennemis

| Ennemi | Description |
|---|---|
| **Le Mercenaire** | L'ennemi de base : treillis olive aux manches retroussées, brassard orange taché de sang, épaulettes rouges, béret glissé sous l'épaulette ou foulard rouge et noir, visage peint (3 styles), chaîne autour du cou. |
| **L'Éclaireur** | Plus petit, rapide, foulard rouge et noir, et il vous fonce dessus en courant. Apparaît dès la vague 2. |
| **Le Colosse** | Énorme, lent, très résistant, chaîne en bandoulière en plus et cadenas. Dès la vague 3. |
| **LE CHEF DE GUERRE** | Boss géant au treillis plus sombre, béret sur la tête et cadenas doré, toutes les 5 vagues. |

### Le HK416

- Chargeur de 30 coups, ~850 coups/min, mode **auto / semi-auto**.
- Viseur **point rouge** (clic droit) : zoom, précision quasi parfaite.
- Recul, dispersion selon le mouvement, flash du canon qui éclaire la scène, traçantes.
- Rechargement animé (chargeur qui sort/rentre), plus long à vide (culasse).
- **Headshot = dégâts x3.**

### Le harpon

- Deuxième arme : **2**, **E** ou la molette (**1** pour revenir au HK416), **RB** à la manette, bouton **HARPON** sur mobile.
- Tire une flèche : le mercenaire touché **meurt en un coup et est projeté** à la renverse en tournoyant, la flèche plantée dans le ventre.
- En vol, il **percute les autres mercenaires** (gros dégâts, il peut même les faire décoller).
- 1 harpon chargé + 6 en réserve. +3 à chaque vague, +1 par caisse de munitions. 1,3 s pour recharger.

## 👥 Multijoueur

Au menu :

| Touche | Mode |
|---|---|
| **1** | Solo |
| **2** | Coop **écran partagé** : 2 joueurs sur le même PC |
| **3** | **Héberger** / **créer** une partie (jusqu'à 50 joueurs, visible dans la liste en ligne) |
| **4** | **Rejoindre** une partie : **liste de toutes les parties en ligne** |

Dans tous les modes à plusieurs :

- Les mercenaires s'en prennent au joueur le plus proche ; ils sont plus nombreux (+50 % par joueur supplémentaire) et plus résistants.
- Plus il y a de joueurs, plus il y a de mercenaires **en même temps** sur le terrain (40 en solo, +2 par joueur, jusqu'à 120) et plus ils arrivent vite : une partie à 50 joueurs devient une vraie bataille.
- Un joueur à 0 PV est **à terre** et revient au début de la vague suivante.
- Game over si tous les joueurs sont à terre ou si tous les poussins sont enlevés.
- Le score est commun, les éliminations sont comptées par joueur. Chaque soldat porte une bande de couleur sur le casque (bleu, orange, vert, violet, puis une couleur différente pour chacun des 50 joueurs). Le classement affiche les 8 meilleurs et votre place.

### Écran partagé

L'écran se coupe en deux, chaque joueur a sa vue, son HUD et son radar. Contrôles du joueur 2 plus bas.

![Coop](docs/coop.png)

### Réseau et parties en ligne (PC, Mac, Linux, Android, iOS)

1. Sur un appareil, appuyez sur **3** (HÉBERGER) : la partie apparaît dans la **liste en ligne** de tous les joueurs, et l'écran affiche aussi son **adresse IP** locale (par ex. `192.168.1.20`).
2. Sur les autres (jusqu'à 49), appuyez sur **4** (REJOINDRE) : la liste montre **toutes les parties hébergées, sur Internet comme sur le même Wi-Fi** (nom, joueurs, vague). Un clic pour entrer. On peut aussi taper l'adresse IP de l'hôte (même Wi-Fi).
3. Quand tout le monde est dans la salle d'attente, l'hôte appuie sur **Entrée** (d'autres peuvent arriver ensuite).

- La liste passe par un petit serveur MQTT sur Internet (adresse et identifiants dans `src/config.h`, `LOBBY_*`) qui ne sert que de tableau d'affichage : très peu de données. La partie elle-même se joue en direct entre les appareils.
- Pour passer les box, chaque appareil demande son adresse Internet à un serveur STUN public, puis l'hôte « perce » sa box vers chaque nouveau joueur (*hole punching*, `src/lobby.c`). Ça marche avec la plupart des box ; si une box ou un opérateur (certains réseaux 4G) le bloque, l'hôte peut ouvrir le **port UDP 27016** sur sa box.
- Sans serveur configuré (`LOBBY_HOST` vide), la liste reste vide et on rejoint par adresse IP comme avant.
- Le pare-feu peut demander l'autorisation au premier lancement : acceptez.
- En réseau, **Échap** ouvre un menu mais **ne met pas la partie en pause** (Q pour quitter).
- Un joueur peut rejoindre une partie déjà commencée.

![Réseau](docs/reseau.png)

### En ligne (navigateur, page claude.ai du jeu)

La page claude.ai du jeu sert de **site de jeu en ligne** : chacun s'y connecte avec son **compte claude.ai** et voit **toutes les parties ouvertes**, pas seulement celles de son réseau.

1. **CRÉER une partie en ligne** : la partie apparaît aussitôt chez tous les joueurs qui ont la page ouverte (avec un code à 4 chiffres en secours).
2. **PARTIES EN LIGNE** : la liste affiche le nom de l'hôte (« Partie de … »), le nombre de joueurs (jusqu'à **50**) et la vague en cours. **Un clic** pour rejoindre, même une partie déjà commencée.
3. L'hôte lance la partie quand il veut ; d'autres joueurs peuvent arriver ensuite.

- Pour inviter quelqu'un : bouton **Partager** de la page. Pour **créer** une partie, il faut au moins l'accès **Contributeur** ; pour en rejoindre une, l'accès à la page suffit.
- L'hôte fait tourner la partie dans son navigateur (comme en réseau local) ; s'il ferme la page, la partie s'arrête.
- Le relais limite chaque page à environ 40 messages par seconde : l'état du monde est compressé (6 Ko à 50 joueurs → moins de 2 Ko) et envoyé en un seul message à tous ; chaque joueur renvoie le sien par sa « présence » sur la page.

## ⌨️ Contrôles

| Touche | Action |
|---|---|
| ZQSD / WASD / flèches | Se déplacer (AZERTY et QWERTY supportés) |
| Souris | Viser |
| Clic gauche | Tirer |
| Clic droit | Viseur point rouge |
| R | Recharger |
| 1 / 2, E, molette | HK416 / harpon |
| B | Auto / semi-auto |
| Maj gauche | Sprint |
| Espace | Sauter (on peut monter sur les caisses) |
| Échap / P | Pause |

En solo, une manette fonctionne aussi (mêmes boutons que le joueur 2 ci-dessous).

### Joueur 2 (coop)

| Action | Manette | Clavier (sans manette) |
|---|---|---|
| Se déplacer | Stick gauche | I J K L |
| Viser | Stick droit | Flèches |
| Tirer | RT | Ctrl droit |
| Viseur point rouge | LT | Maj droite |
| Recharger | X / Carré | U |
| Harpon | RB / R1 | M |
| Sauter | A / Croix | O |
| Sprint | L3 | H |
| Mode de tir | Y / Triangle | N |
| Pause | Start | Échap / P |

Avec deux manettes, la deuxième peut aussi contrôler le joueur 1.

## ⬇️ Télécharger

### Navigateur (PC, Mac, Linux, téléphone, tablette)

Le jeu tourne aussi **dans un navigateur** (Chrome, Edge, Firefox, Safari), sans rien installer :

- **Ordinateur** : clavier + souris, comme la version PC. Cliquez dans le jeu pour capturer la souris ; **Échap** la libère et met en pause.
- **Téléphone / tablette** : commandes tactiles détectées automatiquement, en **paysage** ; le jeu passe en plein écran au premier toucher.
- Modes **solo** et **écran partagé**, et **parties en ligne jusqu'à 50 joueurs** sur la **page claude.ai du jeu** (voir [En ligne](#en-ligne-navigateur-page-claudeai-du-jeu)). Les messages passent par le relais temps réel de claude.ai (`src/netsock_web.c` + module `ZNet` de `web/shell.html`), avec le même protocole que la version PC.
- Hors de claude.ai (fichier html, GitHub Pages…), le menu réseau est masqué : les navigateurs n'autorisent pas l'UDP.

Mise en ligne : à chaque mise à jour de `main`, le jeu est compilé en **un seul fichier** `ZeffalieComeBack-web.html` (dans la release : il suffit de l'ouvrir d'un double-clic, sans serveur) et publié sur **GitHub Pages** si c'est activé (*Settings → Pages → Source : GitHub Actions* ; sur un dépôt privé, GitHub Pages demande un compte payant). Ce fichier peut aussi être déposé tel quel sur n'importe quel hébergeur web (itch.io, Netlify…). Pour le compiler soi-même :

```bash
bash scripts/build_web.sh <raylib compilée pour le web> build-web   # emcc dans le PATH
# puis ouvrir build-web/index.html dans le navigateur (double-clic)
```

### Windows

**[`release/ZeffalieComeBack.exe`](release/ZeffalieComeBack.exe)** : autonome (aucune DLL à installer), double-cliquez dessus.

> Windows SmartScreen peut afficher un avertissement car l'exe n'est pas signé : « Informations complémentaires » → « Exécuter quand même ».

### macOS (Apple Silicon : M1, M2, M3, M4…)

L'application est compilée automatiquement sur un Mac par GitHub Actions :

- après fusion sur `main` : **[page des releases → `ZeffalieComeBack-macOS-arm64.zip`](../../releases/latest)** ;
- sinon : onglet **Actions** → dernier run « Build » → section *Artifacts* → `ZeffalieComeBack-macOS-arm64`.

Dézippez puis lancez **ZeffalieComeBack.app**. L'application n'étant pas signée par un développeur Apple, macOS la bloque la première fois :

- **clic droit** sur `ZeffalieComeBack.app` → **Ouvrir** → **Ouvrir** ;
- ou, si macOS dit qu'elle est « endommagée », dans le Terminal : `xattr -dr com.apple.quarantine ~/Downloads/ZeffalieComeBack.app`

### Linux

Même endroit que pour macOS : `ZeffalieComeBack-linux-x64.tar.gz` (ou compilez, voir plus bas).

### Android

`ZeffalieComeBack-android.apk`, sur la [page des releases](../../releases/latest) (ou *Artifacts* du dernier run « Build »).
Ouvrez-le sur le téléphone et autorisez l'installation d'applications de source inconnue quand Android le demande.

### iOS (iPhone / iPad)

`ZeffalieComeBack-iOS.ipa`, même endroit. Apple n'autorise pas l'installation directe d'une app non signée :
installez-la avec **[Sideloadly](https://sideloadly.io)** ou **[AltStore](https://altstore.io)** (gratuits, avec votre identifiant Apple).
Elle doit alors être réinstallée tous les 7 jours (limite Apple pour les comptes gratuits).

## 📱 Contrôles tactiles

| Zone | Action |
|---|---|
| Moitié gauche | Joystick (il apparaît sous le doigt). Poussé à fond vers l'avant : sprint |
| Moitié droite | Glisser pour viser |
| **FEU** | Tirer (on peut aussi viser en glissant le doigt dessus) |
| **VISER** | Viseur point rouge (appui = activer / désactiver) |
| **SAUT**, **RECH.**, **HARPON** | Sauter, recharger, changer d'arme |
| **II** | Pause |

Le multijoueur réseau marche aussi sur téléphone (même Wi-Fi) ; l'écran partagé est réservé au PC.
Une manette Bluetooth fonctionne aussi.

## 🗂️ Organisation du code

| Fichier | Rôle |
|---|---|
| `src/main.c` | Point d'entrée, fenêtre, boucle principale |
| `src/game.c` | États du jeu (titre, salle d'attente, partie, pause, game over) et écran partagé |
| `src/net.c` | Multijoueur réseau : l'hôte simule la partie et envoie l'état du monde, chaque client gère son joueur et envoie ses tirs |
| `src/netsock.c` | Sockets UDP (et TCP pour la liste en ligne) pour Windows, macOS, Linux, Android et iOS |
| `src/lobby.c` | Liste des parties en ligne : client MQTT, adresse Internet (STUN) et perçage des box |
| `src/player.c` | Commandes (clavier/souris, manette, 2ᵉ clavier), déplacements, vie, caméra, modèle du coéquipier |
| `src/weapon.c` | HK416 : tir, recul, rechargement, modèle 3D et mains |
| `src/enemy.c` | Mercenaires : apparition, IA, dégâts |
| `src/enemymodel.c` | Modèle 3D des mercenaires : treillis, visage peint, brassard, chaînes (un seul mesh généré par carrure), démarche de zombie |
| `src/waves.c` | Enchaînement des vagues et récompenses |
| `src/pickup.c` | Bonus (munitions, salade) |
| `src/map.c` | Arène, stand des poussins, collisions |
| `src/harpoon.c` | Flèches du harpon (projectiles) |
| `src/effects.c` | Particules et traçantes |
| `src/hud.c` | Interface 2D et écrans |
| `src/ui.c` | Échelle de l'interface et boutons (souris / doigt) |
| `src/touch.c` | Contrôles tactiles (mobile) |
| `src/render.c` | Shader (dont l'usure du treillis) et primitives 3D (cube, sphère, cylindre), poussin |
| `src/audio.c` | Sons générés par le code |
| `src/config.h` | Toutes les constantes de réglage (vitesses, dégâts, munitions…) |

## 🔧 Compilation

Nécessite **raylib 5.5**.

### Avec CMake (le plus simple, télécharge raylib automatiquement si besoin)

```bash
cmake -B build
cmake --build build
./build/zeffalie_come_back
```

### Avec make (raylib déjà installée)

```bash
make
./zeffalie_come_back
```

Si raylib est compilée depuis les sources dans un dossier :

```bash
make RAYLIB_PATH=/chemin/vers/raylib
```

Pour régénérer l'exe Windows depuis Linux (MinGW + raylib compilée pour Windows) :

```bash
make windows RAYLIB_WIN_PATH=/chemin/vers/raylib-windows
```

### Mobile

- **Android** : `scripts/build_android.sh` (NDK + SDK Android, sans Gradle).
- **iOS** (sur un Mac avec Xcode) : `scripts/build_ios.sh device` ou `simulator`. raylib n'ayant pas de version iOS officielle, le jeu utilise son backend **SDL2**.
- Pour tester l'interface tactile sur PC : `BR_TOUCH=1 ./zeffalie_come_back` (la souris joue le rôle du doigt).

Le workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) compile automatiquement les 5 versions à chaque pull request. Il lance aussi le jeu dans un émulateur Android et un simulateur iOS, dont les captures sont publiées sur la branche `ci-captures`.

Sous Linux, les dépendances de raylib sont : `libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl1-mesa-dev`.
