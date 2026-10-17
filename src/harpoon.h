/*
 * harpoon.h - Fleches de harpon en vol.
 *
 * Une fleche est un vrai projectile (vitesse, legere chute). Le mercenaire
 * qu'elle touche est tue d'un coup et projete (voir EnemyHarpoon). Une
 * fleche qui touche le decor y reste plantee quelques secondes.
 */
#ifndef HARPOON_H
#define HARPOON_H

#include "player.h"

#include "raylib.h"

void HarpoonsClear(void);
void HarpoonFire(Player *p);                         // tir du joueur (local)
void HarpoonSpawnRemote(Vector3 pos, Vector3 vel);   // fleche d'un autre joueur (visuelle)
void HarpoonsUpdate(float dt);
void HarpoonsDraw(void);

#endif
