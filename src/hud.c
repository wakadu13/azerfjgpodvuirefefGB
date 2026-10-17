/*
 * hud.c - Tout ce qui est dessine en 2D par-dessus la scene.
 */
#include "hud.h"
#include "game.h"
#include "enemy.h"
#include "waves.h"
#include "net.h"
#include "netsock.h"
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char text[96]; float t; Color c; } FeedMsg;

static FeedMsg feed[MAX_MSGS];

static const Color GOLD_TEXT = { 255, 210, 80, 255 };
static const Color AMMO_COLOR = { 230, 200, 120, 255 };
static const Color PANEL = { 0, 0, 0, 110 };

//----------------------------------------------------------------------------------
// Fil d'actualite (eliminations, bonus, burgers manges...)
//----------------------------------------------------------------------------------
void FeedClear(void) { memset(feed, 0, sizeof(feed)); }

void FeedPush(Color c, const char *text)
{
    for (int i = MAX_MSGS - 1; i > 0; i--) feed[i] = feed[i - 1];
    snprintf(feed[0].text, sizeof(feed[0].text), "%s", text);
    feed[0].t = 4.0f;
    feed[0].c = c;
}

void FeedAdd(Color c, const char *fmt, ...)
{
    char text[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    FeedPush(c, text);
    NetRecordFeed(c, text);
}

void FeedAddLocal(Color c, const char *fmt, ...)
{
    char text[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    FeedPush(c, text);
}

void FeedUpdate(float dt)
{
    for (int i = 0; i < MAX_MSGS; i++) feed[i].t -= dt;
}

static void DrawFeed(int w)
{
    for (int i = 0; i < MAX_MSGS; i++)
    {
        if (feed[i].t <= 0) continue;
        Color c = feed[i].c;
        c.a = (unsigned char)(255*fminf(1.0f, feed[i].t));
        TextShadow(feed[i].text, w - 20 - MeasureText(feed[i].text, 18), 80 + i*24, 18, c);
    }
}

//----------------------------------------------------------------------------------
// HUD personnel
//----------------------------------------------------------------------------------
static void DrawDamageVignette(const Player *p, int w, int h)
{
    if (p->hurtT <= 0 && p->hp >= 35) return;
    float a = p->hurtT*0.6f;
    if (p->hp < 35) a = fmaxf(a, (0.25f + 0.1f*sinf(G.time*5.0f))*(1.0f - p->hp/35.0f));
    Color red = { 200, 0, 0, (unsigned char)(Clamp(a, 0, 1)*255) }, clear = { 200, 0, 0, 0 };
    int b = h/4;
    DrawRectangleGradientV(0, 0, w, b, red, clear);
    DrawRectangleGradientV(0, h - b, w, b, clear, red);
    DrawRectangleGradientH(0, 0, b, h, red, clear);
    DrawRectangleGradientH(w - b, 0, b, h, clear, red);
}

static void DrawCrosshair(const Player *p, int cx, int cy)
{
    if (p->w.active == WPN_HARPOON && p->w.sprintT < 0.5f)
    {
        // Harpon : cercle + point
        Color c = p->w.harpoonLoaded ? WHITE : Fade(WHITE, 0.35f);
        DrawCircleLines(cx, cy, 11, c);
        DrawCircleLines(cx, cy, 12, Fade(BLACK, 0.4f));
        DrawRectangle(cx - 1, cy - 1, 3, 3, c);
    }
    // Reticule qui s'ecarte avec la dispersion ; masque au viseur
    else if (p->w.ads < 0.6f && p->w.sprintT < 0.5f)
    {
        int gap = (int)(6 + WeaponSpread(p)*350), len = 9;
        Color c = Fade(WHITE, 1.0f - p->w.ads*1.6f);
        DrawRectangle(cx - gap - len, cy - 1, len, 2, c);
        DrawRectangle(cx + gap, cy - 1, len, 2, c);
        DrawRectangle(cx - 1, cy - gap - len, 2, len, c);
        DrawRectangle(cx - 1, cy + gap, 2, len, c);
        DrawRectangle(cx - 1, cy - 1, 2, 2, c);
    }
    // Hitmarker : blanc = touche, orange = tete, rouge = elimine
    if (p->hitT > 0)
    {
        Color c = p->hitKill ? RED : (p->hitHead ? ORANGE : WHITE);
        c.a = (unsigned char)(255*fminf(1, p->hitT/0.1f));
        const float a = 6, b = 14;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                DrawLineEx((Vector2){ cx + sx*b, cy + sy*b }, (Vector2){ cx + sx*a, cy + sy*a }, 2.5f, c);
    }
}

static void DrawHealth(const Player *p, int h, bool showName)
{
    char buf[32];
    int x = 30, y = h - 60;
    DrawRectangle(x - 4, y - 26, 268, 56, PANEL);
    if (showName)
    {
        snprintf(buf, sizeof(buf), "JOUEUR %d", p->id + 1);
        TextShadow(buf, x, y - 22, 16, p->color);
    }
    else TextShadow("SANTE", x, y - 22, 16, RAYWHITE);
    DrawRectangle(x, y, 260, 20, (Color){ 40, 40, 40, 200 });
    Color hc = p->hp > 50 ? (Color){ 90, 220, 90, 255 } : (p->hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(x, y, (int)(260*p->hp/100.0f), 20, hc);
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(p->hp));
    TextShadow(buf, x + 220, y - 22, 18, RAYWHITE);
}

static void DrawHarpoonAmmo(const Player *p, int w, int h, int cx, int cy)
{
    const Weapon *wp = &p->w;
    char buf[32];
    int x = w - 260, y = h - 86;
    DrawRectangle(x - 10, y - 6, 250, 82, PANEL);
    TextShadow(wp->harpoonLoaded ? "1" : "0", x, y, 56, wp->harpoonLoaded ? RAYWHITE : (Color){ 255, 90, 70, 255 });
    snprintf(buf, sizeof(buf), "/ %d", wp->harpoons);
    TextShadow(buf, x + MeasureText("0", 56) + 12, y + 26, 26, LIGHTGRAY);
    TextShadow("HARPON", x + 140, y + 4, 20, SKYBLUE);
    TextShadow("1 coup = 1 glouton", x + 98, y + 30, 14, LIGHTGRAY);
    for (int i = 0; i < HARPOON_MAX; i++)   // une encoche par fleche en reserve
        DrawRectangle(x + i*18, y + 62, 14, 5, (i < wp->harpoons) ? SKYBLUE : (Color){ 80, 80, 80, 200 });

    if (wp->harpoonT > 0)
    {
        TextCenter("RECHARGEMENT DU HARPON...", cx, cy + 50, 20, RAYWHITE);
        DrawRectangle(cx - 80, cy + 76, 160, 6, (Color){ 0, 0, 0, 150 });
        DrawRectangle(cx - 80, cy + 76, (int)(160*(1.0f - wp->harpoonT/HARPOON_RELOAD)), 6, SKYBLUE);
    }
    else if (!wp->harpoonLoaded && wp->harpoons == 0) TextCenter("PLUS DE HARPONS ! (1 / E : HK416)", cx, cy + 50, 22, RED);
}

static void DrawAmmo(const Player *p, int w, int h, int cx, int cy)
{
    const Weapon *wp = &p->w;
    char buf[32];
    int x = w - 260, y = h - 86;
    if (wp->active == WPN_HARPOON) { DrawHarpoonAmmo(p, w, h, cx, cy); return; }
    DrawRectangle(x - 10, y - 6, 250, 82, PANEL);
    snprintf(buf, sizeof(buf), "%d", wp->ammo);
    TextShadow(buf, x, y, 56, wp->ammo <= 8 ? (Color){ 255, 90, 70, 255 } : RAYWHITE);
    snprintf(buf, sizeof(buf), "/ %d", wp->reserve);
    TextShadow(buf, x + MeasureText(wp->ammo >= 10 ? "00" : "0", 56) + 12, y + 26, 26, LIGHTGRAY);
    TextShadow("HK416", x + 150, y + 4, 20, AMMO_COLOR);
    TextShadow(wp->semi ? "SEMI" : "AUTO", x + 150, y + 28, 16, LIGHTGRAY);
    for (int i = 0; i < MAG_SIZE; i++)
        DrawRectangle(x + i*7, y + 62, 5, 9, (i < wp->ammo) ? AMMO_COLOR : (Color){ 80, 80, 80, 200 });

    if (wp->reloading)
    {
        TextCenter("RECHARGEMENT...", cx, cy + 50, 20, RAYWHITE);
        DrawRectangle(cx - 80, cy + 76, 160, 6, (Color){ 0, 0, 0, 150 });
        DrawRectangle(cx - 80, cy + 76, (int)(160*wp->reloadT/wp->reloadDur), 6, AMMO_COLOR);
    }
    else if (wp->ammo == 0 && wp->reserve == 0) TextCenter("PLUS DE MUNITIONS !", cx, cy + 50, 22, RED);
    else if (wp->ammo <= 5) TextCenter("RECHARGER !", cx, cy + 50, 20, (Color){ 255, 200, 80, 255 });
}

static void DrawRadar(const Player *p, int cx, int cy, int R)
{
    DrawCircle(cx, cy, (float)R, (Color){ 0, 0, 0, 120 });
    DrawCircleLines(cx, cy, (float)R, (Color){ 255, 255, 255, 120 });
    DrawLine(cx - R, cy, cx + R, cy, (Color){ 255, 255, 255, 40 });
    DrawLine(cx, cy - R, cx, cy + R, (Color){ 255, 255, 255, 40 });

    // Repere du joueur : l'avant est toujours vers le haut du radar
    const float range = 32.0f, k = R/range;
    Vector2 fwd = { sinf(p->yaw), cosf(p->yaw) }, right = { -cosf(p->yaw), sinf(p->yaw) };
    Vector2 center = { (float)cx, (float)cy };
    #define TO_RADAR(wx, wz) ((Vector2){ cx + (((wx) - p->pos.x)*right.x + ((wz) - p->pos.z)*right.y)*k, \
                                         cy - (((wx) - p->pos.x)*fwd.x + ((wz) - p->pos.z)*fwd.y)*k })

    Vector2 sp = TO_RADAR(0.0f, 0.0f);   // stand
    if (Vector2Distance(sp, center) < R) DrawRectangle((int)sp.x - 5, (int)sp.y - 3, 10, 6, (Color){ 255, 200, 40, 255 });
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        const Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        sp = TO_RADAR(e->pos.x, e->pos.z);
        if (Vector2Distance(sp, center) > R - 3) continue;
        DrawCircleV(sp, e->type == E_BOSS ? 6.0f : 3.0f, e->state == ES_EAT ? ORANGE : RED);
    }
    for (int i = 0; i < numPlayers; i++)   // coequipiers
    {
        if (&players[i] == p || !players[i].connected || !players[i].alive) continue;
        sp = TO_RADAR(players[i].pos.x, players[i].pos.z);
        if (Vector2Distance(sp, center) <= R - 3) DrawCircleV(sp, 4.0f, players[i].color);
    }
    #undef TO_RADAR
    DrawTriangle((Vector2){ (float)cx, cy - 7.0f }, (Vector2){ cx - 5.0f, cy + 5.0f }, (Vector2){ cx + 5.0f, cy + 5.0f }, WHITE);
}

