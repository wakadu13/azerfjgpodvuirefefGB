/*
 * util.c - Petites fonctions utilitaires partagees par tout le jeu.
 */
#include "util.h"
#include "raymath.h"

#include <stdlib.h>

// rand() renvoie un entier entre 0 et RAND_MAX : on le ramene entre 0 et 1,
// puis on l'etire sur l'intervalle [a, b] (interpolation lineaire).
float Randf(float a, float b) { return a + (b - a)*(float)rand()/(float)RAND_MAX; }

// Multiplie chaque composante RVB par k (k < 1 assombrit, k > 1 eclaircit).
// Clamp evite de depasser 255 (sinon la conversion en unsigned char "boucle").
// L'alpha (transparence) n'est pas modifie.
Color Shade(Color c, float k)
{
    return (Color){ (unsigned char)Clamp(c.r*k, 0, 255), (unsigned char)Clamp(c.g*k, 0, 255),
                    (unsigned char)Clamp(c.b*k, 0, 255), c.a };
}

// Interpole chaque composante vers 255 (blanc) : t = 0 couleur d'origine,
// t = 1 blanc pur. Sert a faire "clignoter" un ennemi touche par exemple.
Color Flash(Color c, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)Lerp(c.r, 255, t), (unsigned char)Lerp(c.g, 255, t),
                    (unsigned char)Lerp(c.b, 255, t), c.a };
}

// Les shaders travaillent avec des couleurs flottantes entre 0 et 1.
Vector4 ColorVec(Color c) { return (Vector4){ c.r/255.0f, c.g/255.0f, c.b/255.0f, c.a/255.0f }; }

// Dessine d'abord le texte en noir semi-transparent decale de 2 px (l'ombre),
// puis le texte lui-meme par-dessus. L'ombre suit l'opacite du texte (60 %).
void TextShadow(const char *text, int x, int y, int size, Color c)
{
    DrawText(text, x + 2, y + 2, size, (Color){ 0, 0, 0, (unsigned char)(c.a*0.6f) });
    DrawText(text, x, y, size, c);
}

// MeasureText donne la largeur en pixels : on recule de la moitie pour centrer.
void TextCenter(const char *text, int cx, int y, int size, Color c)
{
    TextShadow(text, cx - MeasureText(text, size)/2, y, size, c);
}
