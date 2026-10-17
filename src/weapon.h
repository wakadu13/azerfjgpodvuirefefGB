/*
 * weapon.h - Les armes du joueur : le HK416 et le harpon.
 */
#ifndef WEAPON_H
#define WEAPON_H

#include "raylib.h"

typedef struct Player Player;

typedef enum { WPN_RIFLE, WPN_HARPOON } WeaponId;

typedef struct {
    WeaponId active;
    float switchT;                // 1 -> 0 pendant que l'arme remonte apres un changement

    // HK416
    int ammo, reserve;
    float fireT;                  // delai avant le prochain tir
    bool reloading;
    float reloadT, reloadDur;
    int reloadStep;               // clics sonores deja joues
    bool semi, shotThisPress;

    // Harpon
    bool harpoonLoaded;
    int harpoons;                 // harpons en reserve
    float harpoonT;               // rechargement du harpon en cours (s restantes)

    // Commun
    float ads;                    // 0 = hanche, 1 = visee
    float sprintT;                // 0..1, arme baissee pendant le sprint
    float muzzleT, kick, bloom;
    float recoilPitch, recoilYaw; // recul qui revient tout seul
    Vector2 sway;
} Weapon;

void WeaponReset(Weapon *w);
void WeaponUpdate(Player *p, float dt);
float WeaponSpread(const Player *p);

// Position du canon dans le monde (traçantes, eclair, depart du harpon).
Vector3 WeaponMuzzleWorld(const Player *p);

// Passe "vue a la premiere personne" : mains + arme, par-dessus la scene.
void WeaponRenderViewModel(const Player *p, Camera3D worldCam);

// Modeles seuls dans un repere donne (canon vers -Z). w peut etre NULL.
void DrawHK416(Matrix frame, const Weapon *w);
void DrawHarpoonGun(Matrix frame, const Weapon *w);
void DrawSpear(Matrix frame);   // fleche de harpon, pointe vers -Z, longueur 1.15

#endif
