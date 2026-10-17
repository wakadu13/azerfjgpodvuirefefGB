/*
 * BURGER RUSH : Operation HK416
 * FPS en C avec raylib : un operateur arme d'un HK416 defend ses poussins
 * contre des vagues de mercenaires enchaines. Solo, ecran partage ou
 * reseau (jusqu'a 4 joueurs). PC, Mac, Linux, Android et iOS.
 *
 * Organisation du code :
 *   main.c     point d'entree, fenetre et boucle principale
 *   game.c     etats du jeu (titre, lobby, partie, pause, game over), ecran partage
 *   net.c      multijoueur reseau : protocole hote / clients
 *   netsock.c  sockets UDP (Windows / Linux / macOS)
 *   player.c   commandes, deplacement, vie et camera des joueurs
 *   weapon.c   HK416 : tir, recul, rechargement, modele 3D
 *   enemy.c    mercenaires : apparition, IA, degats
 *   enemymodel.c  modele 3D des mercenaires (treillis, grimage, chaines)
 *   waves.c    enchainement des vagues
 *   pickup.c   bonus (munitions, salade)
 *   map.c      arene, stand des poussins, collisions
 *   effects.c  particules et tracantes
 *   harpoon.c  fleches du harpon
 *   hud.c      interface 2D et ecrans
 *   ui.c       echelle de l'interface et boutons
 *   touch.c    controles tactiles (mobile)
 *   render.c   shader et primitives 3D
 *   audio.c    sons generes par le code
 *   util.c     petites fonctions utilitaires
 *   config.h   constantes de reglage du jeu (tailles, vitesses, limites...)
 *
 * Deroulement general d'une frame (voir la boucle dans main() ci-dessous) :
 *   1. GameUpdate(dt) : lit les entrees, fait avancer la simulation et le reseau ;
 *   2. GameDraw()     : dessine la scene 3D puis l'interface 2D par-dessus.
 */
#include "game.h"     // etat global G, GameInit / GameUpdate / GameDraw...
#include "render.h"   // RenderInit / RenderClose (shader d'eclairage)
#include "audio.h"    // AudioInit / AudioClose (sons synthetises)

#include "raylib.h"   // fenetre, entrees, dessin

#include <math.h>     // fminf

#if defined(BR_IOS)
    #include "SDL_main.h"   // sur iOS, SDL renomme main() et demarre l'application UIKit
    void IosScreenSize(int *width, int *height);   // ios/platform_ios.c
#endif

// Point d'entree du programme : cree la fenetre, initialise les modules,
// fait tourner la boucle principale puis libere tout a la fermeture.
// argc / argv ne sont pas utilises (aucune option en ligne de commande).
int main(int argc, char *argv[])
{
    (void)argc;   // evite l'avertissement "parametre inutilise"
    (void)argv;
#if defined(BR_MOBILE)
    // Telephone : plein ecran a la taille de l'ecran
    // (0 x 0 = raylib prend la taille native de l'ecran sur Android)
    int width = 0, height = 0;
  #if defined(BR_IOS)
    // iOS : on demande explicitement la taille de l'ecran a la plateforme
    IosScreenSize(&width, &height);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_FULLSCREEN_MODE);
  #endif
    // Android : pas de MSAA, beaucoup d'appareils (et l'emulateur) refusent alors de creer le contexte EGL
    InitWindow(width, height, "Burger Rush");
    SetTargetFPS(60);   // 60 images/s sur mobile pour menager la batterie
#else
    // PC / Mac / Linux : fenetre redimensionnable, anticrenelage 4x (MSAA)
    // et synchronisation verticale pour eviter les dechirures d'image
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "BURGER RUSH - Operation HK416");
    SetTargetFPS(144);  // plafond eleve : les ecrans rapides restent fluides
#endif
    SetExitKey(KEY_NULL);   // Echap (ou Retour sur Android) sert a la pause

    // Initialisation des modules (l'ordre compte : le jeu utilise le rendu et le son)
    AudioInit();
    RenderInit();
    GameInit();

    // Boucle principale : tourne tant que la fenetre est ouverte et que le
    // joueur n'a pas demande a quitter depuis le menu (G.quit)
    while (!WindowShouldClose() && !G.quit)
    {
        // Duree de la frame en secondes, plafonnee a 50 ms : apres un gel
        // (chargement, fenetre deplacee...) la simulation ne fait pas un
        // saut enorme qui ferait traverser les murs ou rater des collisions
        float dt = fminf(GetFrameTime(), 0.05f);
        GameUpdate(dt);
        GameDraw();
    }

    // Liberation dans l'ordre inverse de l'initialisation
    GameClose();
    RenderClose();
    AudioClose();
    CloseWindow();
    return 0;
}
