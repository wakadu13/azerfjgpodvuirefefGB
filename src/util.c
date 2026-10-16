#include "util.h"
#include "raymath.h"

#include <stdlib.h>

float Randf(float a, float b) { return a + (b - a)*(float)rand()/(float)RAND_MAX; }

Color Shade(Color c, float k)
{
    return (Color){ (unsigned char)Clamp(c.r*k, 0, 255), (unsigned char)Clamp(c.g*k, 0, 255),
                    (unsigned char)Clamp(c.b*k, 0, 255), c.a };
}

Color Flash(Color c, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)Lerp(c.r, 255, t), (unsigned char)Lerp(c.g, 255, t),
                    (unsigned char)Lerp(c.b, 255, t), c.a };
}

Vector4 ColorVec(Color c) { return (Vector4){ c.r/255.0f, c.g/255.0f, c.b/255.0f, c.a/255.0f }; }

void TextShadow(const char *text, int x, int y, int size, Color c)
{
    DrawText(text, x + 2, y + 2, size, (Color){ 0, 0, 0, (unsigned char)(c.a*0.6f) });
    DrawText(text, x, y, size, c);
}

void TextCenter(const char *text, int cx, int y, int size, Color c)
{
    TextShadow(text, cx - MeasureText(text, size)/2, y, size, c);
}
