#include "touch.h"
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <string.h>

#define MAX_FINGERS   10
#define JOY_RADIUS    70.0f
#define TOUCH_SENS    0.0065f   // radians par unite virtuelle glissee
#define MOUSE_FINGER  9999      // sur PC (BR_TOUCH=1), la souris joue le role d'un doigt

typedef enum { TR_NONE, TR_JOY, TR_LOOK, TR_FIRE, TR_AIM, TR_JUMP, TR_RELOAD, TR_SWITCH, TR_PAUSE, TR_COUNT } TouchRole;

typedef struct {
    bool used, seen;
    int id;
    TouchRole role;
    Vector2 start, pos, last;
} Finger;

typedef struct { TouchRole role; Vector2 c; float r; } TouchButton;
typedef struct { int id; Vector2 p; } TouchPoint;

static Finger fingers[MAX_FINGERS];
static bool pressed[TR_COUNT];   // appuis de cette frame
static bool aimToggle, fireHeld;
static Vector2 joy, lookDelta;
static Vector2 joyBase;           // centre du joystick actif

// Position des boutons (coin bas droit), en unites virtuelles
static int Buttons(TouchButton *b, int vw, int vh)
{
    int n = 0;
    b[n++] = (TouchButton){ TR_FIRE,   { vw - 115.0f, vh - 115.0f }, 62 };
    b[n++] = (TouchButton){ TR_AIM,    { vw - 240.0f, vh - 62.0f },  40 };
    b[n++] = (TouchButton){ TR_JUMP,   { vw - 62.0f,  vh - 240.0f }, 38 };
    b[n++] = (TouchButton){ TR_RELOAD, { vw - 195.0f, vh - 205.0f }, 34 };
    b[n++] = (TouchButton){ TR_SWITCH, { vw - 290.0f, vh - 150.0f }, 36 };
    b[n++] = (TouchButton){ TR_PAUSE,  { vw/2 + 240.0f, 38.0f },     24 };
    return n;
}

static TouchRole HitTest(Vector2 p, int vw, int vh)
{
    TouchButton b[8];
    int n = Buttons(b, vw, vh);
    for (int i = 0; i < n; i++) if (Vector2Distance(p, b[i].c) < b[i].r*1.15f) return b[i].role;
    if (p.x < vw*0.4f && p.y > vh*0.25f) return TR_JOY;
    return TR_LOOK;
}

void TouchReset(void)
{
    memset(fingers, 0, sizeof(fingers));
    aimToggle = false;
}

void TouchUpdate(void)
{
    float s = UiScale();
    int vw = UiWidth(), vh = UiHeight();
    memset(pressed, 0, sizeof(pressed));
    joy = lookDelta = (Vector2){ 0 };
    fireHeld = false;
    for (int i = 0; i < MAX_FINGERS; i++) fingers[i].seen = false;

    // Doigts presents cette frame
    TouchPoint pts[MAX_FINGERS];
    int count = 0;
    int n = GetTouchPointCount();
    for (int i = 0; i < n && count < MAX_FINGERS; i++)
    {
        pts[count].id = GetTouchPointId(i);
        pts[count].p = Vector2Scale(GetTouchPosition(i), 1.0f/s);
        count++;
    }
    if (n == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) pts[count++] = (TouchPoint){ MOUSE_FINGER, UiMouse() };

    for (int k = 0; k < count; k++)
    {
        Finger *f = NULL;
        for (int i = 0; i < MAX_FINGERS && !f; i++) if (fingers[i].used && fingers[i].id == pts[k].id) f = &fingers[i];
        if (!f)
        {
            // Nouveau doigt : son role depend de l'endroit ou il s'est pose
            for (int i = 0; i < MAX_FINGERS && !f; i++) if (!fingers[i].used) f = &fingers[i];
            if (!f) continue;
            *f = (Finger){ true, false, pts[k].id, HitTest(pts[k].p, vw, vh), pts[k].p, pts[k].p, pts[k].p };
            pressed[f->role] = true;
            if (f->role == TR_AIM) aimToggle = !aimToggle;
        }
        f->seen = true;
        f->last = f->pos;
        f->pos = pts[k].p;

        switch (f->role)
        {
            case TR_JOY:
            {
                // Joystick flottant : la base suit le doigt s'il va trop loin
                Vector2 d = Vector2Subtract(f->pos, f->start);
                float len = Vector2Length(d);
                if (len > JOY_RADIUS)
                {
                    f->start = Vector2Add(f->start, Vector2Scale(d, (len - JOY_RADIUS)/len));
                    d = Vector2Scale(d, JOY_RADIUS/len);
                }
                joy = Vector2Scale(d, 1.0f/JOY_RADIUS);
                if (Vector2Length(joy) < 0.12f) joy = (Vector2){ 0 };
                joyBase = f->start;
            } break;
            case TR_FIRE:   // on peut aussi viser en glissant le doigt depuis FEU
            case TR_LOOK:
                if (f->role == TR_FIRE) fireHeld = true;
                lookDelta = Vector2Add(lookDelta, Vector2Subtract(f->pos, f->last));
                break;
            default: break;
        }
    }
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].used && !fingers[i].seen) fingers[i].used = false;
}

