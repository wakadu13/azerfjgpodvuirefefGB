/*
 * hud.c - Tout ce qui est dessine en 2D par-dessus la scene.
 */
#include "hud.h"
#include "game.h"
#include "enemy.h"
#include "waves.h"
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

void FeedAdd(Color c, const char *fmt, ...)
{
    for (int i = MAX_MSGS - 1; i > 0; i--) feed[i] = feed[i - 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(feed[0].text, sizeof(feed[0].text), fmt, args);
    va_end(args);
    feed[0].t = 4.0f;
    feed[0].c = c;
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
    // Reticule qui s'ecarte avec la dispersion ; masque au viseur
    if (p->w.ads < 0.6f && p->w.sprintT < 0.5f)
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

static void DrawHealth(const Player *p, int h, bool split)
{
    char buf[32];
    int x = 30, y = h - 60;
    DrawRectangle(x - 4, y - 26, 268, 56, PANEL);
    if (split) TextShadow(p->id == 0 ? "JOUEUR 1" : "JOUEUR 2", x, y - 22, 16, p->color);
    else TextShadow("SANTE", x, y - 22, 16, RAYWHITE);
    DrawRectangle(x, y, 260, 20, (Color){ 40, 40, 40, 200 });
    Color hc = p->hp > 50 ? (Color){ 90, 220, 90, 255 } : (p->hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(x, y, (int)(260*p->hp/100.0f), 20, hc);
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(p->hp));
    TextShadow(buf, x + 220, y - 22, 18, RAYWHITE);
}

static void DrawAmmo(const Player *p, int w, int h, int cx, int cy)
{
    const Weapon *wp = &p->w;
    char buf[32];
    int x = w - 260, y = h - 86;
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
    for (int i = 0; i < numPlayers; i++)   // coequipier
    {
        if (&players[i] == p || !players[i].alive) continue;
        sp = TO_RADAR(players[i].pos.x, players[i].pos.z);
        if (Vector2Distance(sp, center) <= R - 3) DrawCircleV(sp, 4.0f, players[i].color);
    }
    #undef TO_RADAR
    DrawTriangle((Vector2){ (float)cx, cy - 7.0f }, (Vector2){ cx - 5.0f, cy + 5.0f }, (Vector2){ cx + 5.0f, cy + 5.0f }, WHITE);
}

void HudDrawPlayer(const Player *p, int w, int h, bool split)
{
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
    DrawHealth(p, h, split);
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
        TextCenter("+2 burgers  +90 munitions  +25 PV", cx, h/4 + 60, 22, RAYWHITE);
        snprintf(buf, sizeof(buf), "Prochaine vague dans %d...", (int)ceilf(G.interT));
        TextCenter(buf, cx, h/4 + 92, 22, LIGHTGRAY);
    }
}

void HudDrawShared(int w, int h, bool split)
{
    char buf[96];
    int cx = w/2;
    DrawBurgerBar(cx);

    int remaining = G.alive + G.toSpawn + (G.bossPending ? 1 : 0);
    if (split)
    {
        // Vague et score centres sous la barre, entre les deux vues
        snprintf(buf, sizeof(buf), "VAGUE %d  -  Ennemis : %d  -  Score : %d", G.wave, remaining, G.score);
        int tw = MeasureText(buf, 18);
        DrawRectangle(cx - tw/2 - 10, 72, tw + 20, 24, PANEL);
        TextCenter(buf, cx, 75, 18, GOLD_TEXT);
    }
    else
    {
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextShadow(buf, 20, 195, 30, GOLD_TEXT);
        snprintf(buf, sizeof(buf), "Ennemis : %d", remaining);
        TextShadow(buf, 20, 228, 18, RAYWHITE);
        snprintf(buf, sizeof(buf), "SCORE  %d", G.score);
        TextShadow(buf, w - 20 - MeasureText(buf, 28), 16, 28, RAYWHITE);
        snprintf(buf, sizeof(buf), "Eliminations %d  |  Headshots %d", players[0].kills, players[0].headshots);
        TextShadow(buf, w - 20 - MeasureText(buf, 16), 50, 16, LIGHTGRAY);
    }
    DrawFeed(w);
    DrawWaveBanners(cx, h);
}

//----------------------------------------------------------------------------------
// Ecrans
//----------------------------------------------------------------------------------
void HudDrawTitle(int w, int h)
{
    char buf[64];
    int cx = w/2, y = h/7;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 110 });
    TextCenter("BURGER RUSH", cx, y, 90, (Color){ 255, 200, 60, 255 });
    TextCenter("OPERATION HK416", cx, y + 95, 36, RAYWHITE);
    TextCenter("Des hordes de gloutons veulent devorer votre stand de burgers.", cx, y + 150, 22, LIGHTGRAY);
    TextCenter("Defendez-le avec votre HK416. Ne les laissez pas tout manger !", cx, y + 178, 22, LIGHTGRAY);

    bool blink = fmodf(G.time, 1.0f) < 0.65f;
    TextCenter("[1] ou ENTREE : SOLO", cx, y + 228, 30, blink ? (Color){ 255, 220, 120, 255 } : RAYWHITE);
    TextCenter("[2] : COOP LOCALE (ecran partage)", cx, y + 264, 30, RAYWHITE);

    const char *lines[] = {
        "JOUEUR 1 : ZQSD/WASD, souris, clic G tirer, clic D viser, R recharger, B mode, MAJ sprint, ESPACE sauter",
        "JOUEUR 2 (manette) : sticks, RT tirer, LT viser, X recharger, A sauter, L3 sprint, Y mode",
        "JOUEUR 2 (clavier) : IJKL bouger, fleches viser, CTRL D tirer, MAJ D viser, U recharger, O sauter, H sprint",
        "ECHAP / P / START : pause",
    };
    for (int i = 0; i < 4; i++) TextCenter(lines[i], cx, y + 320 + i*26, 18, i == 0 ? RAYWHITE : LIGHTGRAY);

    if (G.best > 0)
    {
        snprintf(buf, sizeof(buf), "Meilleur score : %d", G.best);
        TextCenter(buf, cx, h - 50, 22, LIGHTGRAY);
    }
}

void HudDrawPause(int w, int h)
{
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter("PAUSE", w/2, h/3, 70, RAYWHITE);
    TextCenter("ECHAP / clic / START : reprendre      Q : menu principal", w/2, h/3 + 90, 24, LIGHTGRAY);
}

void HudDrawGameOver(int w, int h)
{
    char buf[128];
    int cx = w/2, y = h/5;
    DrawRectangle(0, 0, w, h, (Color){ 60, 0, 0, 150 });
    TextCenter("GAME OVER", cx, y, 90, (Color){ 255, 70, 50, 255 });
    TextCenter(G.overReason, cx, y + 100, 28, RAYWHITE);
    snprintf(buf, sizeof(buf), "Vague atteinte : %d      Score : %d      Meilleur score : %d", G.wave, G.score, G.best);
    TextCenter(buf, cx, y + 160, 28, GOLD_TEXT);
    for (int i = 0; i < numPlayers; i++)
    {
        const Player *p = &players[i];
        if (numPlayers == 1) snprintf(buf, sizeof(buf), "Eliminations : %d      Headshots : %d", p->kills, p->headshots);
        else snprintf(buf, sizeof(buf), "Joueur %d : %d eliminations, %d headshots", i + 1, p->kills, p->headshots);
        TextCenter(buf, cx, y + 205 + i*30, 22, numPlayers == 1 ? LIGHTGRAY : p->color);
    }
    TextCenter("R / ENTREE : rejouer      ECHAP : menu", cx, y + 290, 24, RAYWHITE);
}
