/*
 * ui.c - Mise a l'echelle de l'interface 2D et boutons (souris ou doigt).
 */
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <stdlib.h>

#if defined(BR_WEB)
    #include <emscripten/emscripten.h>   // EM_ASM_INT : petit bout de JavaScript
#endif

// Vrai si l'on doit afficher / utiliser les controles tactiles.
// Sur mobile (BR_MOBILE defini a la compilation) c'est toujours le cas.
// Sur PC, on peut forcer ce mode en lancant le jeu avec la variable
// d'environnement BR_TOUCH (ex. "BR_TOUCH=1 ./zeffalie_come_back") pour tester
// l'interface tactile a la souris. Le resultat est mis en cache (static)
// pour ne lire l'environnement qu'une seule fois.
bool TouchMode(void)
{
#if defined(BR_MOBILE)
    return true;
#elif defined(BR_WEB)
    // Navigateur : commandes tactiles si l'ecran se pilote au doigt
    // (telephone, tablette), clavier + souris sinon (ordinateur)
    static int touch = -1;
    if (touch < 0) touch = EM_ASM_INT({ return (window.matchMedia && window.matchMedia("(pointer: coarse)").matches) ? 1 : 0; });
    return touch;
#else
    static int touch = -1;   // -1 = pas encore determine
    if (touch < 0) touch = (getenv("BR_TOUCH") != NULL);
    return touch;
#endif
}

// Echelle = hauteur reelle / hauteur virtuelle (540 en tactile, 720 sinon).
// Le resultat est borne entre 0.5 et 4 pour rester lisible sur des ecrans
// extremes (tres petite fenetre ou ecran 8K).
float UiScale(void)
{
    float h = (float)GetScreenHeight();
    float s = TouchMode() ? h/540.0f : h/720.0f;
    if (!TouchMode() && s < 1.0f) s = 1.0f;   // sur PC, jamais plus petit qu'avant
    return Clamp(s, 0.5f, 4.0f);
}

// Dimensions de l'ecran et position du pointeur converties en unites virtuelles
// (on divise les pixels reels par l'echelle).
int UiWidth(void) { return (int)(GetScreenWidth()/UiScale()); }
int UiHeight(void) { return (int)(GetScreenHeight()/UiScale()); }
Vector2 UiMouse(void) { return Vector2Scale(GetMousePosition(), 1.0f/UiScale()); }

// On utilise une camera 2D de raylib uniquement pour son zoom : tout ce qui
// est dessine entre UiBegin et UiEnd est multiplie par UiScale().
void UiBegin(void)
{
    Camera2D cam = { 0 };
    cam.zoom = UiScale();
    BeginMode2D(cam);
}

void UiEnd(void) { EndMode2D(); }

// Dessine un bouton arrondi : fond sombre (plus opaque au survol), fond de la
// couleur d'accent quand on appuie, contour de la couleur d'accent, et
// libelle centre dont la taille vaut la moitie de la hauteur du bouton.
// En mode tactile il n'y a pas de "survol" (pas de curseur), d'ou le test.
void UiDrawButton(Rectangle r, const char *label, Color accent)
{
    bool hover = !TouchMode() && CheckCollisionPointRec(UiMouse(), r);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    DrawRectangleRounded(r, 0.3f, 6, down ? Fade(accent, 0.5f) : (Color){ 15, 15, 20, hover ? 230 : 190 });
    DrawRectangleRoundedLinesEx(r, 0.3f, 6, 2.0f, accent);
    int size = (int)(r.height*0.5f);
    TextCenter(label, (int)(r.x + r.width/2), (int)(r.y + (r.height - size)/2), size, RAYWHITE);
}

// Le bouton est active quand on relache le clic (ou leve le doigt : raylib
// emule la souris avec le premier doigt) au-dessus de lui. Agir au relachement
// plutot qu'a l'appui permet d'annuler en glissant hors du bouton.
// Doigt qui vient de quitter l'ecran (cette image). Necessaire dans le
// navigateur : raylib n'y transforme pas un toucher en clic de souris
// (sur Android, si). La souris garde la position du dernier doigt.
static int touchPrev, touchNow;

void UiUpdate(void)
{
    touchPrev = touchNow;
    touchNow = GetTouchPointCount();
}

bool UiButtonHit(Rectangle r)
{
    bool released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
#if defined(BR_WEB)
    released = released || (touchPrev > 0 && touchNow == 0);
#endif
    return released && CheckCollisionPointRec(UiMouse(), r);
}
