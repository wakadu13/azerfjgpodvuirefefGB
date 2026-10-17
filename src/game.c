/*
 * game.c - Machine a etats (titre / reseau / partie / pause / game over)
 * et rendu.
 *
 * Trois facons de jouer :
 *  - solo : un joueur, plein ecran ;
 *  - ecran partage : deux joueurs locaux, chacun rendu dans sa propre
 *    RenderTexture (moitie gauche / droite) ;
 *  - reseau : un joueur par PC. L'hote simule la partie, les clients
 *    affichent l'etat recu (voir net.c).
 */
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "pickup.h"
#include "waves.h"
#include "hud.h"
#include "net.h"
#include "harpoon.h"
#include "touch.h"
#include "ui.h"
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Game G;

static Camera3D titleCam;
static RenderTexture2D views[2];
static int viewW, viewH;
static char joinAddress[64] = "192.168.1.";
static bool netMenu;   // menu ouvert en reseau (la partie continue)

// Souris capturee pour viser (sans objet sur ecran tactile)
static void CaptureCursor(bool on)
{
    if (TouchMode()) return;
    if (on) DisableCursor();
    else EnableCursor();
}

void GameInit(void)
{
    MapBuild();
    G.state = ST_TITLE;
    G.burgers = START_BURGERS;
    // Tests automatiques (emulateurs dans GitHub Actions) : partie solo directe
    if (getenv("BR_AUTOSTART")) GameStart(MODE_SOLO);
}

void GameClose(void)
{
    NetStop();
    for (int i = 0; i < 2; i++) if (views[i].id) UnloadRenderTexture(views[i]);
}

static void ClearWorld(void)
{
    EnemiesClear();
    EffectsClear();
    PickupsClear();
    HarpoonsClear();
}

void GameStart(GameMode mode)
{
    ClearWorld();
    FeedClear();

    Game keep = G;
    memset(&G, 0, sizeof(G));
    G.best = keep.best;
    G.time = keep.time;
    G.round = keep.round + 1;
    G.mode = mode;
    G.burgers = START_BURGERS;
    G.state = ST_PLAY;
    netMenu = false;

    if (mode == MODE_SOLO) PlayersReset(1);
    else if (mode == MODE_SPLIT) PlayersReset(2);
    else PlayersRestart();
    WavesStart(1);
    TouchReset();
    CaptureCursor(true);
}

void GameOver(const char *reason)
{
    if (G.state != ST_PLAY) return;
    G.state = ST_OVER;
    snprintf(G.overReason, sizeof(G.overReason), "%s", reason);
    if (G.score > G.best) G.best = G.score;
    CaptureCursor(false);
}

static void BackToTitle(const char *message)
{
    NetStop();
    ClearWorld();
    G.state = ST_TITLE;
    G.mode = MODE_SOLO;
    G.titleT = 0;
    G.burgers = START_BURGERS;
    snprintf(G.titleMsg, sizeof(G.titleMsg), "%s", message ? message : "");
    netMenu = false;
    CaptureCursor(false);
}

//----------------------------------------------------------------------------------
// Reseau
//----------------------------------------------------------------------------------
static void HostLobby(void)
{
    if (!NetHost()) { snprintf(G.titleMsg, sizeof(G.titleMsg), "%s", NetError()); return; }
    ClearWorld();
    FeedClear();
    G.mode = MODE_NET;
    G.state = ST_LOBBY;
    G.titleMsg[0] = '\0';
    PlayersResetNet(0);
}

void GameNetStateChanged(GameState previous)
{
    // Cote client : l'etat de la partie vient de changer chez l'hote
    G.mode = MODE_NET;
    CaptureCursor(G.state == ST_PLAY && !netMenu);
    if (G.state == ST_OVER && previous != ST_OVER && G.score > G.best) G.best = G.score;
}

void GameNetRoundStarted(void)
{
    // Cote client : l'hote a (re)lance une partie
    EffectsClear();
    HarpoonsClear();
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].predictT = 0;
    Player *me = &players[NetLocalId()];
    WeaponReset(&me->w);
    me->alive = true;
}

//----------------------------------------------------------------------------------
// Mise a jour
//----------------------------------------------------------------------------------
static bool AnyGamepadPressed(int button)
{
    for (int i = 0; i < 4; i++) if (IsGamepadAvailable(i) && IsGamepadButtonPressed(i, button)) return true;
    return false;
}

static bool MenuPressed(void)
{
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || IsKeyPressed(KEY_BACK) ||
           AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT) || (TouchMode() && TouchPausePressed());
}

static void UpdateTitleCamera(float dt)
{
    // Camera qui tourne lentement autour du stand
    G.titleT += dt;
    float a = G.titleT*0.15f;
    titleCam.position = V3(sinf(a)*16.0f, 7.0f, cosf(a)*16.0f);
    titleCam.target = V3(0, 2.0f, 0);
    titleCam.up = V3(0, 1, 0);
    titleCam.fovy = 60.0f;
    titleCam.projection = CAMERA_PERSPECTIVE;
}

