/*
 * player.h - Joueurs : controles (clavier/souris, manette, 2e clavier),
 * deplacements, vie, camera et modele vu par les autres joueurs.
 *
 * Un joueur "local" est controle sur cette machine. En reseau, les autres
 * joueurs sont "distants" : leur position arrive par le reseau.
 *
 * Tous les joueurs vivent dans un tableau global fixe (players[]) : un
 * "emplacement" (slot) par joueur possible. L'indice dans le tableau est
 * aussi l'identifiant du joueur (J1 = 0, J2 = 1, ...).
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "weapon.h"

#include "raylib.h"

// Etat des commandes d'un joueur pour une frame, quel que soit le peripherique.
// Il est rempli a chaque frame par PlayersReadInput() a partir du clavier/souris,
// de la manette, du 2e clavier ou de l'ecran tactile ; le reste du jeu ne lit
// QUE cette structure, ce qui le rend independant du peripherique utilise.
typedef struct {
    Vector2 move;          // x = droite, y = avant, longueur <= 1
    Vector2 look;          // rotation demandee cette frame (radians) : x = lacet (yaw), y = tangage (pitch)
    bool fire, firePressed;          // fire = gachette maintenue ; firePressed = vient juste d'etre enfoncee
    bool aim, sprint;                // aim = visee (clic droit / gachette gauche), sprint = course
    bool reload, jump, toggleMode;   // appuis ponctuels (recharger, sauter, mode semi/auto)
    bool switchWeapon;               // HK416 <-> harpon
    int selectWeapon;                // -1, ou arme choisie directement (touches 1 / 2)
} PlayerInput;

// Tout l'etat d'un joueur (le typedef "Player" est declare dans weapon.h).
struct Player {
    int id;                // indice dans players[] (0 = J1, 1 = J2, ...)
    Color color;           // couleur d'identification (bande du casque, brassard, messages)
    bool connected;        // emplacement occupe
    bool local;            // controle sur cette machine
    bool alive;            // false = abattu, attend la vague suivante pour reapparaitre
    float deadT;           // temps ecoule depuis la mort (animation de chute de la camera)

    Vector3 pos, vel;      // pos = pieds ; vel = vitesse en m/s
    float yaw, pitch;      // orientation du regard (radians) : yaw autour de Y, pitch haut/bas
    bool grounded, sprinting;   // au sol ? en train de courir ?
    // hp         : points de vie (0..100)
    // hurtT      : 1 -> 0 apres une blessure (voile rouge a l'ecran)
    // regenDelay : secondes avant que la vie ne commence a remonter
    // bob        : phase du balancement de marche (tete et arme)
    // shake      : intensite du tremblement de camera (tir, blessure)
    float hp, hurtT, regenDelay, bob, shake;

    Weapon w;              // armes du joueur (HK416 + harpon)
    PlayerInput in;        // commandes de la frame en cours
    bool prevFire;         // etat de "fire" a la frame precedente (pour calculer firePressed)
    Camera3D cam;          // camera a la premiere personne de ce joueur

    int kills, headshots;  // statistiques affichees en fin de partie
    float hitT;            // hitmarker : temps d'affichage restant de la croix de touche
    bool hitKill, hitHead; // le dernier impact a tue / etait a la tete (couleur du hitmarker)

    // Reseau
    Vector3 netPos;        // position cible (lissage des joueurs distants)
    float netYaw, netPitch;     // orientation cible recue par le reseau
    // Les compteurs "Grant"/"Seen" fonctionnent par cumul : l'hote augmente
    // le total, le client compare au total deja applique et ajoute la
    // difference. Ainsi un paquet perdu ou duplique ne fausse rien.
    int ammoGrant;         // munitions donnees par l'hote (cumul)
    int ammoGrantSeen;     // ... deja ajoutees cote client
    int harpoonGrant, harpoonGrantSeen;   // idem pour les fleches de harpon
    int respawnCount, respawnSeen;        // idem pour les reapparitions (le client se replace)
};

extern Player players[MAX_PLAYERS];
extern int numPlayers;     // emplacements utilises (1, 2, ou MAX_PLAYERS en reseau)

// Creation / gestion des emplacements
void PlayersReset(int count);          // solo / ecran partage
void PlayersResetNet(int localId);     // reseau : seul l'emplacement local est occupe
void PlayersRestart(void);             // nouvelle partie, memes joueurs
void PlayerJoin(int id);               // un joueur distant arrive (hote)
void PlayerLeave(int id);              // un joueur distant se deconnecte

// Mise a jour par frame
void PlayersReadInput(float dt);                // remplit p->in pour chaque joueur local
void PlayerUpdate(Player *p, float dt);         // joueur local
void PlayerUpdateRemote(Player *p, float dt);   // joueur distant (lissage)
void PlayerUpdateCamera(Player *p, float dt);   // place p->cam (yeux, balancement, FOV)
void PlayerHurt(Player *p, float dmg);          // inflige des degats (peut tuer le joueur)
void PlayerRespawn(Player *p);                  // fait revenir un joueur mort
void PlayerRefillWeapon(Player *p);             // recharge l'arme a un minimum garanti
void PlayerGiveAmmo(Player *p, int amount);     // munitions HK416 en plus (ramassage)
void PlayerGiveHarpoons(Player *p, int amount); // fleches de harpon en plus
void PlayerDrawBody(const Player *p, bool far); // modele 3D du soldat (vu par les autres) ; far = simplifie

// Requetes utilitaires
Vector3 PlayerEye(const Player *p);       // position des yeux dans le monde
Vector3 PlayerForward(const Player *p);   // recul inclus
const char *PlayerTag(const Player *p);   // "" en solo, "J1 " ... en multi
int PlayersAlive(void);                   // nombre de joueurs connectes et vivants
int PlayersConnected(void);               // nombre de joueurs connectes
Player *NearestAlivePlayer(Vector3 pos, float *dist);   // plus proche au sol (XZ) ; NULL si aucun
Player *FirstLocalPlayer(void);           // premier joueur controle sur cette machine

#endif
