/*
 * game.c - Machine a etats (titre / partie / pause / game over) et rendu.
 *
 * En coop, chaque joueur est rendu dans sa propre RenderTexture (moitie
 * gauche / droite de l'ecran), puis les infos communes sont dessinees
 * par-dessus l'ensemble.
 */
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "pickup.h"
#include "waves.h"
#include "hud.h"
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

Game G;

static Camera3D titleCam;
static RenderTexture2D views[MAX_PLAYERS];
static int viewW, viewH;

void GameInit(void)
{
    MapBuild();
    G.state = ST_TITLE;
    G.burgers = START_BURGERS;
}

void GameClose(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) if (views[i].id) UnloadRenderTexture(views[i]);
}

void GameStart(int playerCount)
{
    EnemiesClear();
    EffectsClear();
    PickupsClear();
    FeedClear();

    int best = G.best;
    float time = G.time;
    memset(&G, 0, sizeof(G));
    G.best = best;
    G.time = time;
    G.burgers = START_BURGERS;
    G.state = ST_PLAY;

    PlayersReset(playerCount);
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

//----------------------------------------------------------------------------------
// Mise a jour
//----------------------------------------------------------------------------------
static bool AnyGamepadPressed(int button)
{
    for (int i = 0; i < 4; i++) if (IsGamepadAvailable(i) && IsGamepadButtonPressed(i, button)) return true;
    return false;
}

static bool PausePressed(void)
{
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT);
}

static void UpdateTitle(float dt)
{
    // Camera qui tourne lentement autour du stand
    G.titleT += dt;
    float a = G.titleT*0.15f;
    titleCam.position = V3(sinf(a)*16.0f, 7.0f, cosf(a)*16.0f);
    titleCam.target = V3(0, 2.0f, 0);
    titleCam.up = V3(0, 1, 0);
    titleCam.fovy = 60.0f;
    titleCam.projection = CAMERA_PERSPECTIVE;

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) ||
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) GameStart(1);
    else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT)) GameStart(2);
    else if (IsKeyPressed(KEY_ESCAPE)) G.quit = true;
}

static void UpdatePlay(float dt)
{
    if (PausePressed()) { G.state = ST_PAUSE; EnableCursor(); return; }

    PlayersReadInput(dt);
    for (int i = 0; i < numPlayers; i++)
    {
        PlayerUpdate(&players[i], dt);
        WeaponUpdate(&players[i], dt);
    }
    PickupsUpdate(dt);
    EnemiesUpdate(dt);
    WavesUpdate(dt);
    EffectsUpdate(dt);
    FeedUpdate(dt);
    for (int i = 0; i < numPlayers; i++) PlayerUpdateCamera(&players[i], dt);
    G.bannerT -= dt;
}

void GameUpdate(float dt)
{
    G.time += dt;
    switch (G.state)
    {
        case ST_TITLE:
            UpdateTitle(dt);
            break;
        case ST_PLAY:
            UpdatePlay(dt);
            break;
        case ST_PAUSE:
            if (PausePressed() || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { G.state = ST_PLAY; DisableCursor(); }
            else if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A)) { G.state = ST_TITLE; G.titleT = 0; }
            break;
        case ST_OVER:
            EffectsUpdate(dt);
            for (int i = 0; i < MAX_ENEMIES; i++)   // les gloutons continuent de se dandiner
                if (enemies[i].active && enemies[i].state == ES_WALK) enemies[i].phase += dt*3.0f;
            if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER) || AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) GameStart(numPlayers);
            else if (IsKeyPressed(KEY_ESCAPE)) { G.state = ST_TITLE; G.titleT = 0; }
            break;
    }
}

//----------------------------------------------------------------------------------
// Rendu
//----------------------------------------------------------------------------------
// Scene 3D vue depuis cam. viewer = joueur dont c'est la vue (pas dessine),
// withWeapon = ajoute ses mains et son arme au premier plan.
static void RenderView(Camera3D cam, const Player *viewer, bool withWeapon, int w, int h)
{
    DrawSky(w, h);

    // Le flash de bouche d'un joueur qui tire eclaire la scene
    Vector3 flashPos = V3(0, -100, 0);
    float flashPower = 0.0f;
    for (int i = 0; i < numPlayers && G.state != ST_TITLE; i++)
    {
        if (players[i].alive && players[i].w.muzzleT > 0)
        {
            flashPos = WeaponMuzzleWorld(&players[i]);
            flashPower = 6.0f;
            break;
        }
    }

    BeginMode3D(cam);
        RenderSetFrame(cam.position, RenderSunDir(), 0.018f, flashPos, flashPower);
        MapDraw(G.time, G.burgers);
        PickupsDraw();
        EnemiesDraw();
        if (G.state != ST_TITLE)
            for (int i = 0; i < numPlayers; i++)
                if (&players[i] != viewer) PlayerDrawBody(&players[i]);
        EffectsDraw();
    EndMode3D();

    if (withWeapon && viewer && viewer->alive) WeaponRenderViewModel(viewer, cam);
}

static void EnsureViewTargets(int w, int h)
{
    if (w == viewW && h == viewH && views[0].id) return;
    for (int i = 0; i < MAX_PLAYERS; i++)
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
    for (int i = 0; i < numPlayers; i++)
    {
        BeginTextureMode(views[i]);
            ClearBackground(SKY_HORIZON);
            RenderView(players[i].cam, &players[i], inGame, w, sh);
            if (inGame) HudDrawPlayer(&players[i], w, sh, true);
        EndTextureMode();
    }

    BeginDrawing();
        ClearBackground(BLACK);
        for (int i = 0; i < numPlayers; i++)   // les RenderTexture sont a l'envers en Y
            DrawTextureRec(views[i].texture, (Rectangle){ 0, 0, (float)w, (float)-sh }, (Vector2){ (float)(i*w), 0 }, WHITE);
        DrawRectangle(w - 2, 0, 4, sh, (Color){ 20, 20, 20, 255 });
        if (inGame) HudDrawShared(sw, sh, true);
        if (G.state == ST_PAUSE) HudDrawPause(sw, sh);
        if (G.state == ST_OVER) HudDrawGameOver(sw, sh);
    EndDrawing();
}

void GameDraw(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    bool inGame = (G.state == ST_PLAY || G.state == ST_PAUSE);

    if (G.state != ST_TITLE && numPlayers > 1)
    {
        DrawSplitScreen(sw, sh, inGame);
        return;
    }

    BeginDrawing();
        ClearBackground(SKY_HORIZON);
        if (G.state == ST_TITLE)
        {
            RenderView(titleCam, NULL, false, sw, sh);
            HudDrawTitle(sw, sh);
        }
        else
        {
            RenderView(players[0].cam, &players[0], inGame, sw, sh);
            if (inGame)
            {
                HudDrawPlayer(&players[0], sw, sh, false);
                HudDrawShared(sw, sh, false);
            }
            if (G.state == ST_PAUSE) HudDrawPause(sw, sh);
            if (G.state == ST_OVER) HudDrawGameOver(sw, sh);
        }
    EndDrawing();
}
