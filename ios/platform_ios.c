/*
 * platform_ios.c - Petit complement iOS (compile uniquement pour iOS).
 *
 * Avec le backend SDL, raylib a besoin de la taille de l'ecran avant de
 * creer la fenetre : on la demande a SDL, en paysage.
 */
#include "SDL.h"

void IosScreenSize(int *width, int *height)
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_Init(SDL_INIT_VIDEO);
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) return;
    *width = (mode.w > mode.h) ? mode.w : mode.h;
    *height = (mode.w > mode.h) ? mode.h : mode.w;
}
