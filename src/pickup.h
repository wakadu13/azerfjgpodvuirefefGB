/*
 * pickup.h - Bonus laches par les gloutons : munitions et salade (soin).
 */
#ifndef PICKUP_H
#define PICKUP_H

#include "raylib.h"

void PickupsClear(void);
void PickupDrop(Vector3 pos);     // avec une certaine probabilite
void PickupsUpdate(float dt);     // ramassage par les joueurs
void PickupsDraw(void);

// Synchronisation reseau
typedef enum { PK_AMMO, PK_SALAD } PickupType;
bool PickupGet(int idx, int *type, Vector3 *pos, float *t);
void PickupSet(int idx, bool active, int type, Vector3 pos, float t);
void PickupsAnimate(float dt);   // client : fait seulement tourner les bonus

#endif