// Mobile : vie et munitions en haut a gauche, pour laisser le bas aux pouces
static void DrawCompactStatus(const Player *p, int x, int y, bool showName)
{
    char buf[48];
    const Weapon *wp = &p->w;
    DrawRectangle(x - 6, y - 6, 212, 62, PANEL);
    Color hc = p->hp > 50 ? (Color){ 90, 220, 90, 255 } : (p->hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(x, y, 200, 12, (Color){ 40, 40, 40, 200 });
    DrawRectangle(x, y, (int)(200*p->hp/100.0f), 12, hc);
    if (showName)
    {
        snprintf(buf, sizeof(buf), "J%d", p->id + 1);
        TextShadow(buf, x + 172, y + 18, 16, p->color);
    }
    if (wp->active == WPN_HARPOON) snprintf(buf, sizeof(buf), "HARPON  %d / %d", wp->harpoonLoaded ? 1 : 0, wp->harpoons);
    else snprintf(buf, sizeof(buf), "HK416  %d / %d", wp->ammo, wp->reserve);
    TextShadow(buf, x, y + 18, 22, wp->active == WPN_HARPOON ? SKYBLUE : RAYWHITE);
}

void HudDrawPlayer(const Player *p, int w, int h, bool showName)
{
    bool split = (G.mode == MODE_SPLIT);
    int cx = w/2, cy = h/2;
    DrawDamageVignette(p, w, h);
    if (!p->alive)
    {
        DrawRectangle(0, 0, w, h, (Color){ 40, 0, 0, 120 });
        TextCenter("A TERRE !", cx, h/2 - 30, 48, (Color){ 255, 80, 60, 255 });
        TextCenter("Retour a la prochaine vague", cx, h/2 + 26, 20, RAYWHITE);
        return;
    }
    DrawCrosshair(p, cx, cy);
    if (TouchMode())
    {
        DrawRadar(p, 75, 80, 60);
        DrawCompactStatus(p, 18, 152, showName);
        if (p->w.active == WPN_RIFLE && p->w.reloading)
        {
            TextCenter("RECHARGEMENT...", cx, cy + 50, 20, RAYWHITE);
            DrawRectangle(cx - 80, cy + 76, (int)(160*p->w.reloadT/p->w.reloadDur), 6, AMMO_COLOR);
        }
        else if (p->w.active == WPN_HARPOON && p->w.harpoonT > 0)
        {
            TextCenter("RECHARGEMENT DU HARPON...", cx, cy + 50, 20, RAYWHITE);
            DrawRectangle(cx - 80, cy + 76, (int)(160*(1.0f - p->w.harpoonT/HARPOON_RELOAD)), 6, SKYBLUE);
        }
        return;
    }
    DrawHealth(p, h, showName);
    DrawAmmo(p, w, h, cx, cy);
    DrawRadar(p, 95, split ? 165 : 105, 75);   // en ecran partage, sous la barre de burgers
}

//----------------------------------------------------------------------------------
// HUD commun
//----------------------------------------------------------------------------------
static void DrawBurgerIcon(int x, int y, bool full)
{
    Color bun = full ? (Color){ 230, 160, 60, 255 } : (Color){ 90, 90, 90, 160 };
    Color meat = full ? (Color){ 100, 55, 25, 255 } : (Color){ 60, 60, 60, 160 };
    Color lettuce = full ? (Color){ 100, 200, 70, 255 } : (Color){ 70, 70, 70, 160 };
    DrawCircleSector((Vector2){ (float)x, y + 8.0f }, 11, 180, 360, 16, bun);
    DrawRectangle(x - 12, y + 8, 24, 3, lettuce);
    DrawRectangle(x - 11, y + 11, 22, 5, meat);
    DrawRectangleRounded((Rectangle){ x - 11.0f, y + 16.0f, 22, 5 }, 0.6f, 4, bun);
}

static void DrawBurgerBar(int cx)
{
    int bw = MAX_BURGERS*30;
    DrawRectangle(cx - bw/2 - 12, 8, bw + 24, 62, PANEL);
    TextCenter("BURGERS A PROTEGER", cx, 12, 16, G.burgers <= 3 ? (Color){ 255, 80, 60, 255 } : RAYWHITE);
    for (int i = 0; i < MAX_BURGERS; i++) DrawBurgerIcon(cx - bw/2 + 15 + i*30, 34, i < G.burgers);
}

static void DrawWaveBanners(int cx, int h)
{
    char buf[64];
    if (G.bannerT > 0)
    {
        float a = fminf(1.0f, G.bannerT);
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextCenter(buf, cx, h/4, 64, Fade(GOLD_TEXT, a));
        TextCenter(G.wave % 5 == 0 ? "ALERTE : LE ROI DU GRAS ARRIVE !" : "Les gloutons ont faim...", cx, h/4 + 70, 24, Fade(RAYWHITE, a));
    }
    if (G.inIntermission)
    {
        snprintf(buf, sizeof(buf), "VAGUE %d TERMINEE", G.wave);
        TextCenter(buf, cx, h/4, 52, (Color){ 120, 255, 120, 255 });
        TextCenter("+2 burgers  +90 munitions  +3 harpons  +25 PV", cx, h/4 + 60, 22, RAYWHITE);
        snprintf(buf, sizeof(buf), "Prochaine vague dans %d...", (int)ceilf(G.interT));
        TextCenter(buf, cx, h/4 + 92, 22, LIGHTGRAY);
    }
}

// Liste des joueurs en reseau (couleur, vie, eliminations)
static void DrawScoreboard(int x, int y)
{
    char buf[64];
    for (int i = 0; i < numPlayers; i++)
    {
        const Player *p = &players[i];
        if (!p->connected) continue;
        snprintf(buf, sizeof(buf), "J%d%s  %d elim.%s", i + 1, p->local ? " (vous)" : "", p->kills, p->alive ? "" : "  - A TERRE");
        DrawRectangle(x, y + 4, 10, 10, p->color);
        TextShadow(buf, x + 16, y, 18, p->alive ? RAYWHITE : GRAY);
        y += 22;
    }
}

void HudDrawShared(int w, int h, bool split)
{
    char buf[96];
    int cx = w/2;
    DrawBurgerBar(cx);

    int remaining = G.alive + G.toSpawn + (G.bossPending ? 1 : 0);
    if (split || TouchMode())
    {
        // Vague et score centres sous la barre, entre les deux vues
        snprintf(buf, sizeof(buf), "VAGUE %d  -  Ennemis : %d  -  Score : %d", G.wave, remaining, G.score);
        int tw = MeasureText(buf, 18);
        DrawRectangle(cx - tw/2 - 10, 72, tw + 20, 24, PANEL);
        TextCenter(buf, cx, 75, 18, GOLD_TEXT);
        if (TouchMode() && G.mode == MODE_NET) DrawScoreboard(18, 225);
    }
    else
    {
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextShadow(buf, 20, 195, 30, GOLD_TEXT);
        snprintf(buf, sizeof(buf), "Ennemis : %d", remaining);
        TextShadow(buf, 20, 228, 18, RAYWHITE);
        snprintf(buf, sizeof(buf), "SCORE  %d", G.score);
        TextShadow(buf, w - 20 - MeasureText(buf, 28), 16, 28, RAYWHITE);
        const Player *me = FirstLocalPlayer();
        snprintf(buf, sizeof(buf), "Eliminations %d  |  Headshots %d", me->kills, me->headshots);
        TextShadow(buf, w - 20 - MeasureText(buf, 16), 50, 16, LIGHTGRAY);
        if (G.mode == MODE_NET) DrawScoreboard(20, 256);
    }
    DrawFeed(w);
    DrawWaveBanners(cx, h);
}

//----------------------------------------------------------------------------------
// Ecrans (boutons partages entre dessin et clics, voir hud.h)
//----------------------------------------------------------------------------------
static Rectangle StackButton(int i, float top, float w, float h)
{
    return (Rectangle){ UiWidth()/2.0f - w/2, top + i*(h + 10), w, h };
}

int HudTitleChoices(TitleChoice *out)
{
    int n = 0;
    out[n++] = TITLE_SOLO;
    if (!TouchMode()) out[n++] = TITLE_SPLIT;   // pas d'ecran partage sur telephone
    out[n++] = TITLE_HOST;
    out[n++] = TITLE_JOIN;
    return n;
}

Rectangle HudTitleButton(int i) { return StackButton(i, UiHeight()*0.40f, 520, 48); }
Rectangle HudPauseButton(int i) { return StackButton(i, UiHeight()*0.52f, 420, 50); }
Rectangle HudOverButton(int i) { return StackButton(i, UiHeight()*0.74f, 360, 46); }
Rectangle HudLobbyButton(int i) { return StackButton(i, UiHeight() - 120.0f, 360, 46); }
Rectangle HudJoinButton(int i) { return StackButton(i, UiHeight()*0.75f, 300, 42); }

Rectangle HudJoinKey(int i)
{
    const float kw = 86, kh = 44, gap = 8;
    float x0 = UiWidth()/2.0f - (3*kw + 2*gap)/2;
    float y0 = UiHeight()*0.30f;
    return (Rectangle){ x0 + (i % 3)*(kw + gap), y0 + (i/3)*(kh + gap), kw, kh };
}

const char *HudJoinKeyLabel(int i)
{
    static const char *keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<" };
    return keys[i];
}

void HudDrawTitle(int w, int h)
{
    char buf[64];
    int cx = w/2, y = h/12;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 110 });
    TextCenter("BURGER RUSH", cx, y, 90, (Color){ 255, 200, 60, 255 });
    TextCenter("OPERATION HK416", cx, y + 92, 34, RAYWHITE);
    TextCenter("Des hordes de gloutons veulent devorer votre stand de burgers. Defendez-le !", cx, y + 134, 18, LIGHTGRAY);

    static const char *labels[] = { "SOLO", "ECRAN PARTAGE (2 joueurs)", "HEBERGER une partie reseau", "REJOINDRE une partie reseau" };
    static const char *keys[] = { "1", "2", "3", "4" };
    TitleChoice choices[4];
    int n = HudTitleChoices(choices);
    for (int i = 0; i < n; i++)
    {
        char label[80];
        if (TouchMode()) snprintf(label, sizeof(label), "%s", labels[choices[i]]);
        else snprintf(label, sizeof(label), "[%s]  %s", keys[choices[i]], labels[choices[i]]);
        UiDrawButton(HudTitleButton(i), label, i == 0 ? (Color){ 255, 200, 60, 255 } : RAYWHITE);
    }

    float ty = HudTitleButton(n - 1).y + 62;
    if (TouchMode())
    {
        TextCenter("Gauche : joystick pour bouger   -   Droite : glisser pour viser", cx, (int)ty, 17, LIGHTGRAY);
        TextCenter("FEU (glisser dessus pour viser), VISER, SAUT, RECH., HARPON : 1 coup = 1 glouton projete !", cx, (int)ty + 22, 17, LIGHTGRAY);
    }
    else
    {
        const char *lines[] = {
            "JOUEUR 1 : ZQSD/WASD, souris, clic G tirer, clic D viser, R recharger, 1/2/E/molette : HK416 / HARPON, MAJ sprint, ESPACE sauter",
            "JOUEUR 2 (manette) : sticks, RT tirer, LT viser, RB harpon, X recharger, A sauter, L3 sprint",
            "JOUEUR 2 (clavier) : IJKL bouger, fleches viser, CTRL D tirer, MAJ D viser, M harpon, U recharger, O sauter",
            "HARPON : tue et projette le glouton touche en un coup        ECHAP / P / START : pause",
        };
        for (int i = 0; i < 4; i++) TextCenter(lines[i], cx, (int)ty + i*22, 16, i == 3 ? SKYBLUE : LIGHTGRAY);
    }

    if (G.titleMsg[0]) TextCenter(G.titleMsg, cx, h - 70, 20, (Color){ 255, 120, 90, 255 });
    if (G.best > 0)
    {
        snprintf(buf, sizeof(buf), "Meilleur score : %d", G.best);
        TextCenter(buf, cx, h - 40, 20, LIGHTGRAY);
    }
}

