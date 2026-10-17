/*
 * util.h - Fonctions utilitaires : aleatoire, couleurs, texte.
 */
#ifndef UTIL_H
#define UTIL_H

#include "raylib.h"

// Nombre aleatoire flottant uniforme entre a et b (bornes incluses)
float Randf(float a, float b);

Color Shade(Color c, float k);       // assombrit/eclaircit une couleur
Color Flash(Color c, float t);       // melange vers le blanc (t entre 0 et 1)
Vector4 ColorVec(Color c);           // couleur 0..255 -> vecteur 0..1 (format attendu par les shaders)

// Texte avec une ombre portee noire (lisible sur n'importe quel fond)
void TextShadow(const char *text, int x, int y, int size, Color c);
// Idem, centre horizontalement sur cx (y = haut du texte)
void TextCenter(const char *text, int cx, int y, int size, Color c);

#endif