static void ChooseTitle(TitleChoice choice)
{
    switch (choice)
    {
        case TITLE_SOLO:  GameStart(MODE_SOLO); break;
        case TITLE_SPLIT: GameStart(MODE_SPLIT); break;
        case TITLE_HOST:  HostLobby(); break;
        case TITLE_JOIN:  G.state = ST_JOIN; G.titleMsg[0] = '\0'; break;
    }
}

static void UpdateTitle(void)
{
    TitleChoice choices[4];
    int n = HudTitleChoices(choices);
    for (int i = 0; i < n; i++) if (UiButtonHit(HudTitleButton(i))) { ChooseTitle(choices[i]); return; }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) ||
        AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) ChooseTitle(TITLE_SOLO);
    else if (!TouchMode() && (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT))) ChooseTitle(TITLE_SPLIT);
    else if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) ChooseTitle(TITLE_HOST);
    else if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) ChooseTitle(TITLE_JOIN);
    else if (IsKeyPressed(KEY_ESCAPE)) G.quit = true;
}

static void UpdateJoin(void)
{
    // Saisie de l'adresse puis attente de la reponse de l'hote
    bool waiting = NetGetRole() == NET_CLIENT && NetGetStatus() != NET_FAILED;
    if (!waiting)
    {
        int len = (int)strlen(joinAddress);
        int cap = (int)sizeof(joinAddress) - 1;
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
            if (c > 32 && c < 127 && len < cap) { joinAddress[len++] = (char)c; joinAddress[len] = '\0'; }
        bool erase = (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE));
        for (int i = 0; i < 12; i++)   // pave numerique (indispensable sur telephone)
        {
            if (!UiButtonHit(HudJoinKey(i))) continue;
            const char *key = HudJoinKeyLabel(i);
            if (key[0] == '<') erase = true;
            else if (len < cap) { joinAddress[len++] = key[0]; joinAddress[len] = '\0'; }
        }
        if (erase && len > 0) joinAddress[len - 1] = '\0';
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || UiButtonHit(HudJoinButton(0))) NetJoin(joinAddress);
    }
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudJoinButton(1))) BackToTitle(NULL);
}

static void UpdateLobby(void)
{
    bool host = NetGetRole() == NET_HOST;
    if (host && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || UiButtonHit(HudLobbyButton(0)))) GameStart(MODE_NET);
    else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudLobbyButton(1))) BackToTitle(NULL);
}

static void UpdateWorld(float dt)
{
    PlayersReadInput(dt);
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->connected) continue;
        if (p->local)
        {
            if (netMenu) p->in = (PlayerInput){ 0 };   // menu ouvert : le joueur ne bouge plus
            PlayerUpdate(p, dt);
            WeaponUpdate(p, dt);
        }
        else PlayerUpdateRemote(p, dt);
    }

    if (NetIsAuthority())
    {
        PickupsUpdate(dt);
        EnemiesUpdate(dt);
        WavesUpdate(dt);
    }
    else
    {
        PickupsAnimate(dt);
        EnemiesClientUpdate(dt);
    }
    HarpoonsUpdate(dt);
    EffectsUpdate(dt);
    FeedUpdate(dt);
    for (int i = 0; i < numPlayers; i++) if (players[i].local) PlayerUpdateCamera(&players[i], dt);
    G.bannerT -= dt;
}

static void UpdatePlay(float dt)
{
    if (TouchMode()) TouchUpdate();
    if (G.mode == MODE_NET)
    {
        // En reseau, le menu ne met pas la partie en pause
        bool toggle = MenuPressed() || (netMenu && UiButtonHit(HudPauseButton(0)));
        if (toggle)
        {
            netMenu = !netMenu;
            TouchReset();
            CaptureCursor(!netMenu);
        }
        if (netMenu && (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A) || UiButtonHit(HudPauseButton(1)))) { BackToTitle(NULL); return; }
    }
    else if (MenuPressed()) { G.state = ST_PAUSE; CaptureCursor(false); return; }

    UpdateWorld(dt);
}

void GameUpdate(float dt)
{
    G.time += dt;
    NetReceive(dt);

    // Client : connexion perdue ou refusee
    if (NetIsClient() && G.state != ST_JOIN && (NetGetStatus() == NET_LOST || NetGetStatus() == NET_FAILED))
    {
        BackToTitle(NetError());
        return;
    }

    switch (G.state)
    {
        case ST_TITLE:
            UpdateTitleCamera(dt);
            UpdateTitle();
            break;
        case ST_JOIN:
            UpdateTitleCamera(dt);
            UpdateJoin();
            break;
        case ST_LOBBY:
            UpdateTitleCamera(dt);
            UpdateLobby();
            break;
        case ST_PLAY:
            UpdatePlay(dt);
            break;
        case ST_PAUSE:
            if (TouchMode()) TouchUpdate();
            if (MenuPressed() || UiButtonHit(HudPauseButton(0))) { G.state = ST_PLAY; TouchReset(); CaptureCursor(true); }
            else if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A) || UiButtonHit(HudPauseButton(1))) BackToTitle(NULL);
            break;
        case ST_OVER:
            EffectsUpdate(dt);
            for (int i = 0; i < MAX_ENEMIES; i++)   // les gloutons continuent de se dandiner
                if (enemies[i].active && enemies[i].state == ES_WALK) enemies[i].phase += dt*3.0f;
            if (NetIsAuthority() && (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER) ||
                                     AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || UiButtonHit(HudOverButton(0))))
                GameStart(G.mode);
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudOverButton(1))) BackToTitle(NULL);
            break;
    }

    NetSend(dt);
}