void HudDrawPause(int w, int h, bool network)
{
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter(network ? "MENU" : "PAUSE", w/2, h/4, 70, RAYWHITE);
    if (network) TextCenter("La partie continue pour les autres joueurs !", w/2, h/4 + 80, 22, (Color){ 255, 200, 80, 255 });
    UiDrawButton(HudPauseButton(0), "REPRENDRE", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudPauseButton(1), network ? "QUITTER LA PARTIE" : "MENU PRINCIPAL", RAYWHITE);
    if (!TouchMode()) TextCenter("ECHAP / P / START : reprendre      Q : menu", w/2, (int)HudPauseButton(1).y + 66, 18, LIGHTGRAY);
}

void HudDrawGameOver(int w, int h)
{
    char buf[128];
    int cx = w/2, y = h/8;
    DrawRectangle(0, 0, w, h, (Color){ 60, 0, 0, 150 });
    TextCenter("GAME OVER", cx, y, 80, (Color){ 255, 70, 50, 255 });
    TextCenter(G.overReason, cx, y + 90, 26, RAYWHITE);
    snprintf(buf, sizeof(buf), "Vague atteinte : %d      Score : %d      Meilleur score : %d", G.wave, G.score, G.best);
    TextCenter(buf, cx, y + 140, 24, GOLD_TEXT);
    int line = 0;
    for (int i = 0; i < numPlayers; i++)
    {
        const Player *p = &players[i];
        if (!p->connected) continue;
        if (numPlayers == 1) snprintf(buf, sizeof(buf), "Eliminations : %d      Headshots : %d", p->kills, p->headshots);
        else snprintf(buf, sizeof(buf), "Joueur %d : %d eliminations, %d headshots", i + 1, p->kills, p->headshots);
        TextCenter(buf, cx, y + 180 + line*26, 20, numPlayers == 1 ? LIGHTGRAY : p->color);
        line++;
    }
    if (NetIsClient()) TextCenter("En attente de l'hote pour rejouer...", cx, (int)HudOverButton(0).y - 34, 20, RAYWHITE);
    else UiDrawButton(HudOverButton(0), "REJOUER", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudOverButton(1), "MENU", RAYWHITE);
}

