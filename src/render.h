/*
 * render.h - Shader d'eclairage et primitives 3D de base.
 *
 * Tous les modeles du jeu (ennemis, arme, decor) sont assembles a partir de
 * trois meshes : un cube, une sphere et un cylindre, places par matrices.
 * Convention : MatrixMultiply(local, parent) applique local puis parent.
 *
 * Autrement dit, pour dessiner une main au bout d'un bras, on donne la
 * matrice du bras comme "parent" et la position de la main relative au bras :
 * quand le bras bouge, la main suit automatiquement.
 */
#ifndef RENDER_H
#define RENDER_H

#include "raylib.h"
#include "raymath.h"

// Couleurs du ciel (degrade) ; SKY_HORIZON sert aussi de couleur au brouillard
extern const Color SKY_TOP;
extern const Color SKY_HORIZON;

void RenderInit(void);    // compile le shader et cree les 3 meshes de base (apres InitWindow)
void RenderClose(void);   // libere shader et meshes (avant CloseWindow)

Vector3 RenderSunDir(void);   // direction (normalisee) dans laquelle voyage la lumiere du soleil

// Uniformes globaux d'une passe : position camera, soleil, brouillard,
// eclair du canon (point lumineux temporaire).
void RenderSetFrame(Vector3 viewPos, Vector3 lightDir, float fogDensity, Vector3 flashPos, float flashPower);
void RenderSetEmissive(bool on);   // couleur pleine, sans eclairage
void RenderSetChecker(bool on);    // motif carrelage / herbe (sol)
void RenderSetLamps(bool on);      // lampes de la nuit (eteintes pour la passe de l'arme)
void RenderClearDepth(void);       // vide le depth buffer (passe de l'arme)
// Taches sur le tissu (usure, boue du treillis) pour les pieces suivantes :
// couleur de la piece + nuances a, b, c, dans le repere du corps donne.
void RenderSetCamo(bool on, Matrix body, Color a, Color b, Color c);

// Modele construit a partir d'un mesh genere (ex. chaines des ennemis).
Model RenderLoadModel(Mesh mesh);
void RenderUnloadModel(Model m);
// Dessine le modele m avec la matrice monde world et la teinte c
void DrawPart(Model *m, Matrix world, Color c);

// Fond du ciel en 2D (a appeler avant la passe 3D), dimensions en pixels
void DrawSky(Camera3D cam, int width, int height);   // ciel de nuit, etoiles et lune vues depuis cam

// Primitives. t = translation, r = rotation (euler XYZ), s = taille.
Matrix TRS(Vector3 t, Vector3 r, Vector3 s);
// Cube de taille s (cote 1 mis a l'echelle), centre en t, dans le repere parent
void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c);
// Sphere (ou ellipsoide aligne sur les axes) centree en t, de rayons radii
void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c);
// Ellipsoide oriente : axes x et y orthonormes (z = x ^ y).
void BallAxes(Matrix parent, Vector3 t, Vector3 x, Vector3 y, Vector3 radii, Color c);
void Seg(Matrix parent, Vector3 a, Vector3 b, float radius, Color c);    // cylindre de a vers b
void Limb(Matrix parent, Vector3 a, Vector3 b, float radius, Color c);   // cylindre + rotules
void DrawBlobShadow(Vector3 p, float radius);   // ombre ronde au sol sous la position p

// Poussin (environ 25 cm) ; t = temps d'animation, flap = battements d'ailes (0..1).
void DrawChick(Matrix parent, float t, float flap);

#endif