//----------------------------------------------------------------------------------
// Rendu
//----------------------------------------------------------------------------------
// Scene 3D vue depuis cam. viewer = joueur dont c'est la vue (pas dessine),
// withWeapon = ajoute ses mains et son arme au premier plan.
static void RenderView(Camera3D cam, const Player *viewer, bool withWeapon, int w, int h)
{
    bool inGame = (G.state == ST_PLAY || G.state == ST_PAUSE || G.state == ST_OVER);
    DrawSky(w, h);

    // Le flash de bouche d'un joueur qui tire eclaire la scene
    Vector3 flashPos = V3(0, -100, 0);
    float flashPower = 0.0f;
    for (int i = 0; i < numPlayers && inGame; i++)
    {
        const Player *p = &players[i];
        if (p->connected && p->alive && p->w.muzzleT > 0)
        {
            flashPos = WeaponMuzzleWorld(p);
            flashPower = 6.0f;
            break;
        }
    }

    BeginMode3D(cam);
        RenderSetFrame(cam.position, RenderSunDir(), 0.018f, flashPos, flashPower);
        MapDraw(G.time, G.burgers);
        PickupsDraw();
        EnemiesDraw();
        HarpoonsDraw();
        for (int i = 0; i < numPlayers && inGame; i++)
            if (&players[i] != viewer && players[i].connected) PlayerDrawBody(&players[i]);
        EffectsDraw();
    EndMode3D();

    if (withWeapon && viewer && viewer->alive) WeaponRenderViewModel(viewer, cam);
}

static void DrawOverlays(int w, int h)
{
    if (G.state == ST_PAUSE) HudDrawPause(w, h, false);
    if (G.state == ST_PLAY && netMenu) HudDrawPause(w, h, true);
    if (G.state == ST_OVER) HudDrawGameOver(w, h);
}

static void EnsureViewTargets(int w, int h)
{
    if (w == viewW && h == viewH && views[0].id) return;
    for (int i = 0; i < 2; i++)
    {
        if (views[i].id) UnloadRenderTexture(views[i]);
        views[i] = LoadRenderTexture(w, h);
    }
    viewW = w;
    viewH = h;
}

static void DrawSplitScreen(int sw, int sh, bool inGame)
{
    int w = sw/2;
    float s = UiScale();
    EnsureViewTargets(w, sh);
    for (int i = 0; i < 2; i++)
    {
        BeginTextureMode(views[i]);
            ClearBackground(SKY_HORIZON);
            RenderView(players[i].cam, &players[i], inGame, w, sh);
            if (inGame)
            {
                UiBegin();
                HudDrawPlayer(&players[i], (int)(w/s), (int)(sh/s), true);
                UiEnd();
            }
        EndTextureMode();
    }

    BeginDrawing();
        ClearBackground(BLACK);
        for (int i = 0; i < 2; i++)   // les RenderTexture sont a l'envers en Y
            DrawTextureRec(views[i].texture, (Rectangle){ 0, 0, (float)w, (float)-sh }, (Vector2){ (float)(i*w), 0 }, WHITE);
        DrawRectangle(w - 2, 0, 4, sh, (Color){ 20, 20, 20, 255 });
        UiBegin();
            if (inGame) HudDrawShared(UiWidth(), UiHeight(), true);
            DrawOverlays(UiWidth(), UiHeight());
        UiEnd();
    EndDrawing();
}

void GameDraw(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int vw = UiWidth(), vh = UiHeight();   // interface en unites virtuelles
    bool menuScreen = (G.state == ST_TITLE || G.state == ST_JOIN || G.state == ST_LOBBY);
    bool inGame = (G.state == ST_PLAY || G.state == ST_PAUSE);

    if (!menuScreen && G.mode == MODE_SPLIT)
    {
        DrawSplitScreen(sw, sh, inGame);
        return;
    }

    BeginDrawing();
        ClearBackground(SKY_HORIZON);
        if (menuScreen)
        {
            RenderView(titleCam, NULL, false, sw, sh);
            UiBegin();
                if (G.state == ST_TITLE) HudDrawTitle(vw, vh);
                else if (G.state == ST_JOIN) HudDrawJoin(vw, vh, joinAddress);
                else HudDrawLobby(vw, vh);
            UiEnd();
        }
        else
        {
            Player *me = FirstLocalPlayer();
            RenderView(me->cam, me, inGame, sw, sh);
            UiBegin();
                if (inGame)
                {
                    HudDrawPlayer(me, vw, vh, G.mode == MODE_NET);
                    HudDrawShared(vw, vh, false);
                    if (TouchMode() && G.state == ST_PLAY && !netMenu && me->alive) TouchDraw(me);
                }
                DrawOverlays(vw, vh);
            UiEnd();
        }
    EndDrawing();
}
