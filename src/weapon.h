/*
 * weapon.h - Les armes du joueur : le HK416 et le harpon.
 *
 * Chaque joueur possede une structure Weapon qui contient l'etat des DEUX
 * armes (munitions, rechargement...) et les valeurs d'animation communes
 * (visee, recul, balancement). Une seule arme est active a la fois.
 */
#ifndef WEAPON_H
#define WEAPON_H

#include "raylib.h"

// Declaration anticipee : la structure complete est dans player.h (qui
// inclut ce fichier), on ne peut donc pas l'inclure ici.
typedef struct Player Player;

// Identifiant de l'arme active (aussi utilise pour les touches 1 / 2)
typedef enum { WPN_RIFLE, WPN_HARPOON } WeaponId;

typedef struct {
    WeaponId active;              // arme actuellement en main
    float switchT;                // 1 -> 0 pendant que l'arme remonte apres un changement

    // HK416
    int ammo, reserve;            // balles dans le chargeur / balles en reserve
    float fireT;                  // delai avant le prochain tir
    bool reloading;               // rechargement en cours
    float reloadT, reloadDur;     // temps ecoule / duree totale du rechargement
    int reloadStep;               // clics sonores deja joues
    bool semi, shotThisPress;     // mode semi-auto ; un tir a deja eu lieu pendant cet appui

    // Harpon
    bool harpoonLoaded;           // une fleche est en place, pret a tirer
    int harpoons;                 // harpons en reserve
    float harpoonT;               // rechargement du harpon en cours (s restantes)

    // Commun
    float ads;                    // 0 = hanche, 1 = visee
    float sprintT;                // 0..1, arme baissee pendant le sprint
    // muzzleT : > 0 = flash de bouche visible (secondes restantes)
    // kick    : 1 -> 0 apres un tir, l'arme recule vers l'epaule
    // bloom   : dispersion ajoutee par les tirs rapproches (radians)
    float muzzleT, kick, bloom;
    float recoilPitch, recoilYaw; // recul qui revient tout seul
    Vector2 sway;                 // decalage de l'arme qui "traine" derriere la camera
} Weapon;

void WeaponReset(Weapon *w);              // etat de depart (chargeur plein, HK416 en main)
void WeaponUpdate(Player *p, float dt);   // tir, rechargement, visee, animations
float WeaponSpread(const Player *p);      // dispersion actuelle du HK416 (rayon du cone, ~radians)

// Position du canon dans le monde (tracantes, eclair, depart du harpon).
Vector3 WeaponMuzzleWorld(const Player *p);

// Passe "vue a la premiere personne" : mains + arme, par-dessus la scene.
void WeaponRenderViewModel(const Player *p, Camera3D worldCam);

// Modeles seuls dans un repere donne (canon vers -Z). w peut etre NULL.
// (NULL = modele "au repos" : chargeur en place, harpon charge, pas de flash)
void DrawHK416(Matrix frame, const Weapon *w);
void DrawHarpoonGun(Matrix frame, const Weapon *w);
void DrawSpear(Matrix frame);   // fleche de harpon, pointe vers -Z, longueur 1.15

#endif
