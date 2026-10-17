/*
 * game.h - Etat global de la partie, boucle de mise a jour et rendu.
 */
#ifndef GAME_H
#define GAME_H

#include "raylib.h"

typedef enum {
    ST_TITLE,    // menu principal
    ST_JOIN,     // saisie de l'adresse de l'hote / connexion en cours
    ST_LOBBY,    // salle d'attente d'une partie reseau
    ST_PLAY,
    ST_PAUSE,    // pause (solo et ecran partage uniquement)
    ST_OVER
} GameState;

typedef enum { MODE_SOLO, MODE_SPLIT, MODE_NET } GameMode;

typedef struct {
    GameState state;
    GameMode mode;
    bool quit;
    float time, titleT;
    int round;               // incremente a chaque (re)demarrage, synchronise en reseau

    // Vagues
    int wave, toSpawn, alive;
    bool bossPending, inIntermission;
    float spawnT, interT, bannerT;

    // Partie
    int score, best, burgers;
    char overReason[96];
    char titleMsg[96];       // message affiche au menu (ex : connexion perdue)
} Game;

extern Game G;

void GameInit(void);
void GameClose(void);
void GameStart(GameMode mode);
void GameOver(const char *reason);
void GameUpdate(float dt);
void GameDraw(void);

// Appeles par le module reseau cote client
void GameNetStateChanged(GameState previous);
void GameNetRoundStarted(void);

#endif
