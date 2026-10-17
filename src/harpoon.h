/*
 * harpoon.h - Fleches de harpon en vol.
 *
 * Une fleche est un vrai projectile (vitesse, legere chute). Le mercenaire
 * qu'elle touche est tue d'un coup et projete (voir EnemyHarpoon). Une
 * fleche qui touche le decor y reste plantee quelques secondes.
 *
 * Contrairement aux balles du HK416 (touche instantanee par lancer de
 * rayon), la fleche avance un peu a chaque frame : on peut la voir voler.
 */
#ifndef HARPOON_H
#define HARPOON_H

#include "player.h"

#include "raylib.h"

void HarpoonsClear(void);                            // supprime toutes les fleches (nouvelle partie)
void HarpoonFire(Player *p);                         // tir du joueur (local)
void HarpoonSpawnRemote(Vector3 pos, Vector3 vel);   // fleche d'un autre joueur (visuelle)
void HarpoonsUpdate(float dt);                       // deplacement + collisions de toutes les fleches
void HarpoonsDraw(void);                             // dessine les fleches en vol ou plantees

#endif
