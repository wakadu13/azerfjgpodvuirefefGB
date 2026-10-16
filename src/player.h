/*
 * player.h - Joueurs : controles (clavier/souris, manette, 2e clavier),
 * deplacements, vie, camera et modele vu par l'autre joueur en coop.
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
};

extern Player players[MAX_PLAYERS];
extern int numPlayers;

void PlayersReset(int count);
void PlayersReadInput(float dt);
void PlayerUpdate(Player *p, float dt);
void PlayerUpdateCamera(Player *p, float dt);
void PlayerHurt(Player *p, float dmg);
void PlayerRespawn(Player *p);
void PlayerDrawBody(const Player *p);

Vector3 PlayerEye(const Player *p);
Vector3 PlayerForward(const Player *p);   // recul inclus
const char *PlayerTag(const Player *p);   // "" en solo, "J1 " / "J2 " en coop
int PlayersAlive(void);
Player *NearestAlivePlayer(Vector3 pos, float *dist);

#endif
