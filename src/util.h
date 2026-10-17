/*
 * util.h - Fonctions utilitaires : aleatoire, couleurs, texte.
 */
#ifndef UTIL_H
#define UTIL_H

#include "raylib.h"

float Randf(float a, float b);

Color Shade(Color c, float k);       // assombrit/eclaircit une couleur
Color Flash(Color c, float t);       // melange vers le blanc (t entre 0 et 1)
Vector4 ColorVec(Color c);

void TextShadow(const char *text, int x, int y, int size, Color c);
void TextCenter(const char *text, int cx, int y, int size, Color c);

#endif
