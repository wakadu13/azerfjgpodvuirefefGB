# 🍔 BURGER RUSH : Opération HK416

FPS en **C** avec **raylib**, jouable **seul**, **à deux sur le même PC** (écran partagé) ou **jusqu'à 4 en réseau** (plusieurs PC). Disponible pour **Windows**, **macOS (Apple Silicon)** et **Linux**. Vous incarnez à la première personne un opérateur armé d'un **HK416** qui défend son stand de burgers contre des vagues de gloutons obèses bien décidés à tout dévorer.

![Aperçu](docs/apercu.png)

Tout est généré par le code (modèles 3D, sons, textures) : **aucun fichier externe**.

## 🎮 Le jeu

- Au centre de l'arène se trouve un **stand de burgers** (12 au départ).
- Les gloutons entrent par les **4 portes** de l'arène et foncent vers le comptoir. Chaque burger mangé les rend **plus gros et plus résistants**.
- Si vous vous approchez trop d'eux, ils vous attaquent.
- **Game over** si vous mourez **ou** si tous les burgers sont mangés.
- Entre deux vagues : +2 burgers, +90 munitions, +25 PV.
- Les ennemis abattus lâchent parfois des **caisses de munitions** ou une **salade** (soin, ils n'en veulent pas).

### Les ennemis

| Ennemi | Description |
|---|---|
| **Le Gourmand** | L'ennemi de base. |
| **Le Pressé** | Plus petit, rapide, et il vous fonce dessus. Apparaît dès la vague 2. |
| **Le Glouton** | Énorme, lent, très résistant. Dès la vague 3. |
| **LE ROI DU GRAS** | Boss couronné toutes les 5 vagues. |

### Le HK416

- Chargeur de 30 coups, ~850 coups/min, mode **auto / semi-auto**.
- Viseur **point rouge** (clic droit) : zoom, précision quasi parfaite.
- Recul, dispersion selon le mouvement, flash du canon qui éclaire la scène, traçantes.
- Rechargement animé (chargeur qui sort/rentre), plus long à vide (culasse).
- **Headshot = dégâts x3.**

## 👥 Multijoueur

Au menu :

| Touche | Mode |
|---|---|
| **1** | Solo |
| **2** | Coop **écran partagé** : 2 joueurs sur le même PC |
| **3** | **Héberger** une partie réseau (jusqu'à 4 PC) |
| **4** | **Rejoindre** une partie réseau |

Dans tous les modes à plusieurs :

- Les gloutons s'en prennent au joueur le plus proche ; ils sont plus nombreux (+50 % par joueur supplémentaire) et plus résistants.
- Un joueur à 0 PV est **à terre** et revient au début de la vague suivante.
- Game over si tous les joueurs sont à terre ou si tous les burgers sont mangés.
- Le score est commun, les éliminations sont comptées par joueur. Chaque soldat porte une bande de couleur sur le casque (bleu, orange, vert, violet).

### Écran partagé

L'écran se coupe en deux, chaque joueur a sa vue, son HUD et son radar. Contrôles du joueur 2 plus bas.

![Coop](docs/coop.png)

### Réseau (plusieurs PC)

1. Sur un PC, appuyez sur **3** : l'écran affiche son **adresse IP** (par ex. `192.168.1.20`).
2. Sur les autres PC (3 maximum), appuyez sur **4**, tapez cette adresse puis **Entrée**.
3. Quand tout le monde est dans la salle d'attente, l'hôte appuie sur **Entrée**.

- Fonctionne entre Windows, macOS et Linux, sur le même réseau (Wi-Fi / box).
- Par Internet : l'hôte doit ouvrir le **port UDP 27016** sur sa box (redirection de port).
- Le pare-feu peut demander l'autorisation au premier lancement : acceptez.
- En réseau, **Échap** ouvre un menu mais **ne met pas la partie en pause** (Q pour quitter).
- Un joueur peut rejoindre une partie déjà commencée.

![Réseau](docs/reseau.png)

## ⌨️ Contrôles

| Touche | Action |
|---|---|
| ZQSD / WASD / flèches | Se déplacer (AZERTY et QWERTY supportés) |
| Souris | Viser |
| Clic gauche | Tirer |
| Clic droit | Viseur point rouge |
| R | Recharger |
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
| Sauter | A / Croix | O |
| Sprint | L3 | H |
| Mode de tir | Y / Triangle | N |
| Pause | Start | Échap / P |

Avec deux manettes, la deuxième peut aussi contrôler le joueur 1.

## ⬇️ Télécharger

### Windows

**[`release/BurgerRush.exe`](release/BurgerRush.exe)** : autonome (aucune DLL à installer), double-cliquez dessus.

> Windows SmartScreen peut afficher un avertissement car l'exe n'est pas signé : « Informations complémentaires » → « Exécuter quand même ».

### macOS (Apple Silicon : M1, M2, M3, M4…)

L'application est compilée automatiquement sur un Mac par GitHub Actions :

- après fusion sur `main` : **[page des releases → `BurgerRush-macOS-arm64.zip`](../../releases/latest)** ;
- sinon : onglet **Actions** → dernier run « Build » → section *Artifacts* → `BurgerRush-macOS-arm64`.

Dézippez puis lancez **BurgerRush.app**. L'application n'étant pas signée par un développeur Apple, macOS la bloque la première fois :

- **clic droit** sur `BurgerRush.app` → **Ouvrir** → **Ouvrir** ;
- ou, si macOS dit qu'elle est « endommagée », dans le Terminal : `xattr -dr com.apple.quarantine ~/Downloads/BurgerRush.app`

### Linux

Même endroit que pour macOS : `BurgerRush-linux-x64.tar.gz` (ou compilez, voir plus bas).

## 🗂️ Organisation du code

| Fichier | Rôle |
|---|---|
| `src/main.c` | Point d'entrée, fenêtre, boucle principale |
| `src/game.c` | États du jeu (titre, salle d'attente, partie, pause, game over) et écran partagé |
| `src/net.c` | Multijoueur réseau : l'hôte simule la partie et envoie l'état du monde, chaque client gère son joueur et envoie ses tirs |
| `src/netsock.c` | Sockets UDP pour Windows, macOS et Linux |
| `src/player.c` | Commandes (clavier/souris, manette, 2ᵉ clavier), déplacements, vie, caméra, modèle du coéquipier |
| `src/weapon.c` | HK416 : tir, recul, rechargement, modèle 3D et mains |
| `src/enemy.c` | Gloutons : IA, dégâts, modèle 3D |
| `src/waves.c` | Enchaînement des vagues et récompenses |
| `src/pickup.c` | Bonus (munitions, salade) |
| `src/map.c` | Arène, stand de burgers, collisions |
| `src/effects.c` | Particules et traçantes |
| `src/hud.c` | Interface 2D et écrans |
| `src/render.c` | Shader et primitives 3D (cube, sphère, cylindre) |
| `src/audio.c` | Sons générés par le code |
| `src/config.h` | Toutes les constantes de réglage (vitesses, dégâts, munitions…) |

## 🔧 Compilation

Nécessite **raylib 5.5**.

### Avec CMake (le plus simple, télécharge raylib automatiquement si besoin)

```bash
cmake -B build
cmake --build build
./build/burger_rush
```

### Avec make (raylib déjà installée)

```bash
make
./burger_rush
```

Si raylib est compilée depuis les sources dans un dossier :

```bash
make RAYLIB_PATH=/chemin/vers/raylib
```

Pour régénérer l'exe Windows depuis Linux (MinGW + raylib compilée pour Windows) :

```bash
make windows RAYLIB_WIN_PATH=/chemin/vers/raylib-windows
```

Le workflow [`.github/workflows/build.yml`](.github/workflows/build.yml) compile automatiquement les versions macOS ARM, Windows et Linux à chaque pull request.

Sous Linux, les dépendances de raylib sont : `libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl1-mesa-dev`.
