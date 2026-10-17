/*
 * touch.c - Gestion multi-touch : attribution d'un role a chaque doigt,
 * joystick virtuel flottant, visee par glissement et boutons ronds.
 *
 * Toutes les positions sont en unites virtuelles de l'interface (voir ui.h),
 * ce qui garde les boutons a la meme taille relative sur tous les ecrans.
 */
#include "touch.h"
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <string.h>

#define MAX_FINGERS   10        // nombre maximum de doigts suivis en meme temps
#define JOY_RADIUS    70.0f     // rayon du joystick (unites virtuelles) : course maximale du doigt
#define TOUCH_SENS    0.0065f   // radians par unite virtuelle glissee
#define MOUSE_FINGER  9999      // sur PC (BR_TOUCH=1), la souris joue le role d'un doigt

// Role d'un doigt, choisi a l'endroit ou il se pose. TR_COUNT = nombre de roles.
typedef enum { TR_NONE, TR_JOY, TR_LOOK, TR_FIRE, TR_AIM, TR_JUMP, TR_RELOAD, TR_SWITCH, TR_PAUSE, TR_COUNT } TouchRole;

// Etat d'un doigt suivi d'une frame a l'autre
typedef struct {
    bool used, seen;          // used : emplacement occupe ; seen : doigt encore present cette frame
    int id;                   // identifiant donne par le systeme (stable tant que le doigt est pose)
    TouchRole role;
    Vector2 start, pos, last; // point de depart (ou base du joystick), position actuelle, position precedente
} Finger;

// Bouton rond : role, centre c et rayon r (unites virtuelles)
typedef struct { TouchRole role; Vector2 c; float r; } TouchButton;
// Point de contact brut lu aupres de raylib
typedef struct { int id; Vector2 p; } TouchPoint;

static Finger fingers[MAX_FINGERS];
static bool pressed[TR_COUNT];   // appuis de cette frame
static bool aimToggle, fireHeld; // visee activee (bascule) ; doigt sur FEU
static Vector2 joy, lookDelta;   // joy : direction -1..1 ; lookDelta : glissement total de visee cette frame
static Vector2 joyBase;           // centre du joystick actif

// Position des boutons (coin bas droit), en unites virtuelles
// Remplit le tableau b et renvoie le nombre de boutons. vw / vh = taille de
// l'ecran virtuel : les boutons sont places par rapport au coin bas droit
// (FEU, le plus gros, sous le pouce droit), sauf pause en haut au centre.
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

// Determine le role d'un doigt qui se pose en p :
//  - sur un bouton (zone 15 % plus grande que le cercle dessine, plus facile a viser) ;
//  - sinon dans les 40 % gauches de l'ecran (hors bande du haut) : joystick ;
//  - partout ailleurs : visee.
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

