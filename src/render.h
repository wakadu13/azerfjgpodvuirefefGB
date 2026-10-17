/*
 * render.h - Shader d'eclairage et primitives 3D de base.
 *
 * Tous les modeles du jeu (ennemis, arme, decor) sont assembles a partir de
 * trois meshes : un cube, une sphere et un cylindre, places par matrices.
 * Convention : MatrixMultiply(local, parent) applique local puis parent.
 */
#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "raymath.h"

extern const Color SKY_TOP;
extern const Color SKY_HORIZON;

void RenderInit(void);
void RenderClose(void);

Vector3 RenderSunDir(void);

// Uniformes globaux d'une passe : position camera, soleil, brouillard,
// eclair du canon (point lumineux temporaire).
void RenderSetFrame(Vector3 viewPos, Vector3 lightDir, float fogDensity, Vector3 flashPos, float flashPower);
void RenderSetEmissive(bool on);   // couleur pleine, sans eclairage
void RenderSetChecker(bool on);    // motif carrelage / herbe (sol)
void RenderClearDepth(void);       // vide le depth buffer (passe de l'arme)

void DrawSky(int width, int height);

// Primitives. t = translation, r = rotation (euler XYZ), s = taille.
Matrix TRS(Vector3 t, Vector3 r, Vector3 s);
void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c);
void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c);
void Seg(Matrix parent, Vector3 a, Vector3 b, float radius, Color c);    // cylindre de a vers b
void Limb(Matrix parent, Vector3 a, Vector3 b, float radius, Color c);   // cylindre + rotules
void DrawBlobShadow(Vector3 p, float radius);

void DrawBurger(Matrix parent);

#endif
