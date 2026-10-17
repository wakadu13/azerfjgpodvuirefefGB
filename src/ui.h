/*
 * ui.h - Echelle de l'interface et boutons cliquables / tactiles.
 *
 * L'interface est dessinee dans un espace "virtuel" de 720 px de haut sur
 * PC (540 px sur mobile, pour des boutons assez gros pour les doigts), puis
 * agrandie a la taille reelle de l'ecran.
 */
#ifndef UI_H
#define UI_H

#include "raylib.h"

bool TouchMode(void);        // controles tactiles (mobile, ou BR_TOUCH=1 sur PC pour tester)

float UiScale(void);
int UiWidth(void);           // taille de l'ecran en unites virtuelles
int UiHeight(void);
Vector2 UiMouse(void);       // souris / doigt en unites virtuelles
void UiBegin(void);          // dessin en unites virtuelles
void UiEnd(void);

// Bouton : dessin, et "relache dessus" (clic ou doigt) pour l'activer
void UiDrawButton(Rectangle r, const char *label, Color accent);
bool UiButtonHit(Rectangle r);

#endif
