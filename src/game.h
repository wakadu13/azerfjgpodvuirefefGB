/*
 * game.h - Etat global de la partie, boucle de mise a jour et ecran partage.
 */
#ifndef GAME_H
#define GAME_H

#include "raylib.h"

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER } GameState;

typedef struct {
    GameState state;
    bool quit;
    float time, titleT;

    // Vagues
    int wave, toSpawn, alive;
    bool bossPending, inIntermission;
    float spawnT, interT, bannerT;

    // Partie
    int score, best, burgers;
    char overReason[96];
} Game;

extern Game G;

void GameInit(void);
void GameClose(void);
void GameStart(int playerCount);
void GameOver(const char *reason);
void GameUpdate(float dt);
void GameDraw(void);

#endif
