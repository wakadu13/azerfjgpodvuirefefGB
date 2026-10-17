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
// Infos communes (poussins, vague, score, annonces) sur tout l'ecran.
void HudDrawShared(int w, int h, bool split);

// Ecrans (dessines en unites virtuelles, voir ui.h). Les rectangles des
// boutons servent a la fois au dessin et a la detection des clics.
typedef enum { TITLE_SOLO, TITLE_SPLIT, TITLE_HOST, TITLE_JOIN } TitleChoice;
int HudTitleChoices(TitleChoice *out);
Rectangle HudTitleButton(int i);
Rectangle HudPauseButton(int i);    // 0 reprendre, 1 quitter
Rectangle HudOverButton(int i);     // 0 rejouer, 1 menu
Rectangle HudLobbyButton(int i);    // 0 lancer (hote), 1 retour
Rectangle HudJoinButton(int i);     // 0 se connecter, 1 retour
Rectangle HudJoinKey(int i);        // pave numerique (12 touches)
const char *HudJoinKeyLabel(int i);

void HudDrawTitle(int w, int h);
void HudDrawPause(int w, int h, bool network);
void HudDrawGameOver(int w, int h);
void HudDrawJoin(int w, int h, const char *address);
void HudDrawLobby(int w, int h);

#endif