void HudDrawJoin(int w, int h, const char *address)
{
    int cx = w/2;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter("REJOINDRE UNE PARTIE", cx, (int)(h*0.04f), 44, (Color){ 255, 200, 60, 255 });
    TextCenter("Adresse IP de l'hote (affichee sur son ecran) :", cx, (int)(h*0.13f), 20, LIGHTGRAY);

    int bw = 380;
    Rectangle box = { cx - bw/2.0f, h*0.18f, (float)bw, 44 };
    DrawRectangleRec(box, (Color){ 20, 20, 20, 220 });
    DrawRectangleLinesEx(box, 2, (Color){ 255, 200, 60, 255 });
    char shown[80];
    snprintf(shown, sizeof(shown), "%s%s", address, fmodf(G.time, 1.0f) < 0.5f ? "_" : "");
    TextCenter(shown, cx, (int)box.y + 9, 28, RAYWHITE);

    for (int i = 0; i < 12; i++) UiDrawButton(HudJoinKey(i), HudJoinKeyLabel(i), RAYWHITE);
    UiDrawButton(HudJoinButton(0), "SE CONNECTER", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudJoinButton(1), "RETOUR", RAYWHITE);

    NetStatus st = NetGetStatus();
    int sy = (int)(HudJoinKey(11).y + HudJoinKey(11).height + 8);
    if (NetGetRole() == NET_CLIENT && st == NET_CONNECTING) TextCenter("Connexion en cours...", cx, sy, 20, (Color){ 255, 220, 120, 255 });
    else if (NetGetRole() == NET_CLIENT && st == NET_CONNECTED) TextCenter("Connecte ! En attente de l'hote...", cx, sy, 20, GREEN);
    else if (NetError()[0]) TextCenter(NetError(), cx, sy, 20, (Color){ 255, 100, 80, 255 });
    TextCenter("Meme reseau Wi-Fi que l'hote (ou port UDP 27016 ouvert chez lui).", cx, h - 24, 16, GRAY);
}

