/*
 * player.h - Joueurs : controles (clavier/souris, manette, 2e clavier),
 * deplacements, vie, camera et modele vu par les autres joueurs.
 *
 * Un joueur "local" est controle sur cette machine. En reseau, les autres
 * joueurs sont "distants" : leur position arrive par le reseau.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "weapon.h"

#include "raylib.h"

// Etat des commandes d'un joueur pour une frame, quel que soit le peripherique.
typedef struct {
    Vector2 move;          // x = droite, y = avant, longueur <= 1
    Vector2 look;          // rotation demandee cette frame (radians)
    bool fire, firePressed;
    bool aim, sprint;
    bool reload, jump, toggleMode;   // appuis ponctuels
} PlayerInput;

struct Player {
    int id;
    Color color;
    bool connected;        // emplacement occupe
    bool local;            // controle sur cette machine
    bool alive;
    float deadT;

    Vector3 pos, vel;      // pos = pieds
    float yaw, pitch;
    bool grounded, sprinting;
    float hp, hurtT, regenDelay, bob, shake;

    Weapon w;
    PlayerInput in;
    bool prevFire;
    Camera3D cam;

    int kills, headshots;
    float hitT;            // hitmarker
    bool hitKill, hitHead;

    // Reseau
    Vector3 netPos;        // position cible (lissage des joueurs distants)
    float netYaw, netPitch;
    int ammoGrant;         // munitions donnees par l'hote (cumul)
    int ammoGrantSeen;     // ... deja ajoutees cote client
    int respawnCount, respawnSeen;
};

extern Player players[MAX_PLAYERS];
extern int numPlayers;     // emplacements utilises (1, 2, ou MAX_PLAYERS en reseau)

void PlayersReset(int count);          // solo / ecran partage
void PlayersResetNet(int localId);     // reseau : seul l'emplacement local est occupe
void PlayersRestart(void);             // nouvelle partie, memes joueurs
void PlayerJoin(int id);               // un joueur distant arrive (hote)
void PlayerLeave(int id);

void PlayersReadInput(float dt);
void PlayerUpdate(Player *p, float dt);         // joueur local
void PlayerUpdateRemote(Player *p, float dt);   // joueur distant (lissage)
void PlayerUpdateCamera(Player *p, float dt);
void PlayerHurt(Player *p, float dmg);
void PlayerRespawn(Player *p);
void PlayerRefillWeapon(Player *p);
void PlayerGiveAmmo(Player *p, int amount);
void PlayerDrawBody(const Player *p);

Vector3 PlayerEye(const Player *p);
Vector3 PlayerForward(const Player *p);   // recul inclus
const char *PlayerTag(const Player *p);   // "" en solo, "J1 " ... en multi
int PlayersAlive(void);
int PlayersConnected(void);
Player *NearestAlivePlayer(Vector3 pos, float *dist);
Player *FirstLocalPlayer(void);

#endif
