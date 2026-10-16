# 🍔 BURGER RUSH : Opération HK416

FPS en **C** avec **raylib**. Vous incarnez à la première personne un opérateur armé d'un **HK416** qui défend son stand de burgers contre des vagues de gloutons obèses bien décidés à tout dévorer.

![Aperçu](docs/apercu.png)

Tout est généré par le code (modèles 3D, sons, textures) : **aucun fichier externe**, un seul fichier source (`src/main.c`).

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

Sous Linux, les dépendances de raylib sont : `libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl1-mesa-dev`.