void HudDrawLobby(int w, int h)
{
    char buf[96];
    int cx = w/2, y = h/12;
    bool host = NetGetRole() == NET_HOST;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter("PARTIE RESEAU", cx, y, 56, (Color){ 255, 200, 60, 255 });

    if (host)
    {
        char ips[4][40];
        int n = SockLocalIPs(ips, 4);
        TextCenter("Les autres joueurs choisissent REJOINDRE et tapent :", cx, y + 70, 20, LIGHTGRAY);
        for (int i = 0; i < n && i < 2; i++) TextCenter(ips[i], cx, y + 98 + i*34, 32, RAYWHITE);
        if (n == 0) TextCenter("(adresse IP introuvable)", cx, y + 98, 26, GRAY);
        y += 98 + (n > 0 ? (n < 2 ? n : 2) : 1)*34;
        snprintf(buf, sizeof(buf), "(port UDP %d)", NET_PORT);
        TextCenter(buf, cx, y, 16, GRAY);
        y += 28;
    }
    else y += 90;

    TextCenter("JOUEURS", cx, y, 24, GOLD_TEXT);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        const Player *p = &players[i];
        if (p->connected) snprintf(buf, sizeof(buf), "Joueur %d%s%s", i + 1, i == 0 ? " (hote)" : "", p->local ? " - vous" : "");
        else snprintf(buf, sizeof(buf), "Joueur %d : en attente...", i + 1);
        TextCenter(buf, cx, y + 30 + i*26, 20, p->connected ? p->color : GRAY);
    }
    if (host) UiDrawButton(HudLobbyButton(0), "LANCER LA PARTIE", (Color){ 255, 200, 60, 255 });
    else TextCenter("En attente du lancement par l'hote...", cx, (int)HudLobbyButton(0).y + 12, 20, RAYWHITE);
    UiDrawButton(HudLobbyButton(1), host ? "ANNULER" : "QUITTER", RAYWHITE);
}