void TouchApplyInput(PlayerInput *in)
{
    in->move.x += joy.x;
    in->move.y -= joy.y;
    in->look.x -= lookDelta.x*TOUCH_SENS;
    in->look.y -= lookDelta.y*TOUCH_SENS;
    in->fire |= fireHeld;
    in->aim |= aimToggle;
    in->sprint |= joy.y < -0.95f;
    in->jump |= pressed[TR_JUMP];
    in->reload |= pressed[TR_RELOAD];
    in->switchWeapon |= pressed[TR_SWITCH];
}

bool TouchPausePressed(void) { return pressed[TR_PAUSE]; }

static bool RoleHeld(TouchRole role)
{
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].used && fingers[i].role == role) return true;
    return false;
}

void TouchDraw(const Player *p)
{
    int vw = UiWidth(), vh = UiHeight();

    // Joystick : position par defaut en transparence, ou sous le doigt
    bool joyActive = RoleHeld(TR_JOY);
    Vector2 base = joyActive ? joyBase : (Vector2){ 130, vh - 130.0f };
    DrawCircleV(base, JOY_RADIUS, Fade(BLACK, joyActive ? 0.30f : 0.15f));
    DrawCircleLinesV(base, JOY_RADIUS, Fade(WHITE, joyActive ? 0.6f : 0.3f));
    DrawCircleV(Vector2Add(base, Vector2Scale(joy, JOY_RADIUS)), 28, Fade(WHITE, joyActive ? 0.55f : 0.25f));
    if (!joyActive) TextCenter("BOUGER", (int)base.x, (int)base.y - 8, 16, Fade(WHITE, 0.5f));

    TouchButton b[8];
    int n = Buttons(b, vw, vh);
    for (int i = 0; i < n; i++)
    {
        const char *label = "";
        Color c = WHITE;
        switch (b[i].role)
        {
            case TR_FIRE:   label = "FEU"; c = (Color){ 255, 90, 70, 255 }; break;
            case TR_AIM:    label = "VISER"; c = aimToggle ? (Color){ 255, 220, 80, 255 } : WHITE; break;
            case TR_JUMP:   label = "SAUT"; break;
            case TR_RELOAD: label = "RECH."; break;
            case TR_SWITCH: label = (p->w.active == WPN_RIFLE) ? "HARPON" : "HK416"; c = SKYBLUE; break;
            case TR_PAUSE:  label = "II"; break;
            default: break;
        }
        bool held = RoleHeld(b[i].role) || (b[i].role == TR_AIM && aimToggle);
        DrawCircleV(b[i].c, b[i].r, Fade(held ? c : BLACK, held ? 0.45f : 0.3f));
        DrawCircleLinesV(b[i].c, b[i].r, Fade(c, 0.8f));
        int size = (b[i].r > 50) ? 24 : (b[i].role == TR_PAUSE ? 18 : 14);
        TextCenter(label, (int)b[i].c.x, (int)b[i].c.y - size/2, size, Fade(c, 0.95f));
    }
}
