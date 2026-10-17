#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <stdlib.h>

bool TouchMode(void)
{
#if defined(BR_MOBILE)
    return true;
#else
    static int touch = -1;
    if (touch < 0) touch = (getenv("BR_TOUCH") != NULL);
    return touch;
#endif
}

float UiScale(void)
{
    float h = (float)GetScreenHeight();
    float s = TouchMode() ? h/540.0f : h/720.0f;
    if (!TouchMode() && s < 1.0f) s = 1.0f;   // sur PC, jamais plus petit qu'avant
    return Clamp(s, 0.5f, 4.0f);
}

int UiWidth(void) { return (int)(GetScreenWidth()/UiScale()); }
int UiHeight(void) { return (int)(GetScreenHeight()/UiScale()); }
Vector2 UiMouse(void) { return Vector2Scale(GetMousePosition(), 1.0f/UiScale()); }

void UiBegin(void)
{
    Camera2D cam = { 0 };
    cam.zoom = UiScale();
    BeginMode2D(cam);
}

void UiEnd(void) { EndMode2D(); }

void UiDrawButton(Rectangle r, const char *label, Color accent)
{
    bool hover = !TouchMode() && CheckCollisionPointRec(UiMouse(), r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    DrawRectangleRounded(r, 0.3f, 6, down ? Fade(accent, 0.5f) : (Color){ 15, 15, 20, hover ? 230 : 190 });
    DrawRectangleRoundedLinesEx(r, 0.3f, 6, 2.0f, accent);
    int size = (int)(r.height*0.5f);
    TextCenter(label, (int)(r.x + r.width/2), (int)(r.y + (r.height - size)/2), size, RAYWHITE);
}

bool UiButtonHit(Rectangle r)
{
    return IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(UiMouse(), r);
}
