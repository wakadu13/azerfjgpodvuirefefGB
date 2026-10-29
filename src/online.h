/*
 * online.h - Client du serveur de parties (liste mondiale + relais).
 *
 * Protocole : voir online_proto.h. Ce module ne touche a aucune socket : il
 * fabrique et lit les messages, et tient l'etat (partie declaree, partie
 * rejointe, liste des parties). Le transport est fourni par la couche
 * reseau de la plateforme :
 *  - netsock.c (PC, mobiles) : datagrammes UDP vers le serveur ;
 *  - netsock_web.c (navigateur) : messages WebSocket.
 * Comme netsock.h, ce fichier n'inclut pas raylib (conflits avec winsock).
 *
 * Numerotation des joueurs relayes (champ "relay" de NetAddr) : numero dans
 * la partie + 1, donc 1 = l'hote, 2, 3... = les clients (0 = pas relaye).
 */
#ifndef ONLINE_H
#define ONLINE_H

#include "netsock.h"

#include <stdbool.h>

// Adresse du serveur de parties utilisee par defaut ("nom" ou "nom:port").
// Vide = pas de serveur. Se regle a la compilation (-DONLINE_SERVER=\"jeu.exemple.fr\",
// ou variable ZCB_SERVEUR du depot pour les versions compilees par GitHub Actions)
// et peut etre remplacee au lancement (variable d'environnement ZCB_SERVEUR sur PC,
// parametre ?serveur= dans l'adresse de la page du navigateur).
#ifndef ONLINE_SERVER
#define ONLINE_SERVER ""
#endif

// Transport, fourni par netsock.c ou netsock_web.c
bool OnlineTransportSend(const unsigned char *data, int len);   // un message vers le serveur
bool OnlineTransportReady(void);                                 // serveur joignable (adresse connue / WebSocket ouverte)

typedef enum {
    OL_IDLE,           // rien
    OL_HOST_WAIT,      // hote : declaration de la partie en cours
    OL_HOSTING,        // hote : partie declaree (code attribue)
    OL_HOST_REFUSED,   // hote : le serveur a refuse (trop de parties...) ; le reseau local marche quand meme
    OL_JOINING,        // client : demande envoyee
    OL_JOINED,         // client : inscrit dans la partie, les paquets passent par le serveur
    OL_FAILED          // client : echec (code inconnu, serveur injoignable...) -> OnlineError()
} OnlineState;

// Identite affichee dans la liste : plateforme (ZP_xxx) et pseudo (peut etre vide)
void OnlineSetIdentity(int platform, const char *pseudo);

void OnlineHost(int maxPlayers);    // declarer une partie (hote)
void OnlineJoin(int code);          // rejoindre une partie par son code (client)
void OnlineLeave(void);             // quitter / fermer la partie, retour a OL_IDLE
void OnlineLinkLost(void);          // navigateur : la WebSocket s'est fermee

// A chaque image : repetitions, nouvelles de l'hote, demandes de liste
// (listEvery = intervalle entre deux demandes en secondes, 0 = la liste n'est pas affichee)
void OnlineTick(float dt, float listEvery);
// Message recu du serveur. S'il contient un paquet du jeu, il est copie dans out
// (cap octets au plus) et sa taille est renvoyee, *relay = joueur expediteur ; sinon 0.
int OnlineReceive(const unsigned char *msg, int len, unsigned char *out, int cap, unsigned short *relay);
bool OnlineSendTo(unsigned short relay, const void *data, int len);   // vers un joueur relaye
bool OnlineSendAll(const void *data, int len);                        // hote : vers tous les clients relayes
bool OnlinePopGone(unsigned short *relay);                            // joueur relaye parti (une fois chacun)

OnlineState OnlineGetState(void);
int OnlineCode(void);               // code de la partie (0 si aucun)
const char *OnlineError(void);      // dernier probleme (vide si aucun)
bool OnlineServerSeen(void);        // le serveur a repondu recemment

// Hote : etat de la partie, envoye au serveur (seulement s'il change, ou chaque seconde)
void OnlineStatus(int players, int maxPlayers, int wave, bool playing);
void OnlineSetListed(bool on);      // partie visible dans la liste (sinon : par son code seulement)
bool OnlineListed(void);

// Liste des parties (rafraichie pendant OnlineTick(.., listEvery > 0))
int OnlineGames(SockGame *out, int max);
int OnlineTotals(int *players);     // nombre de parties visibles (-1 = inconnu) et de joueurs en ligne

#endif
