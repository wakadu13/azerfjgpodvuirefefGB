# 🌍 Serveur de parties de Zeffalie Come Back

Un petit programme à faire tourner sur votre serveur pour que **tout le monde voie les parties ouvertes dans le monde entier** et puisse les rejoindre d'un clic, depuis un PC, un téléphone ou un navigateur.

Il fait trois choses :

- **la liste des parties** : chaque hôte y déclare sa partie ; le jeu (écran *Rejoindre*), la page web `/parties` et votre site l'affichent ;
- **le relais** : les joueurs qui rejoignent une partie par son code passent par le serveur. L'hôte n'a **aucun port à ouvrir** sur sa box, et un navigateur peut jouer avec un PC ou un téléphone ;
- **le site** : le jeu dans le navigateur (`/`), la liste des parties (`/parties`, `/api/parties` en JSON) et le script pour l'intégrer à un autre site (`/embed.js`, mode d'emploi sur `/exemple.html`).

Aucune dépendance : un seul exécutable, pas de base de données, aucun compte. Rien n'est enregistré à part le journal (codes et noms des parties, sans adresses IP).

## Installation (Linux)

1. Téléchargez le paquet sur la [page des releases](../../../releases/latest) :
   - `ZeffalieComeBack-serveur-linux-x64.tar.gz` (la plupart des serveurs),
   - `ZeffalieComeBack-serveur-linux-arm64.tar.gz` (Raspberry Pi, serveurs ARM).
2. Décompressez-le et lancez le serveur :

   ```bash
   tar -xzf ZeffalieComeBack-serveur-linux-x64.tar.gz
   cd zcb-serveur
   ./zcb-server
   ```

3. Ouvrez le port **28016 en UDP et en TCP** dans le pare-feu (et chez l'hébergeur s'il en a un) :

   ```bash
   sudo ufw allow 28016
   ```

4. Vérifiez depuis un autre appareil : `http://ADRESSE-DU-SERVEUR:28016/parties` (liste) et `http://ADRESSE-DU-SERVEUR:28016/` (le jeu).

Le paquet contient `zcb-server`, le dossier `www/` (le jeu `index.html`, `embed.js`, `exemple.html`), ce fichier et `zcb-server.service`.

## Brancher le jeu sur votre serveur

Le jeu doit connaître l'adresse du serveur (un nom comme `jeu.exemple.fr`, ou une adresse IP) :

- **Toutes les versions compilées par GitHub** (Windows, macOS, Linux, Android, iOS, navigateur) : dans le dépôt, *Settings → Secrets and variables → Actions → Variables*, créez la variable `ZCB_SERVEUR` avec l'adresse du serveur (`jeu.exemple.fr`, ou `jeu.exemple.fr:PORT` si vous avez changé le port). Les versions compilées ensuite l'utilisent.
- **Le jeu servi par le serveur** (`http(s)://votre-serveur/`) le trouve tout seul.
- **Sans recompiler** : sur PC, variable d'environnement `ZCB_SERVEUR=jeu.exemple.fr` ; dans le navigateur, ajoutez `?serveur=jeu.exemple.fr` à l'adresse de la page.

## HTTPS (obligatoire pour intégrer le jeu à un site en https)

Une page https n'a pas le droit d'ouvrir une connexion non chiffrée : il faut un nom de domaine qui pointe vers le serveur et un proxy HTTPS devant `zcb-server`. Le plus simple est [Caddy](https://caddyserver.com), qui obtient et renouvelle le certificat tout seul. Fichier `/etc/caddy/Caddyfile` :

```
jeu.exemple.fr {
	encode gzip
	reverse_proxy 127.0.0.1:28016
}
```

Ensuite `https://jeu.exemple.fr/` sert le jeu et les navigateurs utilisent `wss://jeu.exemple.fr/ws` automatiquement. Les PC et téléphones continuent de passer par le port **UDP 28016**, qui doit rester ouvert ; le port TCP 28016 peut alors être fermé au public.

Avec nginx, le bloc équivalent :

```nginx
location / {
    proxy_pass http://127.0.0.1:28016;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_read_timeout 120s;
}
```

Derrière un proxy installé sur la même machine, le serveur lit la vraie adresse des joueurs dans `X-Forwarded-For` (pour les limites par adresse IP). Si le proxy est sur une autre machine, ajoutez l'option `--proxy`.

## Lancer le serveur au démarrage (systemd)

```bash
sudo cp -r zcb-serveur /opt/
sudo cp /opt/zcb-serveur/zcb-server.service /etc/systemd/system/
sudo systemctl enable --now zcb-server
```

Journal en direct :

```bash
journalctl -u zcb-server -f
```

## Intégrer le jeu à un site

Ouvrez `https://jeu.exemple.fr/exemple.html` : la page montre le jeu et la liste intégrés, avec le code à copier. En résumé, une ligne suffit :

```html
<script src="https://jeu.exemple.fr/embed.js" async></script>
```

Et pour la liste des parties en ligne :

```html
<script src="https://jeu.exemple.fr/embed.js" data-mode="parties" async></script>
```

## Options

| Option | Rôle |
|---|---|
| `--port N` | Port UDP (jeu PC / mobile) et TCP (HTTP + WebSocket), 28016 par défaut |
| `--www DOSSIER` | Fichiers servis : `index.html` (le jeu), `embed.js`... (`www` par défaut) |
| `--max-parties N` | Parties simultanées au plus (500 par défaut) |
| `--parties-par-ip N` | Parties par adresse IP au plus (10 par défaut) |
| `--proxy` | Croire `X-Forwarded-For` même si le proxy n'est pas sur cette machine |

## Bande passante

Les parties rejointes par leur code passent par le serveur, qui renvoie l'état du monde à chaque joueur 20 fois par seconde. Ordre de grandeur de ce que le serveur envoie : **2 à 3 Mbit/s pour une partie à 8 joueurs**, jusqu'à une cinquantaine de Mbit/s pour une partie pleine à 50. Les joueurs du même réseau local que l'hôte, eux, lui parlent directement, sans passer par le serveur.

## Sécurité

- Chaque joueur reçoit un jeton secret ; un paquet qui ne le porte pas, ou qui vient d'une autre adresse, est ignoré.
- Limites par adresse IP (parties créées, connexions, demandes par seconde) et par joueur (paquets relayés par seconde).
- La demande de liste en UDP doit être aussi grosse que la réponse : le serveur ne peut pas servir à amplifier une attaque.
- Les noms de parties ne gardent que les caractères ASCII imprimables, et les pages les affichent comme du texte.

## Compiler soi-même (Linux, macOS, BSD)

```bash
make serveur          # ou : cc -O2 -o zcb-server server/zcb_server.c
make test-serveur     # tests : serveur + couche réseau du jeu (python3 nécessaire)
```

Le protocole est décrit dans [`src/online_proto.h`](../src/online_proto.h).
