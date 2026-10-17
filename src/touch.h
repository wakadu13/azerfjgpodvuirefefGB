/*
 * touch.h - Controles tactiles en jeu (mobile).
 *
 *  - moitie gauche : joystick virtuel (il apparait sous le doigt) ;
 *    pousse a fond vers l'avant = sprint ;
 *  - moitie droite : glisser pour viser ;
 *  - boutons : FEU (on peut aussi viser en glissant dessus), VISER
 *    (bascule), SAUT, RECHARGER, changement d'arme, pause.
 *
 * Chaque doigt recoit un "role" au moment ou il se pose (selon l'endroit
 * touche) et le garde jusqu'a ce qu'il soit leve : on peut donc bouger,
 * viser et tirer en meme temps avec plusieurs doigts.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include "player.h"

void TouchReset(void);                       // oublie tous les doigts et desactive la visee (debut de partie)
void TouchUpdate(void);                      // une fois par frame, avant la lecture des commandes
void TouchApplyInput(PlayerInput *in);       // ajoute les commandes tactiles a celles du joueur
bool TouchPausePressed(void);                // vrai si le bouton pause vient d'etre touche
void TouchDraw(const Player *p);             // en unites virtuelles (UiBegin)

#endif
