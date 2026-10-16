/*
 * map.h - Arene : murs, caisses, stand de burgers et collisions.
 */
#ifndef MAP_H
#define MAP_H

#include "config.h"
#include "raylib.h"

typedef enum { BX_CRATE, BX_CONCRETE, BX_WALL, BX_COUNTER } BoxStyle;

typedef struct {
    BoundingBox b;
    BoxStyle style;
} Box;

extern Box boxes[MAX_BOXES];   // boxes[0] est toujours le comptoir du stand
extern int boxCount;

void MapBuild(void);
void MapDraw(float time, int burgers);

// Repousse un cercle (plan XZ) hors des boites qu'il touche.
void ResolveCircle(Vector3 *p, float radius, float feetY, float height);
// Hauteur du sol sous un cercle (0 ou le dessus d'une caisse).
float GroundHeightAt(Vector3 p, float radius, float feetY);
// Point du comptoir le plus proche de p (au sol).
Vector3 CounterPoint(Vector3 p);
// Rayon contre le sol et les boites. Met a jour *dist/*normal si plus proche.
bool MapRaycast(Ray ray, float *dist, Vector3 *normal);

#endif
