/*
 * netsock.h - Couche socket UDP minimale et multi-plateforme.
 *
 * Ce module n'inclut volontairement pas raylib : sous Windows, les en-tetes
 * winsock/windows.h entrent en conflit avec raylib.h (Rectangle, DrawText...).
 */
#ifndef NETSOCK_H
#define NETSOCK_H

#include <stdbool.h>

// Adresse IPv4 + port, stockes dans l'ordre reseau.
typedef struct {
    unsigned int ip;
    unsigned short port;
} NetAddr;

bool SockOpen(unsigned short port);    // 0 = port choisi par le systeme
void SockClose(void);
bool SockSend(NetAddr to, const void *data, int len);
int SockRecv(void *buf, int cap, NetAddr *from);   // taille recue, 0 si rien
bool SockResolve(const char *host, unsigned short port, NetAddr *out);
int SockLocalIPs(char out[][40], int max);         // adresses IPv4 de cette machine
bool SockAddrEqual(NetAddr a, NetAddr b);

#endif
