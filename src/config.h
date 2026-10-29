/*
 * config.h - Constantes de reglage du jeu et petites macros communes.
 *
 * Toutes les valeurs "magiques" du gameplay sont regroupees ici pour pouvoir
 * equilibrer le jeu sans fouiller dans le code. Unites utilisees partout :
 *   - distances en metres (1 unite raylib = 1 metre),
 *   - temps en secondes,
 *   - vitesses en metres par seconde, accelerations en m/s^2,
 *   - angles en radians (sauf les FOV de la camera, en degres comme raylib).
 * Les valeurs suffixees par 'f' sont des float (evite des conversions double).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "raylib.h"

// Monde
#define ARENA          30.0f   // demi-largeur de l'arene (metres) : le sol jouable va de -30 a +30 en X et Z
#define MAX_BOXES      64      // nombre maximum de caisses / obstacles (boites de collision) dans la carte

// Entites (tailles des tableaux statiques : pas d'allocation dynamique en jeu)
#define MAX_PLAYERS    50      // 2 en ecran partage, jusqu'a 50 en reseau (grosses parties en ligne)
#define MAX_ENEMIES    160     // mercenaires vivants (ou en train de mourir) en meme temps (< 256 : index sur 1 octet)
#define MAX_PARTICLES  2048    // etincelles, sang, poussiere, plumes (si tout est plein, les nouvelles sont ignorees)
#define MAX_TRACERS    64      // traits lumineux des balles
#define MAX_PICKUPS    32      // objets a ramasser au sol (munitions, soin, harpons)
#define MAX_MSGS       6       // lignes du fil de messages a l'ecran

// Reseau
#define NET_PORT           27016   // port UDP de l'hote
#define NET_SNAPSHOT_RATE  30.0f   // etats du monde envoyes par l'hote, par seconde
#define NET_STATE_RATE     60.0f   // etats des joueurs envoyes par les clients, par seconde
// Par le serveur de parties : un peu moins (sa connexion est partagee par toutes les parties)
#define NET_RELAY_SNAPSHOT_RATE  20.0f
#define NET_RELAY_STATE_RATE     30.0f
// Salon claude.ai (navigateur) : le relais accepte ~40 messages/s par page, on reste en dessous
#define NET_ROOM_SNAPSHOT_RATE   8.0f
#define NET_ROOM_STATE_RATE      15.0f
#define NET_TIMEOUT        6.0f    // secondes sans nouvelles avant deconnexion

// Poussins du stand (ce que les mercenaires viennent voler)
#define MAX_CHICKS    12      // taille du tableau de poussins
#define START_CHICKS  12      // poussins presents au debut d'une partie

// Joueur
#define EYE_HEIGHT     1.65f    // hauteur des yeux (camera) au-dessus des pieds, en metres
#define PLAYER_RADIUS  0.35f    // rayon du cylindre de collision du joueur
#define WALK_SPEED     5.5f     // vitesse de marche (m/s)
#define SPRINT_SPEED   8.5f     // vitesse en courant (m/s)
#define JUMP_SPEED     6.0f     // vitesse verticale donnee au saut (m/s) -> saut d'environ 1,1 m avec GRAVITY
#define GRAVITY        16.0f    // acceleration vers le bas (m/s^2), plus forte que 9,81 pour un saut plus "nerveux"
#define MOUSE_SENS     0.0022f   // radians par pixel
#define PAD_LOOK_SPEED 3.2f      // radians par seconde, stick au maximum
#define KEY_LOOK_SPEED 2.4f      // radians par seconde, fleches (J2 au clavier)

// HK416 (fusil d'assaut, arme principale)
#define MAG_SIZE       30       // balles par chargeur
#define START_RESERVE  180      // balles en reserve (hors chargeur) au depart
#define MAX_RESERVE    300      // reserve maximale de balles
#define FIRE_DELAY     (60.0f/850.0f)   // ~850 coups/min : 60 s / 850 = ~0,07 s entre deux tirs
#define RELOAD_TIME    2.2f     // rechargement tactique (il reste une balle chambree), en secondes
#define RELOAD_EMPTY   2.75f    // rechargement a vide (il faut en plus armer la culasse), plus long
#define BULLET_DAMAGE  30.0f    // degats d'une balle dans le corps (points de vie)
#define HEADSHOT_MULT  3.0f     // multiplicateur de degats pour une balle dans la tete

// Harpon : tue et projette le mercenaire touche en un coup
#define HARPOON_START      6       // harpons en reserve au depart (+1 charge)
#define HARPOON_MAX        12      // reserve maximale de harpons
#define HARPOON_RELOAD     1.3f    // temps pour recharger un harpon (secondes)
#define HARPOON_SPEED      55.0f   // m/s
#define HARPOON_GRAVITY    3.0f    // gravite (m/s^2) appliquee au harpon : trajectoire legerement courbe
#define HARPOON_LAUNCH     16.0f   // vitesse horizontale donnee au mercenaire harponne
#define HARPOON_LIFT       8.0f    // ... et vers le haut
#define HARPOON_BOWL_DMG   60.0f   // degats aux mercenaires percutes en vol
#define SWITCH_TIME        0.35f   // changement d'arme

// Camera : angle de vue vertical (field of view), en degres
#define FOV_HIP        75.0f    // vision normale (arme a la hanche)
#define FOV_ADS        52.0f    // en visant (Aim Down Sights) : FOV reduit = effet de zoom
#define FOV_SPRINT     82.0f    // en courant : FOV elargi pour accentuer la sensation de vitesse

// Raccourci pour construire un Vector3 litteral : V3(1, 2, 3)
#define V3(x, y, z) ((Vector3){ (x), (y), (z) })
// Rotation nulle (angles d'Euler a zero), pratique pour TRS / Box3
#define NOROT V3(0, 0, 0)

#endif
