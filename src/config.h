/*
 * config.h - Constantes de reglage du jeu et petites macros communes.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "raylib.h"

// Monde
#define ARENA          30.0f   // demi-largeur de l'arene (metres)
#define MAX_BOXES      64

// Entites
#define MAX_PLAYERS    4       // 2 en ecran partage, jusqu'a 4 en reseau
#define MAX_ENEMIES    64
#define MAX_PARTICLES  2048
#define MAX_TRACERS    64
#define MAX_PICKUPS    32
#define MAX_MSGS       6

// Reseau
#define NET_PORT           27016   // port UDP de l'hote
#define NET_SNAPSHOT_RATE  30.0f   // etats du monde envoyes par l'hote, par seconde
#define NET_STATE_RATE     60.0f   // etats des joueurs envoyes par les clients, par seconde
#define NET_TIMEOUT        6.0f    // secondes sans nouvelles avant deconnexion

// Burgers du stand
#define MAX_BURGERS    12
#define START_BURGERS  12

// Joueur
#define EYE_HEIGHT     1.65f
#define PLAYER_RADIUS  0.35f
#define WALK_SPEED     5.5f
#define SPRINT_SPEED   8.5f
#define JUMP_SPEED     6.0f
#define GRAVITY        16.0f
#define MOUSE_SENS     0.0022f   // radians par pixel
#define PAD_LOOK_SPEED 3.2f      // radians par seconde, stick au maximum
#define KEY_LOOK_SPEED 2.4f      // radians par seconde, fleches (J2 au clavier)

// HK416
#define MAG_SIZE       30
#define START_RESERVE  180
#define MAX_RESERVE    300
#define FIRE_DELAY     (60.0f/850.0f)   // ~850 coups/min
#define RELOAD_TIME    2.2f
#define RELOAD_EMPTY   2.75f
#define BULLET_DAMAGE  30.0f
#define HEADSHOT_MULT  3.0f

// Harpon : tue et projette le glouton touche en un coup
#define HARPOON_START      6       // harpons en reserve au depart (+1 charge)
#define HARPOON_MAX        12
#define HARPOON_RELOAD     1.3f
#define HARPOON_SPEED      55.0f   // m/s
#define HARPOON_GRAVITY    3.0f
#define HARPOON_LAUNCH     16.0f   // vitesse horizontale donnee au glouton harponne
#define HARPOON_LIFT       8.0f    // ... et vers le haut
#define HARPOON_BOWL_DMG   60.0f   // degats aux gloutons percutes en vol
#define SWITCH_TIME        0.35f   // changement d'arme

// Camera
#define FOV_HIP        75.0f
#define FOV_ADS        52.0f
#define FOV_SPRINT     82.0f

#define V3(x, y, z) ((Vector3){ (x), (y), (z) })
#define NOROT V3(0, 0, 0)

#endif