// Lit les doigts presents, les associe aux doigts deja suivis (par leur id),
// attribue un role aux nouveaux, puis calcule joystick, visee et appuis.
void TouchUpdate(void)
{
    float s = UiScale();
    int vw = UiWidth(), vh = UiHeight();
    // Remise a zero des valeurs "de cette frame"
    memset(pressed, 0, sizeof(pressed));
    joy = lookDelta = (Vector2){ 0 };
    fireHeld = false;
    for (int i = 0; i < MAX_FINGERS; i++) fingers[i].seen = false;

    // Doigts presents cette frame
    // (positions converties de pixels en unites virtuelles)
    TouchPoint pts[MAX_FINGERS];
    int count = 0;
    int n = GetTouchPointCount();
    for (int i = 0; i < n && count < MAX_FINGERS; i++)
    {
        pts[count].id = GetTouchPointId(i);
        pts[count].p = Vector2Scale(GetTouchPosition(i), 1.0f/s);
        count++;
    }
    // Aucun vrai doigt mais bouton de souris enfonce : la souris fait office de doigt (test sur PC)
    if (n == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) pts[count++] = (TouchPoint){ MOUSE_FINGER, UiMouse() };

    for (int k = 0; k < count; k++)
    {
        // Ce doigt etait-il deja suivi a la frame precedente ?
        Finger *f = NULL;
        for (int i = 0; i < MAX_FINGERS && !f; i++) if (fingers[i].used && fingers[i].id == pts[k].id) f = &fingers[i];
        if (!f)
        {
            // Nouveau doigt : son role depend de l'endroit ou il s'est pose
            for (int i = 0; i < MAX_FINGERS && !f; i++) if (!fingers[i].used) f = &fingers[i];
            if (!f) continue;   // plus de place : doigt ignore
            *f = (Finger){ true, false, pts[k].id, HitTest(pts[k].p, vw, vh), pts[k].p, pts[k].p, pts[k].p };
            pressed[f->role] = true;   // appui = uniquement la frame ou le doigt se pose
            if (f->role == TR_AIM) aimToggle = !aimToggle;   // VISER fonctionne comme un interrupteur
        }
        f->seen = true;
        f->last = f->pos;
        f->pos = pts[k].p;

        switch (f->role)
        {
            case TR_JOY:
            {
                // Joystick flottant : la base suit le doigt s'il va trop loin
                // d = deplacement depuis la base ; si |d| > rayon, on rapproche
                // la base du doigt pour que |d| = rayon exactement.
                Vector2 d = Vector2Subtract(f->pos, f->start);
                float len = Vector2Length(d);
                if (len > JOY_RADIUS)
                {
                    f->start = Vector2Add(f->start, Vector2Scale(d, (len - JOY_RADIUS)/len));
                    d = Vector2Scale(d, JOY_RADIUS/len);
                }
                // Normalisation : joy entre -1 et 1 sur chaque axe
                joy = Vector2Scale(d, 1.0f/JOY_RADIUS);
                if (Vector2Length(joy) < 0.12f) joy = (Vector2){ 0 };   // zone morte (12 %) contre les micro-mouvements
                joyBase = f->start;
            } break;
            case TR_FIRE:   // on peut aussi viser en glissant le doigt depuis FEU
            case TR_LOOK:
                if (f->role == TR_FIRE) fireHeld = true;
                // On additionne le deplacement depuis la frame precedente (plusieurs doigts possibles)
                lookDelta = Vector2Add(lookDelta, Vector2Subtract(f->pos, f->last));
                break;
            default: break;
        }
    }
    // Les doigts qui n'ont pas ete vus cette frame ont ete leves : on libere leur emplacement
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].used && !fingers[i].seen) fingers[i].used = false;
}

// Traduit l'etat tactile en commandes de jeu. On utilise += et |= pour
// combiner avec les autres peripheriques (clavier, manette) au lieu de les remplacer.
void TouchApplyInput(PlayerInput *in)
{
    // Ecran : y vers le bas. Jeu : move.y positif = avancer, d'ou le signe moins.
    in->move.x += joy.x;
    in->move.y -= joy.y;
    // Glisser vers la droite tourne la vue vers la droite (lacet negatif dans ce repere)
    in->look.x -= lookDelta.x*TOUCH_SENS;
    in->look.y -= lookDelta.y*TOUCH_SENS;
    in->fire |= fireHeld;
    in->aim |= aimToggle;
    in->sprint |= joy.y < -0.95f;   // joystick pousse a fond vers le haut = sprint
    in->jump |= pressed[TR_JUMP];
    in->reload |= pressed[TR_RELOAD];
    in->switchWeapon |= pressed[TR_SWITCH];
}

bool TouchPausePressed(void) { return pressed[TR_PAUSE]; }

// Vrai si au moins un doigt pose a ce role (pour allumer le bouton)
static bool RoleHeld(TouchRole role)
{
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].used && fingers[i].role == role) return true;
    return false;
}

// Dessine les commandes tactiles par-dessus le jeu (entre UiBegin et UiEnd).
// p sert a afficher le nom de l'autre arme sur le bouton de changement.
void TouchDraw(const Player *p)
{
    int vw = UiWidth(), vh = UiHeight();

    // Joystick : position par defaut en transparence, ou sous le doigt
    // Cercle de fond + contour + "manche" (petit disque) decale selon joy.
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
        // Libelle et couleur de chaque bouton
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
        // Bouton "enfonce" : rempli de sa couleur ; sinon fond noir transparent
        bool held = RoleHeld(b[i].role) || (b[i].role == TR_AIM && aimToggle);
        DrawCircleV(b[i].c, b[i].r, Fade(held ? c : BLACK, held ? 0.45f : 0.3f));
        DrawCircleLinesV(b[i].c, b[i].r, Fade(c, 0.8f));
        // Taille du texte : grande pour FEU (rayon > 50), moyenne pour pause, petite sinon
        int size = (b[i].r > 50) ? 24 : (b[i].role == TR_PAUSE ? 18 : 14);
        TextCenter(label, (int)b[i].c.x, (int)b[i].c.y - size/2, size, Fade(c, 0.95f));
    }
}
