/*
 * netsock.h - Couche socket UDP minimale et multi-plateforme.
 *
 * Ce module n'inclut volontairement pas raylib : sous Windows, les en-tetes
 * winsock/windows.h entrent en conflit avec raylib.h (Rectangle, DrawText...).
 *
 * Principe :
 *  - Une seule socket UDP est ouverte a la fois (variable statique dans netsock.c).
 *  - UDP = "datagrammes" : chaque envoi part en un seul paquet, sans connexion,
 *    sans garantie d'arrivee ni d'ordre. La fiabilite (quand elle est necessaire)
 *    est geree au-dessus, dans net.c.
 *  - La socket est non bloquante : SockRecv() rend la main immediatement, ce qui
 *    permet de l'appeler a chaque image du jeu sans figer l'affichage.
 */
#ifndef NETSOCK_H
#define NETSOCK_H

#include <stdbool.h>

// Adresse IPv4 + port, stockes dans l'ordre reseau (big-endian, "network byte order").
// On les garde tels quels pour les recopier directement dans une struct sockaddr_in :
// il ne faut donc PAS les afficher ou les comparer a un nombre sans ntohl()/ntohs().
typedef struct {
    unsigned int ip;        // adresse IPv4 sur 32 bits (ex. 192.168.1.10), ordre reseau
    unsigned short port;    // numero de port UDP, ordre reseau
} NetAddr;

// Ouvre la socket UDP et l'attache ("bind") au port donne, sur toutes les interfaces.
// 0 = port choisi par le systeme (cas d'un client, qui n'a pas besoin d'un port fixe).
// Retourne false si le port est deja pris ou si la socket ne peut pas etre creee.
bool SockOpen(unsigned short port);    // 0 = port choisi par le systeme
// Ferme la socket si elle est ouverte (sans effet sinon).
void SockClose(void);
// Envoie len octets de data vers l'adresse 'to'. Retourne true si tout a ete envoye
// (ce qui ne veut pas dire que le paquet arrivera : c'est de l'UDP).
bool SockSend(NetAddr to, const void *data, int len);
// Lit UN paquet en attente (au plus cap octets) et indique son expediteur dans *from.
int SockRecv(void *buf, int cap, NetAddr *from);   // taille recue, 0 si rien
// Convertit un nom d'hote ou une IP texte ("192.168.1.10", "monpc.local") en NetAddr.
// Le port est fourni dans l'ordre "normal" (machine) et converti ici.
bool SockResolve(const char *host, unsigned short port, NetAddr *out);
// Remplit out[] (chaines de 40 caracteres max) avec les IPv4 locales, hors 127.x.x.x,
// pour les afficher a l'hote (les autres joueurs doivent taper l'une d'elles). Retourne le nombre trouve.
int SockLocalIPs(char out[][40], int max);         // adresses IPv4 de cette machine
// Vrai si les deux adresses designent la meme machine ET le meme port.
bool SockAddrEqual(NetAddr a, NetAddr b);

#endif
