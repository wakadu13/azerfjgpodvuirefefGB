/*
 * platform_ios.c - Petit complement iOS (compile uniquement pour iOS).
 *
 * Avec le backend SDL, raylib a besoin de la taille de l'ecran avant de
 * creer la fenetre : on la demande a SDL, en paysage.
 */
#include "SDL.h"

// Remplit *width / *height avec la taille de l'ecran en mode paysage
// (largeur = plus grande dimension). En cas d'erreur, les valeurs ne sont pas
// modifiees : l'appelant garde ses valeurs par defaut.
void IosScreenSize(int *width, int *height)
{
    // Autorise seulement les deux orientations paysage (bouton a gauche ou a droite)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_Init(SDL_INIT_VIDEO);   // necessaire pour interroger l'ecran
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) return;   // ecran 0 = ecran principal
    // L'appareil peut repondre en portrait : on met toujours le plus grand cote en largeur
    *width = (mode.w > mode.h) ? mode.w : mode.h;
    *height = (mode.w > mode.h) ? mode.h : mode.w;
}
