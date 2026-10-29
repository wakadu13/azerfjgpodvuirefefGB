/*
 * netsock.h - Couche reseau minimale et multi-plateforme, utilisee par net.c.
 *
 * Ce module n'inclut volontairement pas raylib : sous Windows, les en-tetes
 * winsock/windows.h entrent en conflit avec raylib.h (Rectangle, DrawText...).
 *
 * Trois facons de faire passer les paquets du jeu, derriere la meme interface :
 *  - DIRECT (PC, mobiles, netsock.c) : sockets UDP, hote et clients se parlent
 *    directement (reseau local, ou port 27016 ouvert chez l'hote) ;
 *  - SERVEUR (partout, online.c) : le serveur de parties fait suivre les
 *    paquets ; on rejoint une partie par son code a 4 chiffres ou dans la liste
 *    mondiale. Un hote PC accepte a la fois les joueurs DIRECTS et RELAYES ;
 *  - SALON (navigateur sur la page claude.ai, netsock_web.c) : relais "room"
 *    de claude.ai.
 *
 * Principe (le meme que l'UDP dans les trois cas) :
 *  - Une seule socket est ouverte a la fois (variable statique dans netsock.c).
 *  - Datagrammes : chaque envoi part en un seul paquet, sans garantie d'arrivee
 *    ni d'ordre. La fiabilite (quand elle est necessaire) est geree au-dessus, dans net.c.
 *  - Non bloquant : SockRecv() rend la main immediatement, ce qui permet de
 *    l'appeler a chaque image du jeu sans figer l'affichage.
 */
#ifndef NETSOCK_H
#define NETSOCK_H

#include <stdbool.h>

// Version du protocole du jeu (paquets de net.c), a incrementer a chaque changement de format.
// Elle est aussi annoncee au serveur de parties : la liste signale les parties incompatibles.
#define NET_VERSION 4

// Adresse d'un autre joueur.
//  - Direct : IPv4 + port UDP, stockes dans l'ordre reseau (big-endian), pour les recopier
//    tels quels dans une struct sockaddr_in : ne PAS les afficher ou les comparer a un
//    nombre sans ntohl()/ntohs().
//  - Relaye par le serveur de parties : relay = numero du joueur + 1 (1 = l'hote), ip = port = 0.
//  - Salon claude.ai : ip = numero du joueur dans le salon, port = 0.
typedef struct {
    unsigned int ip;        // adresse IPv4 sur 32 bits (ex. 192.168.1.10), ordre reseau
    unsigned short port;    // numero de port UDP, ordre reseau
    unsigned short relay;   // 0 = pas relaye ; sinon joueur relaye par le serveur de parties
} NetAddr;

// Ouvre la "socket" du jeu.
//  port != 0 : hote. Ecoute sur ce port (PC, mobiles) et declare la partie au serveur de
//              parties s'il y en a un (elle apparait dans la liste mondiale).
//  port == 0 : client. Si le dernier SockResolve() a recu un code de partie, le client
//              s'inscrit aupres du serveur de parties ; sinon socket UDP sur un port libre.
// Retourne false si le port est deja pris ou si le reseau n'est pas disponible.
bool SockOpen(unsigned short port);
// Ferme la socket si elle est ouverte (quitte aussi la partie sur le serveur de parties).
void SockClose(void);
// Envoie len octets de data vers 'to'. Retourne true si c'est parti
// (ce qui ne veut pas dire que le paquet arrivera : c'est de l'UDP).
bool SockSend(NetAddr to, const void *data, int len);
// Envoie le meme paquet a n adresses (l'etat du monde, de l'hote vers tous les clients).
// Les joueurs relayes le recoivent par UN seul envoi au serveur, qui le diffuse.
bool SockSendMany(const NetAddr *to, int n, const void *data, int len);
// Lit UN paquet du jeu en attente (au plus cap octets) et indique son expediteur dans *from.
int SockRecv(void *buf, int cap, NetAddr *from);   // taille recue, 0 si rien
// Convertit ce que le joueur a tape en adresse :
//  - "192.168.1.10", "monpc.local" : adresse directe (le port est donne en ordre machine) ;
//  - un code de partie a 4 chiffres : partie du serveur de parties (ou du salon claude.ai).
bool SockResolve(const char *host, unsigned short port, NetAddr *out);
// Remplit out[] (chaines de 40 caracteres max) avec ce que l'hote affiche aux autres
// joueurs : ses adresses IPv4 locales (PC), ou le code de la partie (navigateur).
int SockLocalIPs(char out[][40], int max);
// Vrai si les deux adresses designent le meme joueur.
bool SockAddrEqual(NetAddr a, NetAddr b);
// Le reseau est-il possible ici ? Toujours sur PC / mobile ; dans le navigateur,
// seulement sur la page claude.ai ou si le serveur de parties est joignable.
bool SockAvailable(void);

// Lien utilise par la partie en cours (net.c en deduit le rythme d'envoi)
typedef enum { LINK_DIRECT, LINK_SERVER, LINK_ROOM } SockLink;
SockLink SockLinkKind(void);
// Joueur relaye parti (prevenu par le serveur) : a traiter comme un BYE de sa part
bool SockPeerGone(NetAddr *who);
// Client relaye : raison de l'echec de la connexion au serveur de parties (vide si tout va bien)
const char *SockLinkError(void);

//----------------------------------------------------------------------------------
// Liste des parties en ligne (serveur de parties, ou salon claude.ai)
//----------------------------------------------------------------------------------
typedef struct {
    int code;            // code a 4 chiffres a rejoindre
    int players, maxPlayers, wave;
    int version;         // NET_VERSION de l'hote (0 = inconnue)
    int platform;        // plateforme de l'hote (ZP_xxx de online_proto.h)
    bool playing;        // partie lancee (sinon : salle d'attente)
    char name[48];       // "Partie de <pseudo>" (ASCII seulement)
} SockGame;

bool SockOnlineConfigured(void);    // un serveur de parties est configure
// A appeler a chaque image. listEvery = intervalle de rafraichissement de la liste en
// secondes (0 = elle n'est pas affichee) ; drain = aucune partie en cours (personne
// d'autre ne lit la socket).
void SockUpdate(float dt, float listEvery, bool drain);
// Remplit out[] avec les parties ouvertes (au plus max) ; renvoie leur nombre
int SockListGames(SockGame *out, int max);
// Nombre de parties visibles dans le monde (-1 = inconnu) et de joueurs en ligne
int SockOnlineTotals(int *players);
// Hote : met a jour son annonce (appele regulierement)
void SockAnnounce(int players, int maxPlayers, int wave, bool playing);
// Hote : code de la partie sur le serveur de parties (0 = pas (encore) en ligne)
int SockOnlineCode(void);
// Hote : etat du serveur de parties en une ligne (vide si pas de serveur)
const char *SockOnlineInfo(void);
// Hote : partie visible dans la liste mondiale (sinon : seulement par son code)
void SockSetListed(bool on);
bool SockListed(void);
// Partie a rejoindre des le lancement (0 = aucune) : parametre ?rejoindre=1234 de la
// page dans le navigateur (liens de la page /parties du serveur), variable
// d'environnement ZCB_REJOINDRE sur PC
int SockAutoJoinCode(void);

#endif
