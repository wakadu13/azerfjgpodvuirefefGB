/*
 * hud.c - Tout ce qui est dessine en 2D par-dessus la scene.
 *
 * Contenu :
 *  - le fil d'actualite (messages courts en haut a droite) ;
 *  - le HUD personnel de chaque joueur (vie, munitions, reticule, radar) ;
 *  - le HUD commun (poussins restants, vague, score, annonces) ;
 *  - les ecrans de menu (titre, rejoindre, salle d'attente, pause, game over).
 *
 * Tout est dessine en unites virtuelles (entre UiBegin et UiEnd, voir ui.h) :
 * les coordonnees en dur ci-dessous (30, 260, h - 60...) restent donc valables
 * quelle que soit la resolution reelle de l'ecran.
 * Sur ecran tactile (TouchMode), la disposition change pour laisser le bas
 * de l'ecran libre pour les pouces (joystick et boutons virtuels).
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

// Un message du fil d'actualite : texte, temps restant a l'ecran (s) et couleur
typedef struct { char text[96]; float t; Color c; } FeedMsg;

static FeedMsg feed[MAX_MSGS];   // feed[0] = message le plus recent

// Couleurs reutilisees dans tout le HUD
static const Color GOLD_TEXT = { 255, 210, 80, 255 };    // titres, vague, score
static const Color AMMO_COLOR = { 230, 200, 120, 255 };  // cartouches du HK416
static const Color PANEL = { 0, 0, 0, 110 };             // fond noir translucide des encadres

//----------------------------------------------------------------------------------
// Fil d'actualite (eliminations, bonus, poussins enleves...)
//----------------------------------------------------------------------------------
void FeedClear(void) { memset(feed, 0, sizeof(feed)); }

// Ajoute un message en tete du fil ; le plus ancien est pousse dehors.
void FeedPush(Color c, const char *text)
{
    // Decale tous les messages d'une case vers le bas (le dernier est perdu)
    for (int i = MAX_MSGS - 1; i > 0; i--) feed[i] = feed[i - 1];
    snprintf(feed[0].text, sizeof(feed[0].text), "%s", text);
    feed[0].t = 4.0f;   // reste affiche 4 secondes
    feed[0].c = c;
}

// Version "printf" : formate le message, l'affiche et le confie au reseau
// pour que l'hote le retransmette aux clients.
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

// Comme FeedAdd, mais le message reste sur cette machine.
void FeedAddLocal(Color c, const char *fmt, ...)
{
    char text[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    FeedPush(c, text);
}

// Decompte du temps d'affichage de chaque message (t <= 0 : invisible).
void FeedUpdate(float dt)
{
    for (int i = 0; i < MAX_MSGS; i++) feed[i].t -= dt;
}

// Dessine le fil aligne a droite (w = largeur de l'ecran), une ligne par message.
static void DrawFeed(int w)
{
    for (int i = 0; i < MAX_MSGS; i++)
    {
        if (feed[i].t <= 0) continue;
        Color c = feed[i].c;
        c.a = (unsigned char)(255*fminf(1.0f, feed[i].t));   // fondu pendant la derniere seconde
        TextShadow(feed[i].text, w - 20 - MeasureText(feed[i].text, 18), 80 + i*24, 18, c);
    }
}

//----------------------------------------------------------------------------------
// HUD personnel
//----------------------------------------------------------------------------------
// Bords rouges de l'ecran : flash quand on prend un coup (hurtT), et
// pulsation continue quand la vie passe sous 35 PV.
static void DrawDamageVignette(const Player *p, int w, int h)
{
    if (p->hurtT <= 0 && p->hp >= 35) return;
    float a = p->hurtT*0.6f;   // intensite du flash de degat
    // Vie basse : pulsation (sinus) d'autant plus forte que la vie est faible
    if (p->hp < 35) a = fmaxf(a, (0.25f + 0.1f*sinf(G.time*5.0f))*(1.0f - p->hp/35.0f));
    Color red = { 200, 0, 0, (unsigned char)(Clamp(a, 0, 1)*255) }, clear = { 200, 0, 0, 0 };
    int b = h/4;   // epaisseur des degrades
    // Quatre degrades du bord (rouge) vers l'interieur (transparent)
    DrawRectangleGradientV(0, 0, w, b, red, clear);
    DrawRectangleGradientV(0, h - b, w, b, clear, red);
    DrawRectangleGradientH(0, 0, b, h, red, clear);
    DrawRectangleGradientH(w - b, 0, b, h, clear, red);
}

// Reticule au centre de la vue (cx, cy) et marqueur de touche (hitmarker).
static void DrawCrosshair(const Player *p, int cx, int cy)
{
    // Pas de reticule pendant le sprint (sprintT >= 0.5 : arme baissee)
    if (p->w.active == WPN_HARPOON && p->w.sprintT < 0.5f)
    {
        // Harpon : cercle + point
        // (estompe tant que la fleche n'est pas rechargee)
        Color c = p->w.harpoonLoaded ? WHITE : Fade(WHITE, 0.35f);
        DrawCircleLines(cx, cy, 11, c);
        DrawCircleLines(cx, cy, 12, Fade(BLACK, 0.4f));   // lisere sombre pour la lisibilite sur fond clair
        DrawRectangle(cx - 1, cy - 1, 3, 3, c);
    }
    // Reticule qui s'ecarte avec la dispersion ; masque au viseur
    // (ads = 0 hanche, 1 visee complete : le reticule disparait en visant)
    else if (p->w.ads < 0.6f && p->w.sprintT < 0.5f)
    {
        int gap = (int)(6 + WeaponSpread(p)*350), len = 9;   // ecart des branches, longueur
        Color c = Fade(WHITE, 1.0f - p->w.ads*1.6f);
        DrawRectangle(cx - gap - len, cy - 1, len, 2, c);   // branche gauche
        DrawRectangle(cx + gap, cy - 1, len, 2, c);         // droite
        DrawRectangle(cx - 1, cy - gap - len, 2, len, c);   // haut
        DrawRectangle(cx - 1, cy + gap, 2, len, c);         // bas
        DrawRectangle(cx - 1, cy - 1, 2, 2, c);             // point central
    }
    // Hitmarker : blanc = touche, orange = tete, rouge = elimine
    if (p->hitT > 0)
    {
        Color c = p->hitKill ? RED : (p->hitHead ? ORANGE : WHITE);
        c.a = (unsigned char)(255*fminf(1, p->hitT/0.1f));   // s'efface sur les 0,1 dernieres secondes
        const float a = 6, b = 14;   // distance au centre du debut et de la fin de chaque trait
        // Quatre traits en diagonale (un par coin : sx, sy = -1 ou +1)
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                DrawLineEx((Vector2){ cx + sx*b, cy + sy*b }, (Vector2){ cx + sx*a, cy + sy*a }, 2.5f, c);
    }
}

// Barre de vie en bas a gauche (version PC). h = hauteur de la vue.
static void DrawHealth(const Player *p, int h, bool showName)
{
    char buf[32];
    int x = 30, y = h - 60;   // coin haut gauche de la barre
    DrawRectangle(x - 4, y - 26, 268, 56, PANEL);
    // Titre de l'encadre : nom du joueur (en couleur) ou simplement "SANTE"
    if (showName)
    {
        snprintf(buf, sizeof(buf), "JOUEUR %d", p->id + 1);
        TextShadow(buf, x, y - 22, 16, p->color);
    }
    else TextShadow("SANTE", x, y - 22, 16, RAYWHITE);
    DrawRectangle(x, y, 260, 20, (Color){ 40, 40, 40, 200 });   // fond de la jauge
    // Couleur selon la vie : vert > 50, jaune > 25, rouge en dessous
    Color hc = p->hp > 50 ? (Color){ 90, 220, 90, 255 } : (p->hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(x, y, (int)(260*p->hp/100.0f), 20, hc);       // jauge (vie sur 100)
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(p->hp));         // arrondi au-dessus : jamais "0" si encore en vie
    TextShadow(buf, x + 220, y - 22, 18, RAYWHITE);
}

// Encadre de munitions quand le harpon est en main (bas a droite),
// plus l'indication de rechargement au centre de l'ecran.
static void DrawHarpoonAmmo(const Player *p, int w, int h, int cx, int cy)
{
    const Weapon *wp = &p->w;
    char buf[32];
    int x = w - 260, y = h - 86;
    DrawRectangle(x - 10, y - 6, 250, 82, PANEL);
    // Gros chiffre : fleche chargee (1) ou non (0, en rouge), puis "/ reserve"
    TextShadow(wp->harpoonLoaded ? "1" : "0", x, y, 56, wp->harpoonLoaded ? RAYWHITE : (Color){ 255, 90, 70, 255 });
    snprintf(buf, sizeof(buf), "/ %d", wp->harpoons);
    TextShadow(buf, x + MeasureText("0", 56) + 12, y + 26, 26, LIGHTGRAY);
    TextShadow("HARPON", x + 140, y + 4, 20, SKYBLUE);
    TextShadow("1 coup = 1 ennemi", x + 98, y + 30, 14, LIGHTGRAY);
    for (int i = 0; i < HARPOON_MAX; i++)   // une encoche par fleche en reserve
        DrawRectangle(x + i*18, y + 62, 14, 5, (i < wp->harpoons) ? SKYBLUE : (Color){ 80, 80, 80, 200 });

    // Rechargement en cours : texte + barre de progression sous le reticule
    if (wp->harpoonT > 0)
    {
        TextCenter("RECHARGEMENT DU HARPON...", cx, cy + 50, 20, RAYWHITE);
        DrawRectangle(cx - 80, cy + 76, 160, 6, (Color){ 0, 0, 0, 150 });
        // harpoonT decompte de HARPOON_RELOAD vers 0, d'ou le "1 - ..."
        DrawRectangle(cx - 80, cy + 76, (int)(160*(1.0f - wp->harpoonT/HARPOON_RELOAD)), 6, SKYBLUE);
    }
    else if (!wp->harpoonLoaded && wp->harpoons == 0) TextCenter("PLUS DE HARPONS ! (1 / E : HK416)", cx, cy + 50, 22, RED);
}

// Encadre de munitions (bas a droite) pour l'arme en main, et alertes au
// centre (rechargement, chargeur presque vide, plus de munitions).
// w, h : taille de la vue ; cx, cy : centre de la vue.
static void DrawAmmo(const Player *p, int w, int h, int cx, int cy)
{
    const Weapon *wp = &p->w;
    char buf[32];
    int x = w - 260, y = h - 86;
    if (wp->active == WPN_HARPOON) { DrawHarpoonAmmo(p, w, h, cx, cy); return; }
    DrawRectangle(x - 10, y - 6, 250, 82, PANEL);
    // Cartouches dans le chargeur (rouge quand il en reste 8 ou moins)
    snprintf(buf, sizeof(buf), "%d", wp->ammo);
    TextShadow(buf, x, y, 56, wp->ammo <= 8 ? (Color){ 255, 90, 70, 255 } : RAYWHITE);
    // "/ reserve" place juste apres le nombre (largeur d'1 ou 2 chiffres)
    snprintf(buf, sizeof(buf), "/ %d", wp->reserve);
    TextShadow(buf, x + MeasureText(wp->ammo >= 10 ? "00" : "0", 56) + 12, y + 26, 26, LIGHTGRAY);
    TextShadow("HK416", x + 150, y + 4, 20, AMMO_COLOR);
    TextShadow(wp->semi ? "SEMI" : "AUTO", x + 150, y + 28, 16, LIGHTGRAY);   // mode de tir
    // Une petite barre par cartouche du chargeur (pleine ou grisee)
    for (int i = 0; i < MAG_SIZE; i++)
        DrawRectangle(x + i*7, y + 62, 5, 9, (i < wp->ammo) ? AMMO_COLOR : (Color){ 80, 80, 80, 200 });

    if (wp->reloading)
    {
        // reloadT monte de 0 a reloadDur pendant le rechargement
        TextCenter("RECHARGEMENT...", cx, cy + 50, 20, RAYWHITE);
        DrawRectangle(cx - 80, cy + 76, 160, 6, (Color){ 0, 0, 0, 150 });
        DrawRectangle(cx - 80, cy + 76, (int)(160*wp->reloadT/wp->reloadDur), 6, AMMO_COLOR);
    }
    else if (wp->ammo == 0 && wp->reserve == 0) TextCenter("PLUS DE MUNITIONS !", cx, cy + 50, 22, RED);
    else if (wp->ammo <= 5) TextCenter("RECHARGER !", cx, cy + 50, 20, (Color){ 255, 200, 80, 255 });
}

// Radar circulaire de centre (cx, cy) et de rayon R : stand (jaune),
// ennemis (rouge, orange s'ils portent un poussin, plus gros pour le chef)
// et coequipiers (a leur couleur). Le joueur est le triangle au centre.
static void DrawRadar(const Player *p, int cx, int cy, int R)
{
    // Fond, contour et croix de reperage
    DrawCircle(cx, cy, (float)R, (Color){ 0, 0, 0, 120 });
    DrawCircleLines(cx, cy, (float)R, (Color){ 255, 255, 255, 120 });
    DrawLine(cx - R, cy, cx + R, cy, (Color){ 255, 255, 255, 40 });
    DrawLine(cx, cy - R, cx, cy + R, (Color){ 255, 255, 255, 40 });

    // Repere du joueur : l'avant est toujours vers le haut du radar
    // range = portee du radar en metres, k = pixels par metre
    const float range = 32.0f, k = R/range;
    // Vecteurs avant / droite du joueur dans le plan horizontal (x, z)
    Vector2 fwd = { sinf(p->yaw), cosf(p->yaw) }, right = { -cosf(p->yaw), sinf(p->yaw) };
    Vector2 center = { (float)cx, (float)cy };
    // Convertit une position du monde (wx, wz) en point du radar : on prend
    // la position relative au joueur, projetee sur ses axes droite / avant
    // (produits scalaires), mise a l'echelle ; l'axe Y de l'ecran descend, d'ou le "cy -".
    #define TO_RADAR(wx, wz) ((Vector2){ cx + (((wx) - p->pos.x)*right.x + ((wz) - p->pos.z)*right.y)*k, \
                                         cy - (((wx) - p->pos.x)*fwd.x + ((wz) - p->pos.z)*fwd.y)*k })

    Vector2 sp = TO_RADAR(0.0f, 0.0f);   // stand
    if (Vector2Distance(sp, center) < R) DrawRectangle((int)sp.x - 5, (int)sp.y - 3, 10, 6, (Color){ 255, 200, 40, 255 });
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        const Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        sp = TO_RADAR(e->pos.x, e->pos.z);
        if (Vector2Distance(sp, center) > R - 3) continue;   // hors de portee : pas affiche
        DrawCircleV(sp, e->type == E_BOSS ? 6.0f : 3.0f, e->state == ES_GRAB ? ORANGE : RED);
    }
    for (int i = 0; i < numPlayers; i++)   // coequipiers
    {
        if (&players[i] == p || !players[i].connected || !players[i].alive) continue;
        sp = TO_RADAR(players[i].pos.x, players[i].pos.z);
        if (Vector2Distance(sp, center) <= R - 3) DrawCircleV(sp, 4.0f, players[i].color);
    }
    #undef TO_RADAR
    // Le joueur lui-meme : triangle pointe vers le haut (= vers l'avant)
    DrawTriangle((Vector2){ (float)cx, cy - 7.0f }, (Vector2){ cx - 5.0f, cy + 5.0f }, (Vector2){ cx + 5.0f, cy + 5.0f }, WHITE);
}

// Mobile : vie et munitions en haut a gauche, pour laisser le bas aux pouces
// (x, y) = coin haut gauche de l'encadre.
static void DrawCompactStatus(const Player *p, int x, int y, bool showName)
{
    char buf[48];
    const Weapon *wp = &p->w;
    DrawRectangle(x - 6, y - 6, 212, 62, PANEL);
    // Jauge de vie (memes couleurs que DrawHealth)
    Color hc = p->hp > 50 ? (Color){ 90, 220, 90, 255 } : (p->hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(x, y, 200, 12, (Color){ 40, 40, 40, 200 });
    DrawRectangle(x, y, (int)(200*p->hp/100.0f), 12, hc);
    if (showName)
    {
        snprintf(buf, sizeof(buf), "J%d", p->id + 1);
        TextShadow(buf, x + 172, y + 18, 16, p->color);
    }
    // Une seule ligne de texte pour l'arme en main : "ARME  chargeur / reserve"
    if (wp->active == WPN_HARPOON) snprintf(buf, sizeof(buf), "HARPON  %d / %d", wp->harpoonLoaded ? 1 : 0, wp->harpoons);
    else snprintf(buf, sizeof(buf), "HK416  %d / %d", wp->ammo, wp->reserve);
    TextShadow(buf, x, y + 18, 22, wp->active == WPN_HARPOON ? SKYBLUE : RAYWHITE);
}

// HUD propre a un joueur, dessine dans sa vue de w x h unites virtuelles
// (tout l'ecran, ou une moitie en ecran partage).
void HudDrawPlayer(const Player *p, int w, int h, bool showName)
{
    bool split = (G.mode == MODE_SPLIT);
    int cx = w/2, cy = h/2;   // centre de la vue
    DrawDamageVignette(p, w, h);
    // Joueur a terre : voile rouge et message, pas de HUD de combat
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
        // Disposition tactile : radar et etat compact en haut a gauche ;
        // le bas de l'ecran est occupe par le joystick et les boutons (touch.c)
        DrawRadar(p, 75, 80, 60);
        DrawCompactStatus(p, 18, 152, showName);
        // Barre de rechargement sous le reticule (sans fond, plus discrete)
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
    // Disposition PC : vie en bas a gauche, munitions en bas a droite, radar en haut a gauche
    DrawHealth(p, h, showName);
    DrawAmmo(p, w, h, cx, cy);
    DrawRadar(p, 95, split ? 165 : 105, 75);   // en ecran partage, sous la barre de poussins
}

//----------------------------------------------------------------------------------
// HUD commun
//----------------------------------------------------------------------------------
// Petit poussin dessine avec des formes simples ; (x, y) = coin haut gauche
// approximatif. here = false : poussin perdu, dessine en gris.
static void DrawChickIcon(int x, int y, bool here)
{
    Color body = here ? (Color){ 255, 214, 50, 255 } : (Color){ 90, 90, 90, 160 };
    Color beak = here ? (Color){ 255, 140, 30, 255 } : (Color){ 70, 70, 70, 160 };
    DrawEllipse(x, y + 15, 11, 9, body);                               // corps
    DrawCircle(x + 5, y + 6, 7, body);                                 // tete
    // bec (triangle pointe vers la droite)
    DrawTriangle((Vector2){ x + 11.0f, y + 4.0f }, (Vector2){ x + 11.0f, y + 9.0f }, (Vector2){ x + 16.0f, y + 6.5f }, beak);
    DrawCircle(x + 7, y + 5, 1.6f, here ? BLACK : (Color){ 50, 50, 50, 160 });   // oeil
    DrawEllipse(x - 2, y + 15, 6, 4, here ? (Color){ 235, 185, 30, 255 } : (Color){ 80, 80, 80, 160 });   // aile
}

// Barre des poussins en haut, centree sur cx : une icone par poussin
// (jaune = encore la, gris = enleve). Titre en rouge quand il en reste 3 ou moins.
static void DrawChickBar(int cx)
{
    int bw = MAX_CHICKS*30;   // 30 unites par icone
    DrawRectangle(cx - bw/2 - 12, 8, bw + 24, 62, PANEL);
    TextCenter("POUSSINS A PROTEGER", cx, 12, 16, G.chicks <= 3 ? (Color){ 255, 80, 60, 255 } : RAYWHITE);
    for (int i = 0; i < MAX_CHICKS; i++) DrawChickIcon(cx - bw/2 + 15 + i*30, 34, i < G.chicks);
}

// Annonces au quart haut de l'ecran : debut de vague (s'estompe avec
// G.bannerT) et bilan pendant l'entracte entre deux vagues.
static void DrawWaveBanners(int cx, int h)
{
    char buf[64];
    if (G.bannerT > 0)
    {
        float a = fminf(1.0f, G.bannerT);   // opacite : fondu pendant la derniere seconde
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextCenter(buf, cx, h/4, 64, Fade(GOLD_TEXT, a));
        // Toutes les 5 vagues, un chef de guerre (boss) accompagne la vague
        TextCenter(G.wave % 5 == 0 ? "ALERTE : LE CHEF DE GUERRE ARRIVE !" : "Les mercenaires approchent...", cx, h/4 + 70, 24, Fade(RAYWHITE, a));
    }
    if (G.inIntermission)
    {
        snprintf(buf, sizeof(buf), "VAGUE %d TERMINEE", G.wave);
        TextCenter(buf, cx, h/4, 52, (Color){ 120, 255, 120, 255 });
        // Recompenses de fin de vague (texte informatif, appliquees par waves.c)
        TextCenter("+2 poussins  +90 munitions  +3 harpons  +25 PV", cx, h/4 + 60, 22, RAYWHITE);
        snprintf(buf, sizeof(buf), "Prochaine vague dans %d...", (int)ceilf(G.interT));
        TextCenter(buf, cx, h/4 + 92, 22, LIGHTGRAY);
    }
}

// Liste des joueurs en reseau (couleur, vie, eliminations)
// a partir de (x, y), une ligne de 22 unites par joueur connecte.
static void DrawScoreboard(int x, int y)
{
    char buf[64];
    for (int i = 0; i < numPlayers; i++)
    {
        const Player *p = &players[i];
        if (!p->connected) continue;
        snprintf(buf, sizeof(buf), "J%d%s  %d elim.%s", i + 1, p->local ? " (vous)" : "", p->kills, p->alive ? "" : "  - A TERRE");
        DrawRectangle(x, y + 4, 10, 10, p->color);   // pastille de couleur du joueur
        TextShadow(buf, x + 16, y, 18, p->alive ? RAYWHITE : GRAY);
        y += 22;
    }
}

// Informations communes a tous les joueurs, dessinees une seule fois
// sur tout l'ecran (w x h unites virtuelles).
void HudDrawShared(int w, int h, bool split)
{
    char buf[96];
    int cx = w/2;
    DrawChickBar(cx);

    // Ennemis restants dans la vague : en vie + a venir + le chef s'il n'est pas encore la
    int remaining = G.alive + G.toSpawn + (G.bossPending ? 1 : 0);
    if (split || TouchMode())
    {
        // Vague et score centres sous la barre, entre les deux vues
        // (sur mobile aussi : les coins sont pris par le radar et les commandes)
        snprintf(buf, sizeof(buf), "VAGUE %d  -  Ennemis : %d  -  Score : %d", G.wave, remaining, G.score);
        int tw = MeasureText(buf, 18);
        DrawRectangle(cx - tw/2 - 10, 72, tw + 20, 24, PANEL);
        TextCenter(buf, cx, 75, 18, GOLD_TEXT);
        if (TouchMode() && G.mode == MODE_NET) DrawScoreboard(18, 225);   // sous l'etat compact
    }
    else
    {
        // PC plein ecran : vague a gauche sous le radar, score en haut a droite
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextShadow(buf, 20, 195, 30, GOLD_TEXT);
        snprintf(buf, sizeof(buf), "Ennemis : %d", remaining);
        TextShadow(buf, 20, 228, 18, RAYWHITE);
        snprintf(buf, sizeof(buf), "SCORE  %d", G.score);
        TextShadow(buf, w - 20 - MeasureText(buf, 28), 16, 28, RAYWHITE);   // aligne a droite
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
// Les fonctions HudXxxButton(i) renvoient le rectangle du i-eme bouton d'un
// ecran. game.c les utilise pour tester les clics (UiButtonHit) et ce fichier
// pour les dessiner (UiDrawButton) : une seule definition de la position, donc
// impossible que la zone cliquable et le dessin se decalent.

// i-eme bouton d'une pile verticale centree horizontalement :
// top = haut du premier bouton, w x h = taille, 10 unites entre deux boutons.
static Rectangle StackButton(int i, float top, float w, float h)
{
    return (Rectangle){ UiWidth()/2.0f - w/2, top + i*(h + 10), w, h };
}

// Liste des choix du menu principal selon la plateforme ; out doit pouvoir
// contenir 4 elements. Renvoie le nombre de choix ecrits.
int HudTitleChoices(TitleChoice *out)
{
    int n = 0;
    out[n++] = TITLE_SOLO;
    if (!TouchMode()) out[n++] = TITLE_SPLIT;   // pas d'ecran partage sur telephone
#if !defined(BR_WEB)
    // Pas de reseau dans le navigateur : il n'autorise pas les sockets UDP
    out[n++] = TITLE_HOST;
    out[n++] = TITLE_JOIN;
#endif
    return n;
}

// Positions des boutons de chaque ecran (hauteur de depart en fraction de
// la hauteur virtuelle, pour s'adapter aux ecrans plus ou moins hauts)
Rectangle HudTitleButton(int i) { return StackButton(i, UiHeight()*0.40f, 520, 48); }
Rectangle HudPauseButton(int i) { return StackButton(i, UiHeight()*0.52f, 420, 50); }
Rectangle HudOverButton(int i) { return StackButton(i, UiHeight()*0.74f, 360, 46); }
Rectangle HudLobbyButton(int i) { return StackButton(i, UiHeight() - 120.0f, 360, 46); }
Rectangle HudJoinButton(int i) { return StackButton(i, UiHeight()*0.75f, 300, 42); }

// Touche i du pave numerique de l'ecran REJOINDRE : grille de 3 colonnes
// sur 4 lignes, centree (i % 3 = colonne, i / 3 = ligne).
Rectangle HudJoinKey(int i)
{
    const float kw = 86, kh = 44, gap = 8;   // taille d'une touche et espacement
    float x0 = UiWidth()/2.0f - (3*kw + 2*gap)/2;
    float y0 = UiHeight()*0.30f;
    return (Rectangle){ x0 + (i % 3)*(kw + gap), y0 + (i/3)*(kh + gap), kw, kh };
}

// Texte de la touche i du pave (meme ordre qu'un pave de telephone).
const char *HudJoinKeyLabel(int i)
{
    static const char *keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<" };
    return keys[i];
}

// Menu principal : titre, boutons, rappel des commandes, message eventuel
// et meilleur score.
void HudDrawTitle(int w, int h)
{
    char buf[64];
    int cx = w/2, y = h/12;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 110 });   // assombrit la scene 3D derriere
    // Titre en 90 px, plus petit si l'ecran est etroit (tablette 4:3...) pour ne pas deborder
    int titleSize = (int)fminf(90.0f, w*0.072f);
    TextCenter("ZEFFALIE COME BACK", cx, y + (90 - titleSize)/2, titleSize, (Color){ 255, 200, 60, 255 });
    TextCenter("OPERATION HK416", cx, y + 92, 34, RAYWHITE);
    TextCenter("Des mercenaires enchaines veulent enlever vos poussins. Protegez-les !", cx, y + 134, 18, LIGHTGRAY);

    // Libelles et raccourcis indexes par TitleChoice
    static const char *labels[] = { "SOLO", "ECRAN PARTAGE (2 joueurs)", "HEBERGER une partie reseau", "REJOINDRE une partie reseau" };
    static const char *keys[] = { "1", "2", "3", "4" };
    TitleChoice choices[4];
    int n = HudTitleChoices(choices);
    for (int i = 0; i < n; i++)
    {
        char label[80];
        // Sur ecran tactile, pas de raccourci clavier a afficher
        if (TouchMode()) snprintf(label, sizeof(label), "%s", labels[choices[i]]);
        else snprintf(label, sizeof(label), "[%s]  %s", keys[choices[i]], labels[choices[i]]);
        UiDrawButton(HudTitleButton(i), label, i == 0 ? (Color){ 255, 200, 60, 255 } : RAYWHITE);   // SOLO mis en avant
    }

    // Aide sur les commandes, juste sous le dernier bouton
    float ty = HudTitleButton(n - 1).y + 62;
    if (TouchMode())
    {
        TextCenter("Gauche : joystick pour bouger   -   Droite : glisser pour viser", cx, (int)ty, 17, LIGHTGRAY);
        TextCenter("FEU (glisser dessus pour viser), VISER, SAUT, RECH., HARPON : 1 coup = 1 ennemi projete !", cx, (int)ty + 22, 17, LIGHTGRAY);
    }
    else
    {
        const char *lines[] = {
            "JOUEUR 1 : ZQSD/WASD, souris, clic G tirer, clic D viser, R recharger, 1/2/E/molette : HK416 / HARPON, MAJ sprint, ESPACE sauter",
            "JOUEUR 2 (manette) : sticks, RT tirer, LT viser, RB harpon, X recharger, A sauter, L3 sprint",
            "JOUEUR 2 (clavier) : IJKL bouger, fleches viser, CTRL D tirer, MAJ D viser, M harpon, U recharger, O sauter",
            "HARPON : tue et projette le mercenaire touche en un coup        ECHAP / P / START : pause",
        };
        for (int i = 0; i < 4; i++) TextCenter(lines[i], cx, (int)ty + i*22, 16, i == 3 ? SKYBLUE : LIGHTGRAY);
    }

    // Message d'erreur / d'information (ex : connexion perdue), en bas
    if (G.titleMsg[0]) TextCenter(G.titleMsg, cx, h - 70, 20, (Color){ 255, 120, 90, 255 });
    if (G.best > 0)
    {
        snprintf(buf, sizeof(buf), "Meilleur score : %d", G.best);
        TextCenter(buf, cx, h - 40, 20, LIGHTGRAY);
    }
}

// Ecran de pause. network = true : menu en partie reseau, qui ne met pas
// le jeu en pause (titre "MENU" et avertissement).
void HudDrawPause(int w, int h, bool network)
{
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter(network ? "MENU" : "PAUSE", w/2, h/4, 70, RAYWHITE);
    if (network) TextCenter("La partie continue pour les autres joueurs !", w/2, h/4 + 80, 22, (Color){ 255, 200, 80, 255 });
    UiDrawButton(HudPauseButton(0), "REPRENDRE", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudPauseButton(1), network ? "QUITTER LA PARTIE" : "MENU PRINCIPAL", RAYWHITE);
    // Rappel des touches (inutile sur ecran tactile)
    if (!TouchMode()) TextCenter("ECHAP / P / START : reprendre      Q : menu", w/2, (int)HudPauseButton(1).y + 66, 18, LIGHTGRAY);
}

// Ecran de fin : raison, resultats globaux, statistiques par joueur et boutons.
void HudDrawGameOver(int w, int h)
{
    char buf[128];
    int cx = w/2, y = h/8;
    DrawRectangle(0, 0, w, h, (Color){ 60, 0, 0, 150 });   // voile rouge sombre
    TextCenter("GAME OVER", cx, y, 80, (Color){ 255, 70, 50, 255 });
    TextCenter(G.overReason, cx, y + 90, 26, RAYWHITE);
    snprintf(buf, sizeof(buf), "Vague atteinte : %d      Score : %d      Meilleur score : %d", G.wave, G.score, G.best);
    TextCenter(buf, cx, y + 140, 24, GOLD_TEXT);
    // Une ligne de statistiques par joueur connecte (formulation differente en solo)
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
    // Un client ne peut pas relancer la partie : seul l'hote decide
    if (NetIsClient()) TextCenter("En attente de l'hote pour rejouer...", cx, (int)HudOverButton(0).y - 34, 20, RAYWHITE);
    else UiDrawButton(HudOverButton(0), "REJOUER", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudOverButton(1), "MENU", RAYWHITE);
}

// Ecran REJOINDRE : champ d'adresse, pave numerique, boutons et etat de la connexion.
// address = texte saisi jusqu'ici (tenu par game.c).
void HudDrawJoin(int w, int h, const char *address)
{
    int cx = w/2;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter("REJOINDRE UNE PARTIE", cx, (int)(h*0.04f), 44, (Color){ 255, 200, 60, 255 });
    TextCenter("Adresse IP de l'hote (affichee sur son ecran) :", cx, (int)(h*0.13f), 20, LIGHTGRAY);

    // Champ de saisie
    int bw = 380;
    Rectangle box = { cx - bw/2.0f, h*0.18f, (float)bw, 44 };
    DrawRectangleRec(box, (Color){ 20, 20, 20, 220 });
    DrawRectangleLinesEx(box, 2, (Color){ 255, 200, 60, 255 });
    char shown[80];
    // Curseur "_" qui clignote (visible une demi-seconde sur deux)
    snprintf(shown, sizeof(shown), "%s%s", address, fmodf(G.time, 1.0f) < 0.5f ? "_" : "");
    TextCenter(shown, cx, (int)box.y + 9, 28, RAYWHITE);

    for (int i = 0; i < 12; i++) UiDrawButton(HudJoinKey(i), HudJoinKeyLabel(i), RAYWHITE);
    UiDrawButton(HudJoinButton(0), "SE CONNECTER", (Color){ 255, 200, 60, 255 });
    UiDrawButton(HudJoinButton(1), "RETOUR", RAYWHITE);

    // Etat de la connexion, juste sous le pave (sy = bas de la derniere touche)
    NetStatus st = NetGetStatus();
    int sy = (int)(HudJoinKey(11).y + HudJoinKey(11).height + 8);
    if (NetGetRole() == NET_CLIENT && st == NET_CONNECTING) TextCenter("Connexion en cours...", cx, sy, 20, (Color){ 255, 220, 120, 255 });
    else if (NetGetRole() == NET_CLIENT && st == NET_CONNECTED) TextCenter("Connecte ! En attente de l'hote...", cx, sy, 20, GREEN);
    else if (NetError()[0]) TextCenter(NetError(), cx, sy, 20, (Color){ 255, 100, 80, 255 });
    TextCenter("Meme reseau Wi-Fi que l'hote (ou port UDP 27016 ouvert chez lui).", cx, h - 24, 16, GRAY);
}

// Salle d'attente reseau. L'hote voit ses adresses IP (a communiquer aux
// autres joueurs) et le bouton LANCER ; un client attend le lancement.
void HudDrawLobby(int w, int h)
{
    char buf[96];
    int cx = w/2, y = h/12;   // y = curseur vertical, avance au fil des lignes
    bool host = NetGetRole() == NET_HOST;
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 150 });
    TextCenter("PARTIE RESEAU", cx, y, 56, (Color){ 255, 200, 60, 255 });

    if (host)
    {
        // Adresses IP locales de cette machine (2 au maximum affichees)
        char ips[4][40];
        int n = SockLocalIPs(ips, 4);
        TextCenter("Les autres joueurs choisissent REJOINDRE et tapent :", cx, y + 70, 20, LIGHTGRAY);
        for (int i = 0; i < n && i < 2; i++) TextCenter(ips[i], cx, y + 98 + i*34, 32, RAYWHITE);
        if (n == 0) TextCenter("(adresse IP introuvable)", cx, y + 98, 26, GRAY);
        // Descend d'autant de lignes que d'adresses affichees (1 ligne si aucune)
        y += 98 + (n > 0 ? (n < 2 ? n : 2) : 1)*34;
        snprintf(buf, sizeof(buf), "(port UDP %d)", NET_PORT);
        TextCenter(buf, cx, y, 16, GRAY);
        y += 28;
    }
    else y += 90;

    // Les MAX_PLAYERS emplacements, occupes ou libres (le 0 est toujours l'hote)
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
