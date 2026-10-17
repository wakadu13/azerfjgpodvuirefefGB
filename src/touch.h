/*
 * touch.h - Controles tactiles en jeu (mobile).
 *
 *  - moitie gauche : joystick virtuel (il apparait sous le doigt) ;
 *    pousse a fond vers l'avant = sprint ;
 *  - moitie droite : glisser pour viser ;
 *  - boutons : FEU (on peut aussi viser en glissant dessus), VISER
 *    (bascule), SAUT, RECHARGER, changement d'arme, pause.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include "player.h"

void TouchReset(void);
void TouchUpdate(void);                      // une fois par frame, avant la lecture des commandes
void TouchApplyInput(PlayerInput *in);
bool TouchPausePressed(void);
void TouchDraw(const Player *p);             // en unites virtuelles (UiBegin)

#endif
