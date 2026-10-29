/*
 * online_proto.h - Protocole du serveur de parties (liste mondiale + relais).
 *
 * Partage par le jeu (src/online.c) et par le serveur (server/zcb_server.c) :
 * ce fichier n'inclut rien, il ne contient que des constantes.
 *
 * Le serveur de parties sert a deux choses :
 *  1. LISTE : chaque hote y declare sa partie ; tout le monde (PC, telephone,
 *     navigateur, page web /parties du serveur) voit les parties ouvertes dans
 *     le monde entier ;
 *  2. RELAIS : les joueurs qui rejoignent une partie par son code passent par
 *     le serveur, qui fait suivre les paquets du jeu (net.c) entre l'hote et
 *     eux. Aucun port a ouvrir sur la box de l'hote, et un navigateur (qui ne
 *     sait pas faire d'UDP) peut jouer avec un PC.
 *
 * Transport : UDP (port 28016) pour les versions PC / mobiles, WebSocket
 * (ws://serveur:28016/ws, ou wss:// derriere un proxy HTTPS) pour le
 * navigateur. Les messages sont les memes dans les deux cas : un datagramme
 * UDP = un message WebSocket binaire.
 *
 * Format : tous les nombres en little-endian.
 *   en-tete (5 octets) : u32 ZCB_MAGIC, u8 type
 *   chaine "str"       : u8 longueur puis les caracteres (sans '\0')
 *
 * Client -> serveur
 *   ZM_LIST    u8 proto, puis des zeros jusqu'a ZCB_LIST_MIN octets au total
 *              (la reponse est plus grosse que la demande : sans ce
 *              remplissage, le serveur pourrait servir a amplifier une attaque)
 *   ZM_HOST    u8 proto, u32 cle, u8 version du jeu, u8 joueurs max,
 *              u8 plateforme, u8 drapeaux (ZG_LISTED), str nom
 *              -> ZM_HOSTED (ou ZM_ERROR). La cle, tiree au hasard, rend la
 *              demande repetable sans creer deux parties (pertes UDP).
 *   ZM_STATUS  u32 jeton, u8 joueurs, u8 max, u16 vague, u8 drapeaux
 *              (ZG_PLAYING, ZG_LISTED) : toutes les secondes -> ZM_HOSTED
 *              (accuse de reception) ou ZM_ERROR ZE_UNKNOWN (serveur
 *              redemarre : l'hote se declare de nouveau)
 *   ZM_JOIN    u8 proto, u32 cle, u16 code -> ZM_JOINED ou ZM_ERROR
 *   ZM_DATA    u32 jeton, u16 pair, u8 drapeaux, paquet du jeu
 *              (hote : pair = destinataire, 0 = tous les clients ;
 *               client : pair ignore, c'est toujours pour l'hote)
 *   ZM_LEAVE   u32 jeton : quitter la partie (hote : la fermer)
 *
 * Serveur -> client
 *   ZM_GAMES   u16 parties, u16 joueurs (totaux sur le serveur), u8 n,
 *              puis n fois : u16 code, u8 version, u8 joueurs, u8 max,
 *              u16 vague, u8 drapeaux, u8 plateforme, str nom
 *   ZM_HOSTED  u32 cle, u16 code, u32 jeton
 *   ZM_JOINED  u32 cle, u16 code, u16 pair, u32 jeton
 *   ZM_ERROR   u32 cle (0 si sans objet), u8 erreur (ZE_xxx), str message
 *   ZM_GONE    u16 pair : un joueur est parti (0 = l'hote : partie terminee)
 *   ZM_DATA    u16 pair (expediteur, 0 = l'hote), u8 drapeaux, paquet du jeu
 *
 * Le jeton (u32 aleatoire donne par le serveur) accompagne chaque message
 * d'un hote ou d'un client inscrit : un tiers qui usurperait son adresse IP
 * ne peut pas le deviner. Dans une partie, l'hote est le pair 0 et les
 * clients recoivent les numeros 1, 2, 3...
 */
#ifndef ONLINE_PROTO_H
#define ONLINE_PROTO_H

#define ZCB_SERVER_PORT  28016        // port par defaut du serveur (UDP et TCP)
#define ZCB_MAGIC        0x4D42435Au  // "ZCBM" (octets 'Z' 'C' 'B' 'M' en little-endian)
#define ZCB_PROTO        1            // version de ce protocole
#define ZCB_LIST_MIN     400          // taille minimale d'une demande de liste
#define ZCB_LIST_MAX     1200         // taille maximale d'une reponse (tient dans un datagramme)
#define ZCB_MSG_MAX      17000        // plus gros message accepte (paquet du jeu de 16 Ko + en-tete)
#define ZCB_NAME_MAX     31           // longueur maximale du nom d'une partie

// Types de messages (octet 4)
enum {
    ZM_LIST = 1, ZM_HOST, ZM_STATUS, ZM_JOIN, ZM_DATA, ZM_LEAVE,
    ZM_GAMES = 0x81, ZM_HOSTED, ZM_JOINED, ZM_ERROR, ZM_GONE
};

// Codes d'erreur de ZM_ERROR
enum {
    ZE_NOGAME = 1,   // aucune partie avec ce code (ou elle vient de se terminer)
    ZE_FULL,         // partie pleine
    ZE_LIMIT,        // trop de parties sur le serveur (ou depuis cette adresse)
    ZE_UNKNOWN,      // jeton inconnu (serveur redemarre, delai depasse)
    ZE_PROTO         // version du protocole differente
};

// Drapeaux d'une partie (ZM_HOST, ZM_STATUS, ZM_GAMES)
#define ZG_PLAYING  1    // partie lancee (sinon : salle d'attente)
#define ZG_LISTED   2    // visible dans la liste (sinon : accessible par son code seulement)

// Plateforme de l'hote (affichee dans la liste)
enum { ZP_UNKNOWN, ZP_WINDOWS, ZP_MACOS, ZP_LINUX, ZP_ANDROID, ZP_IOS, ZP_WEB };

#endif
