/*
 * hud.h - Interface : HUD de chaque joueur, infos communes, fil
 * d'actualite et ecrans (titre, pause, game over).
 */
#ifndef HUD_H
#define HUD_H

#include "player.h"

#include "raylib.h"

void FeedClear(void);
void FeedAdd(Color c, const char *fmt, ...);
void FeedUpdate(float dt);

// HUD personnel, dessine dans la vue (w x h) du joueur.
void HudDrawPlayer(const Player *p, int w, int h, bool split);
// Infos communes (burgers, vague, score, annonces) sur tout l'ecran.
void HudDrawShared(int w, int h, bool split);

void HudDrawTitle(int w, int h);
void HudDrawPause(int w, int h);
void HudDrawGameOver(int w, int h);

#endif
