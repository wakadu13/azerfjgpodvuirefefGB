/*
 * net.h - Multijoueur en reseau (UDP), jusqu'a 4 joueurs.
 *
 * Modele hote / clients :
 *  - L'hote fait tourner la partie (gloutons, vagues, bonus, vie des joueurs)
 *    et envoie l'etat du monde aux clients 30 fois par seconde.
 *  - Chaque client deplace et vise lui-meme son joueur (aucune latence
 *    ressentie), detecte ses tirs localement et envoie a l'hote sa position
 *    et ses touches sur les gloutons.
 *  - Les effets visibles par tous (traçantes, ketchup, sons...) sont
 *    enregistres puis rejoues sur les autres machines.
 */
#ifndef NET_H
#define NET_H

#include "raylib.h"

typedef enum { NET_OFF, NET_HOST, NET_CLIENT } NetRole;
typedef enum { NET_IDLE, NET_CONNECTING, NET_CONNECTED, NET_FAILED, NET_LOST } NetStatus;

typedef enum { EV_TRACER = 1, EV_SAUCE, EV_SPARKS, EV_CRUMBS, EV_SOUND } NetEventType;

bool NetHost(void);                    // ouvre le port NET_PORT
bool NetJoin(const char *address);     // "ip" ou "ip:port"
void NetStop(void);                    // previent les autres et ferme

void NetReceive(float dt);             // debut de frame : traite les paquets recus
void NetSend(float dt);                // fin de frame : envoie l'etat

NetRole NetGetRole(void);
NetStatus NetGetStatus(void);
const char *NetError(void);
int NetLocalId(void);
bool NetIsClient(void);
bool NetIsAuthority(void);             // solo, ecran partage ou hote

// Enregistrement d'evenements a rejouer sur les autres machines
void NetRecordEvent(NetEventType type, Vector3 a, Vector3 b, int param, float f1, float f2);
void NetRecordFeed(Color c, const char *text);
void NetReportHit(int enemy, float dmg, bool head, Vector3 pos, Vector3 dir);
void NetSuppress(bool on);             // effets purement locaux (non retransmis)

#endif
