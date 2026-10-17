/*
 * ui.h - Echelle de l'interface et boutons cliquables / tactiles.
 *
 * L'interface est dessinee dans un espace "virtuel" de 720 px de haut sur
 * PC (540 px sur mobile, pour des boutons assez gros pour les doigts), puis
 * agrandie a la taille reelle de l'ecran.
 *
 * Exemple : sur un ecran de 1440 px de haut, l'echelle vaut 2 sur PC ; un
 * bouton de 40 unites virtuelles fera donc 80 vrais pixels.
 */
#ifndef UI_H
#define UI_H

#include "raylib.h"

bool TouchMode(void);        // controles tactiles (mobile, ou BR_TOUCH=1 sur PC pour tester)

float UiScale(void);         // facteur pixels reels / unites virtuelles
int UiWidth(void);           // taille de l'ecran en unites virtuelles
int UiHeight(void);
Vector2 UiMouse(void);       // souris / doigt en unites virtuelles
void UiBegin(void);          // dessin en unites virtuelles
void UiEnd(void);            // fin du dessin en unites virtuelles (retour aux pixels reels)

// Bouton : dessin, et "relache dessus" (clic ou doigt) pour l'activer
void UiDrawButton(Rectangle r, const char *label, Color accent);
void UiUpdate(void);   // a appeler une fois par image (suivi des doigts pour les boutons)
bool UiButtonHit(Rectangle r);

#endif
