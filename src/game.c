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
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

Game G;

static Camera3D titleCam;
static RenderTexture2D views[2];
static int viewW, viewH;
static char joinAddress[64] = "192.168.1.";
static bool netMenu;   // menu ouvert en reseau (la partie continue)

void GameInit(void)
{
    MapBuild();
    G.state = ST_TITLE;
    G.burgers = START_BURGERS;
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
    DisableCursor();
}

void GameOver(const char *reason)
{
    if (G.state != ST_PLAY) return;
    G.state = ST_OVER;
    snprintf(G.overReason, sizeof(G.overReason), "%s", reason);
    if (G.score > G.best) G.best = G.score;
    EnableCursor();
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
    EnableCursor();
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
    if (G.state == ST_PLAY && !netMenu) DisableCursor();
    else EnableCursor();
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
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT);
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

static void UpdateTitle(void)
{
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) ||
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) GameStart(MODE_SOLO);
    else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT)) GameStart(MODE_SPLIT);
    else if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) HostLobby();
    else if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) { G.state = ST_JOIN; G.titleMsg[0] = '\0'; }
    else if (IsKeyPressed(KEY_ESCAPE)) G.quit = true;
}

static void UpdateJoin(void)
{
    // Saisie de l'adresse puis attente de la reponse de l'hote
    bool waiting = NetGetRole() == NET_CLIENT && NetGetStatus() != NET_FAILED;
    if (!waiting)
    {
        int len = (int)strlen(joinAddress);
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
            if (c > 32 && c < 127 && len < (int)sizeof(joinAddress) - 1) { joinAddress[len++] = (char)c; joinAddress[len] = '\0'; }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) joinAddress[len - 1] = '\0';
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) NetJoin(joinAddress);
    }
    if (IsKeyPressed(KEY_ESCAPE)) BackToTitle(NULL);
}

static void UpdateLobby(void)
{
    if (NetGetRole() == NET_HOST && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) GameStart(MODE_NET);
    else if (IsKeyPressed(KEY_ESCAPE)) BackToTitle(NULL);
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
    if (G.mode == MODE_NET)
    {
        // En reseau, le menu ne met pas la partie en pause
        if (MenuPressed() || (netMenu && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
        {
            netMenu = !netMenu;
            if (netMenu) EnableCursor(); else DisableCursor();
        }
        if (netMenu && (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A))) { BackToTitle(NULL); return; }
    }
    else if (MenuPressed()) { G.state = ST_PAUSE; EnableCursor(); return; }

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
            if (MenuPressed() || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { G.state = ST_PLAY; DisableCursor(); }
            else if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A)) BackToTitle(NULL);
            break;
        case ST_OVER:
            EffectsUpdate(dt);
            for (int i = 0; i < MAX_ENEMIES; i++)   // les gloutons continuent de se dandiner
                if (enemies[i].active && enemies[i].state == ES_WALK) enemies[i].phase += dt*3.0f;
            if (NetIsAuthority() && (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER) || AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)))
                GameStart(G.mode);
            else if (IsKeyPressed(KEY_ESCAPE)) BackToTitle(NULL);
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
    EnsureViewTargets(w, sh);
    for (int i = 0; i < 2; i++)
    {
        BeginTextureMode(views[i]);
            ClearBackground(SKY_HORIZON);
            RenderView(players[i].cam, &players[i], inGame, w, sh);
            if (inGame) HudDrawPlayer(&players[i], w, sh, true);
        EndTextureMode();
    }

    BeginDrawing();
        ClearBackground(BLACK);
        for (int i = 0; i < 2; i++)   // les RenderTexture sont a l'envers en Y
            DrawTextureRec(views[i].texture, (Rectangle){ 0, 0, (float)w, (float)-sh }, (Vector2){ (float)(i*w), 0 }, WHITE);
        DrawRectangle(w - 2, 0, 4, sh, (Color){ 20, 20, 20, 255 });
        if (inGame) HudDrawShared(sw, sh, true);
        DrawOverlays(sw, sh);
    EndDrawing();
}

void GameDraw(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
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
            if (G.state == ST_TITLE) HudDrawTitle(sw, sh);
            else if (G.state == ST_JOIN) HudDrawJoin(sw, sh, joinAddress);
            else HudDrawLobby(sw, sh);
        }
        else
        {
            Player *me = FirstLocalPlayer();
            RenderView(me->cam, me, inGame, sw, sh);
            if (inGame)
            {
                HudDrawPlayer(me, sw, sh, G.mode == MODE_NET);
                HudDrawShared(sw, sh, false);
            }
            DrawOverlays(sw, sh);
        }
    EndDrawing();
}
