/*
 * pickup.h - Bonus laches par les mercenaires : munitions et salade (soin).
 */
#ifndef PICKUP_H
#define PICKUP_H

#include "raylib.h"

void PickupsClear(void);          // supprime tous les bonus (nouvelle partie)
void PickupDrop(Vector3 pos);     // avec une certaine probabilite
void PickupsUpdate(float dt);     // ramassage par les joueurs
void PickupsDraw(void);           // dessin 3D (pendant la passe 3D)

// Synchronisation reseau
// L'hote simule les bonus et envoie leur etat ; les clients le recopient.
typedef enum { PK_AMMO, PK_SALAD } PickupType;
// Lit l'emplacement idx (0..MAX_PICKUPS-1) ; renvoie true s'il est actif
bool PickupGet(int idx, int *type, Vector3 *pos, float *t);
// Ecrase l'emplacement idx avec les valeurs recues de l'hote
void PickupSet(int idx, bool active, int type, Vector3 pos, float t);
void PickupsAnimate(float dt);   // client : fait seulement tourner les bonus

#endif
