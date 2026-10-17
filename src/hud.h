/*
 * hud.h - Interface : HUD de chaque joueur, infos communes, fil
 * d'actualite et ecrans (titre, pause, game over).
 */
#ifndef HUD_H
#define HUD_H

#include "player.h"

#include "raylib.h"

void FeedClear(void);
void FeedAdd(Color c, const char *fmt, ...);        // visible par tous (retransmis par l'hote)
void FeedAddLocal(Color c, const char *fmt, ...);   // seulement sur cette machine
void FeedPush(Color c, const char *text);           // message deja formate, non retransmis
void FeedUpdate(float dt);

// HUD personnel, dessine dans la vue (w x h) du joueur.
void HudDrawPlayer(const Player *p, int w, int h, bool showName);
// Infos communes (burgers, vague, score, annonces) sur tout l'ecran.
void HudDrawShared(int w, int h, bool split);

void HudDrawTitle(int w, int h);
void HudDrawPause(int w, int h, bool network);
void HudDrawGameOver(int w, int h);
void HudDrawJoin(int w, int h, const char *address);
void HudDrawLobby(int w, int h);

#endif
