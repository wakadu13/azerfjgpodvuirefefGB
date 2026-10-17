/*
 * net.h - Multijoueur en reseau (UDP), jusqu'a 4 joueurs.
 *
 * Modele hote / clients :
 *  - L'hote fait tourner la partie (mercenaires, vagues, bonus, vie des joueurs)
 *    et envoie l'etat du monde aux clients 30 fois par seconde.
 *  - Chaque client deplace et vise lui-meme son joueur (aucune latence
 *    ressentie), detecte ses tirs localement et envoie a l'hote sa position
 *    et ses touches sur les mercenaires.
 *  - Les effets visibles par tous (tracantes, impacts, plumes, sons...) sont
 *    enregistres puis rejoues sur les autres machines.
 *
 * L'hote est toujours le joueur 0 ; les clients recoivent les numeros 1 a 3.
 * Utilisation type dans la boucle du jeu :
 *     NetReceive(dt);   // au debut de la frame : applique ce qui est arrive
 *     ... mise a jour du jeu ...
 *     NetSend(dt);      // a la fin de la frame : envoie (au rythme voulu)
 */
#ifndef NET_H
#define NET_H

#include "raylib.h"

// Role de cette machine : hors ligne (solo / ecran partage), hote ou client
typedef enum { NET_OFF, NET_HOST, NET_CLIENT } NetRole;
// Etat de la connexion :
//  NET_IDLE       rien en cours
//  NET_CONNECTING client qui attend la reponse (WELCOME) de l'hote
//  NET_CONNECTED  partie en reseau active (l'hote est directement dans cet etat)
//  NET_FAILED     echec de connexion (adresse invalide, refus, pas de reponse) -> voir NetError()
//  NET_LOST       connexion perdue en cours de partie (hote parti ou silencieux)
typedef enum { NET_IDLE, NET_CONNECTING, NET_CONNECTED, NET_FAILED, NET_LOST } NetStatus;

// Types d'effets retransmis aux autres machines (la valeur 0 est evitee volontairement).
//  EV_TRACER   trainee de balle de a vers b
//  EV_IMPACT   impact sur un mercenaire en a, direction b, param = nombre de particules
//  EV_SPARKS   etincelles sur le decor en a, normale b
//  EV_FEATHERS plumes en a
//  EV_SOUND    son : param = SndId, f1 = hauteur (pitch), f2 = volume
//  EV_HARPOON  fleche de harpon d'un autre joueur : position a, vitesse b
typedef enum { EV_TRACER = 1, EV_IMPACT, EV_SPARKS, EV_FEATHERS, EV_SOUND, EV_HARPOON } NetEventType;
// Nature d'une touche signalee par un client : balle de fusil ou harpon
typedef enum { HIT_BULLET, HIT_HARPOON } NetHitKind;

bool NetSupported(void);              // reseau possible ici (navigateur : seulement sur claude.ai)
bool NetHost(void);                    // ouvre le port NET_PORT
bool NetJoin(const char *address);     // "ip" ou "ip:port"
void NetStop(void);                    // previent les autres et ferme

void NetReceive(float dt);             // debut de frame : traite les paquets recus
void NetSend(float dt);                // fin de frame : envoie l'etat

NetRole NetGetRole(void);              // role actuel (NET_OFF si hors ligne)
NetStatus NetGetStatus(void);          // etat de la connexion
const char *NetError(void);            // dernier message d'erreur lisible (vide si aucun)
int NetLocalId(void);                  // numero du joueur local (0 = hote, -1 = pas encore attribue)
bool NetIsClient(void);                // vrai si on est client (le monde est pilote par l'hote)
bool NetIsAuthority(void);             // solo, ecran partage ou hote

// Enregistrement d'evenements a rejouer sur les autres machines
// (appele par les modules d'effets/sons : ne fait rien hors ligne ou pendant NetSuppress(true)).
void NetRecordEvent(NetEventType type, Vector3 a, Vector3 b, int param, float f1, float f2);
// Hote uniquement : ajoute un message au fil d'actualite (kills...) partage avec les clients
void NetRecordFeed(Color c, const char *text);
// Client uniquement : signale a l'hote qu'on a touche le mercenaire 'enemy' (envoi fiable)
void NetReportHit(int enemy, float dmg, bool head, Vector3 pos, Vector3 dir, NetHitKind kind);
void NetSuppress(bool on);             // effets purement locaux (non retransmis)

#endif
