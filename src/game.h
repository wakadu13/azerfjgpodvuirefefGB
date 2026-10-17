/*
 * game.h - Etat global de la partie, boucle de mise a jour et rendu.
 *
 * Toute la partie en cours est decrite par une seule variable globale G
 * (de type Game), accessible depuis tous les modules. En reseau, une
 * grande partie de G est recopiee de l'hote vers les clients (voir net.c).
 */
#ifndef GAME_H
#define GAME_H

#include "raylib.h"

// Etats de la machine a etats du jeu (un seul actif a la fois).
// Transitions principales :
//   ST_TITLE --solo/split--> ST_PLAY <--> ST_PAUSE
//   ST_TITLE --heberger--> ST_LOBBY --lancer--> ST_PLAY
//   ST_TITLE --rejoindre--> ST_JOIN --connecte--> ST_LOBBY (etat recu de l'hote)
//   ST_PLAY --plus de poussins--> ST_OVER --rejouer--> ST_PLAY
//   n'importe quel etat --quitter / connexion perdue--> ST_TITLE
typedef enum {
    ST_TITLE,    // menu principal
    ST_JOIN,     // saisie de l'adresse de l'hote / connexion en cours
    ST_LOBBY,    // salle d'attente d'une partie reseau
    ST_PLAY,     // partie en cours
    ST_PAUSE,    // pause (solo et ecran partage uniquement)
    ST_OVER      // game over : resume de la partie, choix rejouer / menu
} GameState;

// Facon de jouer choisie au menu principal
typedef enum {
    MODE_SOLO,    // un seul joueur local, plein ecran
    MODE_SPLIT,   // deux joueurs sur la meme machine, ecran coupe en deux
    MODE_NET      // multijoueur en reseau (un joueur par machine)
} GameMode;

// Etat global de la partie (une seule instance : G)
typedef struct {
    GameState state;         // etat courant de la machine a etats
    GameMode mode;           // solo, ecran partage ou reseau
    bool quit;               // passe a true pour fermer le jeu (teste par la boucle de main.c)
    float time, titleT;      // time : temps total ecoule (s) ; titleT : temps passe au menu (camera tournante)
    int round;               // incremente a chaque (re)demarrage, synchronise en reseau

    // Vagues (gerees par waves.c)
    int wave, toSpawn, alive;          // numero de vague, ennemis restant a faire apparaitre, ennemis en vie
    bool bossPending, inIntermission;  // chef de guerre pas encore apparu ; pause entre deux vagues
    float spawnT, interT, bannerT;     // minuteries : prochaine apparition, fin de l'entracte, banniere "VAGUE N"

    // Partie
    int score, best, chicks;  // score courant, meilleur score de la session, poussins restants
    char overReason[96];     // raison du game over affichee a l'ecran de fin
    char titleMsg[96];       // message affiche au menu (ex : connexion perdue)
} Game;

extern Game G;   // definie dans game.c

void GameInit(void);                 // a appeler une fois apres la creation de la fenetre
void GameClose(void);                // libere les ressources (reseau, modeles, textures)
void GameStart(GameMode mode);       // lance (ou relance) une partie dans le mode donne
void GameOver(const char *reason);   // termine la partie en cours ; reason est affichee
void GameUpdate(float dt);           // une frame de logique (dt en secondes)
void GameDraw(void);                 // une frame de rendu (3D + interface)

// Appeles par le module reseau cote client
void GameNetStateChanged(GameState previous);   // G.state vient d'etre modifie par un paquet de l'hote
void GameNetRoundStarted(void);                 // l'hote a lance une nouvelle manche (G.round a change)

#endif
