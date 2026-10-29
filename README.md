# 🐥 ZEFFALIE COME BACK : Opération HK416

FPS en **C** avec **raylib**, jouable **seul**, **à deux sur le même PC** (écran partagé) ou **jusqu'à 50 en ligne** : la **liste des parties ouvertes dans le monde entier** est tenue par un petit [serveur de parties](#-serveur-de-parties-liste-mondiale) à installer sur votre serveur. Disponible pour **Windows**, **macOS (Apple Silicon)**, **Linux**, **Android**, **iOS** et **dans le navigateur**, que l'on peut [intégrer à n'importe quel site](#-intégrer-le-jeu-à-un-site). Vous incarnez à la première personne un opérateur armé d'un **HK416** qui protège ses **poussins** contre des vagues de **mercenaires en treillis olive**, le visage peint en vert et les yeux cernés de noir, une **chaîne en métal** autour du cou, qui avancent comme des zombies, bras tendus, bien décidés à tous les enlever.

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
| **3** | **Héberger** / **créer** une partie (jusqu'à 50 joueurs) : réseau local **et** en ligne |
| **4** | **Rejoindre** une partie : **liste des parties en ligne dans le monde entier**, code à 4 chiffres ou adresse IP |

Dans tous les modes à plusieurs :

- Les mercenaires s'en prennent au joueur le plus proche ; ils sont plus nombreux (+50 % par joueur supplémentaire) et plus résistants.
- Plus il y a de joueurs, plus il y a de mercenaires **en même temps** sur le terrain (40 en solo, +2 par joueur, jusqu'à 120) et plus ils arrivent vite : une partie à 50 joueurs devient une vraie bataille.
- Un joueur à 0 PV est **à terre** et revient au début de la vague suivante.
- Game over si tous les joueurs sont à terre ou si tous les poussins sont enlevés.
- Le score est commun, les éliminations sont comptées par joueur. Chaque soldat porte une bande de couleur sur le casque (bleu, orange, vert, violet, puis une couleur différente pour chacun des 50 joueurs). Le classement affiche les 8 meilleurs et votre place.

### Écran partagé

L'écran se coupe en deux, chaque joueur a sa vue, son HUD et son radar. Contrôles du joueur 2 plus bas.

![Coop](docs/coop.png)

### Réseau (plusieurs PC)

1. Sur un PC, appuyez sur **3** : l'écran affiche son **adresse IP** (par ex. `192.168.1.20`).
2. Sur les autres PC (jusqu'à 49), appuyez sur **4**, tapez cette adresse puis **Entrée**.
3. Quand tout le monde est dans la salle d'attente, l'hôte appuie sur **Entrée**.

- Fonctionne entre Windows, macOS et Linux, sur le même réseau (Wi-Fi / box).
- Par Internet : l'hôte doit ouvrir le **port UDP 27016** sur sa box (redirection de port).
- Le pare-feu peut demander l'autorisation au premier lancement : acceptez.
- En réseau, **Échap** ouvre un menu mais **ne met pas la partie en pause** (Q pour quitter).
- Un joueur peut rejoindre une partie déjà commencée.

![Réseau](docs/reseau.png)

### En ligne, dans le monde entier (serveur de parties)

Avec un [serveur de parties](#-serveur-de-parties-liste-mondiale), chaque partie hébergée apparaît **chez tous les joueurs du monde**, quelle que soit leur plateforme : un PC, un téléphone et un navigateur peuvent jouer ensemble.

1. L'hôte appuie sur **3** : en plus de ses adresses IP (réseau local), la salle d'attente affiche le **code en ligne** de la partie (par ex. `4821`).
2. Les autres appuient sur **4** : à gauche, la **liste des parties en ligne** (nom, plateforme de l'hôte, joueurs, vague) ; **un clic** pour rejoindre. On peut aussi taper le code (ou une adresse IP pour le réseau local).
3. Le bouton **VISIBLE PAR TOUS** (ou la touche **V**) de la salle d'attente rend la partie **privée** : elle disparaît de la liste et ne se rejoint plus qu'avec son code.

- Rien à ouvrir sur la box : les joueurs qui rejoignent par la liste ou le code passent par le serveur.
- Les joueurs du même réseau local que l'hôte peuvent toujours le rejoindre directement par son adresse IP.
- Le menu principal indique combien de parties sont ouvertes (« REJOINDRE ... - 3 en ligne »).
- La liste est aussi visible **sans le jeu**, sur la page `/parties` du serveur.

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
- Modes **solo** et **écran partagé**, et **parties en ligne jusqu'à 50 joueurs** :
  - avec le **serveur de parties** : mêmes parties que les PC et les téléphones, dans le monde entier (WebSocket, module `ZWs` de `web/shell.html`) ;
  - sur la **page claude.ai du jeu** : relais temps réel de claude.ai (voir [En ligne](#en-ligne-navigateur-page-claudeai-du-jeu), module `ZNet`).
  Dans les deux cas, `src/netsock_web.c` fait passer le même protocole que la version PC.
- Sans serveur de parties ni claude.ai (fichier ouvert d'un double-clic sans serveur configuré), le menu réseau est masqué : les navigateurs n'autorisent pas l'UDP.
- Paramètres de l'adresse de la page : `?serveur=jeu.exemple.fr` (serveur de parties), `?pseudo=Toto` (nom des parties créées), `?rejoindre=1234` (rejoindre cette partie dès le lancement).

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

## 🌍 Serveur de parties (liste mondiale)

Un petit programme en C, sans dépendance (dossier [`server/`](server/)), à faire tourner sur votre serveur Linux :

- il tient la **liste des parties ouvertes dans le monde entier** ;
- il **relaie** les paquets entre l'hôte et ceux qui rejoignent par la liste ou le code (aucun port à ouvrir chez l'hôte, et le navigateur joue avec les PC) ;
- il sert le **jeu web**, la page publique **`/parties`**, la même liste en JSON (**`/api/parties`**) et le script d'intégration **`/embed.js`**.

Démarrage rapide (paquet `ZeffalieComeBack-serveur-linux-x64.tar.gz` ou `-arm64` de la [page des releases](../../releases/latest)) :

```bash
tar -xzf ZeffalieComeBack-serveur-linux-x64.tar.gz
```

```bash
cd zcb-serveur && ./zcb-server
```

Ouvrez le port **28016 en UDP et TCP**, puis indiquez l'adresse du serveur au jeu : variable **`ZCB_SERVEUR`** du dépôt (*Settings → Secrets and variables → Actions → Variables*, par ex. `jeu.exemple.fr`) ; toutes les versions compilées ensuite l'utilisent. HTTPS avec Caddy, démarrage automatique, options et sécurité : voir **[`server/LISEZMOI.md`](server/LISEZMOI.md)**.

## 🧩 Intégrer le jeu à un site

Une fois le serveur de parties en place (en HTTPS si votre site l'est), une ligne suffit, à l'endroit où le jeu doit apparaître :

```html
<script src="https://jeu.exemple.fr/embed.js" async></script>
```

Le jeu s'affiche en 16/9, s'adapte à la largeur de la page et ne se charge qu'au clic sur **JOUER** (bouton plein écran, manette et son autorisés). Les joueurs de votre site retrouvent ceux du monde entier.

La liste des parties en ligne, mise à jour toutes les 5 secondes (« Rejoindre » lance le jeu de la page) :

```html
<script src="https://jeu.exemple.fr/embed.js" data-mode="parties" async></script>
```

Options (`data-pseudo`, `data-largeur`, `data-cible`...), version sans script (iframe seule) et démonstration : page **`/exemple.html`** du serveur. Le fichier `ZeffalieComeBack-web.html` des releases peut aussi être déposé tel quel sur n'importe quel hébergeur (avec `?serveur=jeu.exemple.fr` si le serveur n'a pas été réglé à la compilation).

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
| `src/netsock.c` | Sockets UDP pour Windows, macOS, Linux et mobiles (joueurs directs + serveur de parties) |
| `src/netsock_web.c` | Réseau du navigateur : WebSocket vers le serveur de parties, ou relais de claude.ai |
| `src/online.c` | Client du serveur de parties : déclarer, lister et rejoindre les parties en ligne, relais des paquets |
| `src/online_proto.h` | Protocole du serveur de parties (partagé par le jeu et le serveur) |
| `server/zcb_server.c` | Serveur de parties : liste mondiale, relais UDP + WebSocket, pages web (voir `server/LISEZMOI.md`) |
| `web/shell.html` | Page du jeu dans le navigateur (chargement, modules réseau `ZWs` et `ZNet`, mode intégré) |
| `web/embed.js` | Script d'intégration à un site (affiche JOUER, liste des parties) |
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

Serveur de parties par défaut (liste mondiale), pour toutes les méthodes de compilation (`make`, CMake, scripts web / Android / iOS) : variable d'environnement `ZCB_SERVEUR` (par ex. `ZCB_SERVEUR=jeu.exemple.fr make`). Le serveur lui-même : `make serveur`, et ses tests : `make test-serveur`.

Pour régénérer l'exe Windows depuis Linux (MinGW + raylib compilée pour Windows) :

```bash
make windows RAYLIB_WIN_PATH=/chemin/vers/raylib-windows
```

### Mobile

- **Android** : `scripts/build_android.sh` (NDK + SDK Android, sans Gradle).
- **iOS** (sur un Mac avec Xcode) : `scripts/build_ios.sh device` ou `simulator`. raylib n'ayant pas de version iOS officielle, le jeu utilise son backend **SDL2**.
- Pour tester l'interface tactile sur PC : `BR_TOUCH=1 ./zeffalie_come_back` (la souris joue le rôle du doigt).

Le workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) compile automatiquement les 6 versions et le serveur de parties (testé) à chaque pull request. Il lance aussi le jeu dans un émulateur Android et un simulateur iOS, dont les captures sont publiées sur la branche `ci-captures`.

Sous Linux, les dépendances de raylib sont : `libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl1-mesa-dev`.
