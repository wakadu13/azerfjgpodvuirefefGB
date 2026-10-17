/*
 * map.h - Arene (camp militaire) : murs en gabions, tentes, sacs de sable,
 * caisses, stand des poussins et collisions.
 *
 * Toute la geometrie solide de la carte est faite de boites alignees sur les
 * axes (AABB : "axis aligned bounding box"), posees sur le sol (y = 0).
 * Cela rend les collisions et les tirs (raycasts) tres simples a calculer.
 */
#ifndef MAP_H
#define MAP_H

#include "config.h"
#include "raylib.h"

// Apparence d'une boite (la collision est la meme pour tous les styles) :
// caisse militaire, muret de sacs de sable, mur en gabions, comptoir du
// stand, grande tente beige a armature noire, tente vert olive.
typedef enum { BX_CRATE, BX_SANDBAGS, BX_WALL, BX_COUNTER, BX_TENT, BX_TENT_GREEN } BoxStyle;

typedef struct {
    BoundingBox b;     // coins min et max de la boite, en metres
    BoxStyle style;
} Box;

extern Box boxes[MAX_BOXES];   // boxes[0] est toujours le comptoir du stand
extern int boxCount;           // nombre de boites utilisees dans le tableau

void MapBuild(void);                    // (re)cree la liste des boites de la carte
void MapDraw(float time, int chicks);   // dessine sol, boites, stand et "chicks" poussins sur le comptoir

// Repousse un cercle (plan XZ) hors des boites qu'il touche.
// p : centre (modifie sur place) ; feetY / height : bas et hauteur du corps,
// pour ignorer les boites au-dessus de la tete ou sous les pieds.
void ResolveCircle(Vector3 *p, float radius, float feetY, float height);
// Hauteur du sol sous un cercle (0 ou le dessus d'une caisse).
float GroundHeightAt(Vector3 p, float radius, float feetY);
// Point du comptoir le plus proche de p (au sol).
Vector3 CounterPoint(Vector3 p);
// Rayon contre le sol et les boites. Met a jour *dist/*normal si plus proche.
// *dist doit contenir la distance maximale (ou le meilleur impact deja trouve).
// Renvoie true si un impact plus proche a ete trouve.
bool MapRaycast(Ray ray, float *dist, Vector3 *normal);

#endif
