/*
 * BURGER RUSH : Operation HK416
 * FPS en C avec raylib : un operateur arme d'un HK416 defend son stand de
 * burgers contre des vagues de gloutons affames. Solo, ecran partage ou
 * reseau (jusqu'a 4 PC).
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
 *   hud.c      interface 2D et ecrans
 *   render.c   shader et primitives 3D
 *   audio.c    sons generes par le code
 *   util.c     petites fonctions utilitaires
 */
#include "game.h"
#include "render.h"
#include "audio.h"

#include "raylib.h"

#include <math.h>

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "BURGER RUSH - Operation HK416");
    SetExitKey(KEY_NULL);   // Echap sert a la pause
    SetTargetFPS(144);

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
