/*
 * weapon.h - Le HK416 : tir, recul, rechargement et modele 3D.
 */
#ifndef WEAPON_H
#define WEAPON_H

#include "raylib.h"

typedef struct Player Player;

typedef struct {
    int ammo, reserve;
    float fireT;                  // delai avant le prochain tir
    bool reloading;
    float reloadT, reloadDur;
    int reloadStep;               // clics sonores deja joues
    float ads;                    // 0 = hanche, 1 = viseur point rouge
    float sprintT;                // 0..1, arme baissee pendant le sprint
    float muzzleT, kick, bloom;
    float recoilPitch, recoilYaw; // recul qui revient tout seul
    Vector2 sway;
    bool semi, shotThisPress;
} Weapon;

void WeaponReset(Weapon *w);
void WeaponUpdate(Player *p, float dt);
float WeaponSpread(const Player *p);

// Position du canon dans le monde (traçantes, eclair) et en espace vue.
Vector3 WeaponMuzzleWorld(const Player *p);

// Passe "vue a la premiere personne" : mains + arme, par-dessus la scene.
void WeaponRenderViewModel(const Player *p, Camera3D worldCam);

// HK416 seul dans un repere donne (canon vers -Z). w peut etre NULL.
void DrawHK416(Matrix frame, const Weapon *w);

#endif
