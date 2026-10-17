/*
 * lobby.h - Liste des parties en ligne pour les versions installees (PC, Mac,
 * Linux, Android, iOS) : toutes les parties hebergees, meme hors du reseau local.
 *
 * Principe :
 *  - un serveur MQTT sur Internet (adresse et identifiants dans config.h) sert
 *    de tableau d'affichage : chaque hote y annonce sa partie, l'ecran
 *    REJOINDRE lit la liste. Tres peu de donnees passent par lui ;
 *  - la partie elle-meme se joue en direct entre les machines (UDP, comme en
 *    reseau local). Pour traverser les box, chacun demande son adresse
 *    publique a un serveur STUN, puis l'hote "perce" sa box vers le joueur
 *    qui veut entrer ("hole punching") : les deux box laissent alors passer
 *    les paquets du jeu.
 * Si le serveur n'est pas configure (LOBBY_HOST vide), la liste reste vide
 * et le jeu fonctionne comme avant (adresse IP).
 * Version navigateur : non utilise (la page claude.ai a sa propre liste).
 */
#ifndef LOBBY_H
#define LOBBY_H

#include "netsock.h"

#include <stdbool.h>

// Une partie annoncee sur le serveur
typedef struct {
    char id[12];             // identifiant (8 caracteres hexadecimaux, tire au hasard par l'hote)
    char name[40];           // nom affiche ("Partie de ...")
    int players, maxPlayers, wave;
    bool playing;            // lancee (sinon : salle d'attente)
    NetAddr pub;             // adresse publique de l'hote (Internet), ip = 0 si inconnue
    NetAddr lan;             // adresse de l'hote dans son reseau local
} LobbyGame;

bool LobbyConfigured(void);                 // un serveur est-il configure ?
void LobbyUpdate(float dt);                 // a chaque image (connexion, annonces, perçage)
void LobbyWatch(bool on);                   // ecran REJOINDRE ouvert : suivre la liste
int LobbyGames(LobbyGame *out, int max);    // parties connues (triees par nom), renvoie leur nombre
const char *LobbyStatus(void);              // etat lisible ("Connexion au serveur...", "" si tout va bien)

// Hote (appele par net.c une fois la socket UDP ouverte)
void LobbyHostStart(void);
void LobbyHostInfo(int players, int maxPlayers, int wave, bool playing);
void LobbyHostStop(void);
const char *LobbyHostState(void);           // texte pour la salle d'attente

// Joueur qui rejoint une partie de la liste (socket UDP deja ouverte)
void LobbyJoinStart(const char *gameId);
void LobbyJoinStop(void);

// Paquet UDP recu : true s'il etait pour la liste en ligne (reponse STUN, perçage)
bool LobbyOnUdp(const unsigned char *data, int len, NetAddr from);

#endif
