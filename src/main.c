/*
 * BURGER RUSH : Operation HK416
 * FPS en C avec raylib : un operateur arme d'un HK416 defend son stand de
 * burgers contre des vagues de gloutons affames. Solo, ecran partage ou
 * reseau (jusqu'a 4 joueurs). PC, Mac, Linux, Android et iOS.
 *
 * Organisation du code :
 *   main.c     point d'entree, fenetre et boucle principale
 *   game.c     etats du jeu (titre, lobby, partie, pause, game over), ecran partage
 *   net.c      multijoueur reseau : protocole hote / clients
 *   netsock.c  sockets UDP (Windows / Linux / macOS)
 *   player.c   commandes, deplacement, vie et camera des joueurs
 *   weapon.c   HK416 : tir, recul, rechargement, modele 3D
 *   enemy.c    gloutons : IA, degats, modele 3D
 *   waves.c    enchainement des vagues
 *   pickup.c   bonus (munitions, salade)
 *   map.c      arene, stand de burgers, collisions
 *   effects.c  particules et tracantes
 *   harpoon.c  fleches du harpon
 *   hud.c      interface 2D et ecrans
 *   ui.c       echelle de l'interface et boutons
 *   touch.c    controles tactiles (mobile)
 *   render.c   shader et primitives 3D
 *   audio.c    sons generes par le code
 *   util.c     petites fonctions utilitaires
 */
#include "game.h"
#include "render.h"
#include "audio.h"

#include "raylib.h"

#include <math.h>

#if defined(BR_IOS)
    #include "SDL_main.h"   // sur iOS, SDL renomme main() et demarre l'application UIKit
    void IosScreenSize(int *width, int *height);   // ios/platform_ios.c
#endif

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
#if defined(BR_MOBILE)
    // Telephone : plein ecran a la taille de l'ecran
    int width = 0, height = 0;
  #if defined(BR_IOS)
    IosScreenSize(&width, &height);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_FULLSCREEN_MODE);
  #endif
    // Android : pas de MSAA, beaucoup d'appareils (et l'emulateur) refusent alors de creer le contexte EGL
    InitWindow(width, height, "Burger Rush");
    SetTargetFPS(60);
#else
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "BURGER RUSH - Operation HK416");
    SetTargetFPS(144);
#endif
    SetExitKey(KEY_NULL);   // Echap (ou Retour sur Android) sert a la pause

    AudioInit();
    RenderInit();
    GameInit();

    while (!WindowShouldClose() && !G.quit)
    {
        float dt = fminf(GetFrameTime(), 0.05f);
        GameUpdate(dt);
        GameDraw();
    }

    GameClose();
    RenderClose();
    AudioClose();
    CloseWindow();
    return 0;
}
