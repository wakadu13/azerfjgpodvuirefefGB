/*
 * player.c - Joueurs : lecture des commandes, deplacement, vie, camera.
 *
 * Attribution des peripheriques :
 *   Solo / reseau : clavier/souris (ZQSD, WASD ou fleches) + manette 1.
 *   Ecran partage : J1 = clavier/souris (ZQSD/WASD) [+ manette 2 si branchee]
 *                   J2 = manette 1, ou a defaut le pave IJKL + fleches.
 */
#include "player.h"
#include "map.h"
#include "audio.h"
#include "hud.h"
#include "game.h"
#include "net.h"
#include "render.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

Player players[MAX_PLAYERS];
int numPlayers = 1;

static const Color PLAYER_COLORS[MAX_PLAYERS] = {
    { 70, 150, 255, 255 }, { 255, 140, 40, 255 }, { 90, 220, 90, 255 }, { 230, 90, 230, 255 }
};

//----------------------------------------------------------------------------------
// Creation / reapparition
//----------------------------------------------------------------------------------
static Vector3 SpawnPoint(int id)
{
    if (numPlayers == 1) return V3(0, 0, 9);
    float spacing = (numPlayers == 2) ? 3.0f : 1.5f;
    return V3((id - (numPlayers - 1)*0.5f)*spacing, 0, 9);
}

// Remet un emplacement a zero (nouvelle partie). Les compteurs reseau et
// l'etat de connexion sont conserves.
static void SetupSlot(Player *p, int id)
{
    Player keep = *p;
    memset(p, 0, sizeof(*p));
    p->id = id;
    p->color = PLAYER_COLORS[id];
    p->connected = keep.connected;
    p->local = keep.local;
    p->ammoGrant = keep.ammoGrant;
    p->ammoGrantSeen = keep.ammoGrantSeen;
    p->harpoonGrant = keep.harpoonGrant;
    p->harpoonGrantSeen = keep.harpoonGrantSeen;
    p->respawnCount = keep.respawnCount + 1;
    p->respawnSeen = keep.respawnSeen;
    p->alive = true;
    p->pos = p->netPos = SpawnPoint(id);
    p->yaw = p->netYaw = PI;   // face au stand
    p->pitch = -0.08f;
    p->hp = 100;
    p->grounded = true;
    p->cam.fovy = FOV_HIP;
    WeaponReset(&p->w);
}

void PlayersReset(int count)
{
    numPlayers = count;
    memset(players, 0, sizeof(players));
    for (int i = 0; i < count; i++)
    {
        players[i].connected = true;
        players[i].local = true;
        SetupSlot(&players[i], i);
    }
}

void PlayersResetNet(int localId)
{
    numPlayers = MAX_PLAYERS;
    memset(players, 0, sizeof(players));
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        players[i].id = i;
        players[i].color = PLAYER_COLORS[i];
    }
    players[localId].connected = true;
    players[localId].local = true;
    SetupSlot(&players[localId], localId);
}

void PlayersRestart(void)
{
    for (int i = 0; i < numPlayers; i++)
        if (players[i].connected) SetupSlot(&players[i], i);
}

void PlayerJoin(int id)
{
    Player *p = &players[id];
    p->connected = true;
    p->local = false;
    SetupSlot(p, id);
    FeedAdd(p->color, "%sa rejoint la partie", PlayerTag(p));
}

void PlayerLeave(int id)
{
    Player *p = &players[id];
    p->connected = false;
    p->alive = false;
}

void PlayerRefillWeapon(Player *p)
{
    p->w.reloading = false;
    p->w.ammo = MAG_SIZE;
    if (p->w.reserve < 90) p->w.reserve = 90;
    p->w.harpoonLoaded = true;
    p->w.harpoonT = 0;
    if (p->w.harpoons < 3) p->w.harpoons = 3;
}

void PlayerRespawn(Player *p)
{
    p->alive = true;
    p->hp = 60;
    p->pos = p->netPos = SpawnPoint(p->id);
    p->vel = V3(0, 0, 0);
    p->yaw = p->netYaw = PI;
    p->pitch = -0.08f;
    p->respawnCount++;
    if (p->local) PlayerRefillWeapon(p);
    FeedAdd(p->color, "%sest de retour !", PlayerTag(p));
}

void PlayerGiveAmmo(Player *p, int amount)
{
    // Les munitions d'un joueur distant sont gerees sur sa machine : on lui
    // transmet un cumul qu'il applique lui-meme.
    if (!p->local) { p->ammoGrant += amount; return; }
    p->w.reserve = (p->w.reserve + amount > MAX_RESERVE) ? MAX_RESERVE : p->w.reserve + amount;
}

void PlayerGiveHarpoons(Player *p, int amount)
{
    if (!p->local) { p->harpoonGrant += amount; return; }
    p->w.harpoons = (p->w.harpoons + amount > HARPOON_MAX) ? HARPOON_MAX : p->w.harpoons + amount;
}

const char *PlayerTag(const Player *p)
{
    static const char *tags[MAX_PLAYERS] = { "J1 ", "J2 ", "J3 ", "J4 " };
    return (numPlayers == 1) ? "" : tags[p->id];
}

int PlayersAlive(void)
{
    int n = 0;
    for (int i = 0; i < numPlayers; i++) if (players[i].connected && players[i].alive) n++;
    return n;
}

int PlayersConnected(void)
{
    int n = 0;
    for (int i = 0; i < numPlayers; i++) if (players[i].connected) n++;
    return n;
}

Player *FirstLocalPlayer(void)
{
    for (int i = 0; i < numPlayers; i++) if (players[i].local) return &players[i];
    return &players[0];
}

Player *NearestAlivePlayer(Vector3 pos, float *dist)
{
    Player *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->connected || !p->alive) continue;
        float dx = p->pos.x - pos.x, dz = p->pos.z - pos.z;
        float d = sqrtf(dx*dx + dz*dz);
        if (d < bd) { bd = d; best = p; }
    }
    if (dist) *dist = bd;
    return best;
}

Vector3 PlayerEye(const Player *p) { return V3(p->pos.x, p->pos.y + EYE_HEIGHT, p->pos.z); }

Vector3 PlayerForward(const Player *p)
{
    float pitch = p->pitch + p->w.recoilPitch, yaw = p->yaw + p->w.recoilYaw;
    return V3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
}

//----------------------------------------------------------------------------------
// Commandes
//----------------------------------------------------------------------------------
static float StickAxis(int pad, int axis)
{
    float v = GetGamepadAxisMovement(pad, axis);
    return (fabsf(v) < 0.15f) ? 0.0f : v;
}

static bool TriggerDown(int pad, int axis, int button)
{
    // Selon le pilote, la gachette au repos vaut -1 ou 0 : un seuil a 0.3 marche dans les deux cas
    return GetGamepadAxisMovement(pad, axis) > 0.3f || IsGamepadButtonDown(pad, button);
}

static void ReadKeyboardMouse(PlayerInput *in, bool arrowsMove)
{
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_Z) || (arrowsMove && IsKeyDown(KEY_UP))) in->move.y += 1;
    if (IsKeyDown(KEY_S) || (arrowsMove && IsKeyDown(KEY_DOWN))) in->move.y -= 1;
    if (IsKeyDown(KEY_D) || (arrowsMove && IsKeyDown(KEY_RIGHT))) in->move.x += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_Q) || (arrowsMove && IsKeyDown(KEY_LEFT))) in->move.x -= 1;

    Vector2 md = GetMouseDelta();
    in->look.x -= md.x*MOUSE_SENS;
    in->look.y -= md.y*MOUSE_SENS;

    in->fire |= IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in->aim |= IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    in->sprint |= IsKeyDown(KEY_LEFT_SHIFT);
    in->reload |= IsKeyPressed(KEY_R);
    in->jump |= IsKeyPressed(KEY_SPACE);
    in->toggleMode |= IsKeyPressed(KEY_B);
    in->switchWeapon |= IsKeyPressed(KEY_E) || GetMouseWheelMove() != 0.0f;
    if (IsKeyPressed(KEY_ONE)) in->selectWeapon = WPN_RIFLE;
    if (IsKeyPressed(KEY_TWO)) in->selectWeapon = WPN_HARPOON;
}

static void ReadGamepad(PlayerInput *in, int pad, float dt)
{
    if (!IsGamepadAvailable(pad)) return;
    in->move.x += StickAxis(pad, GAMEPAD_AXIS_LEFT_X);
    in->move.y -= StickAxis(pad, GAMEPAD_AXIS_LEFT_Y);
    in->look.x -= StickAxis(pad, GAMEPAD_AXIS_RIGHT_X)*PAD_LOOK_SPEED*dt;
    in->look.y -= StickAxis(pad, GAMEPAD_AXIS_RIGHT_Y)*PAD_LOOK_SPEED*dt;

    in->fire |= TriggerDown(pad, GAMEPAD_AXIS_RIGHT_TRIGGER, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);
    in->aim |= TriggerDown(pad, GAMEPAD_AXIS_LEFT_TRIGGER, GAMEPAD_BUTTON_LEFT_TRIGGER_2);
    in->sprint |= IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_THUMB);
    in->reload |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);   // X / Carre
    in->jump |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);     // A / Croix
    in->toggleMode |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP); // Y / Triangle
    in->switchWeapon |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1); // RB / R1
}

// Deuxieme joueur au clavier, sans souris
static void ReadSecondKeyboard(PlayerInput *in, float dt)
{
    if (IsKeyDown(KEY_I)) in->move.y += 1;
    if (IsKeyDown(KEY_K)) in->move.y -= 1;
    if (IsKeyDown(KEY_L)) in->move.x += 1;
    if (IsKeyDown(KEY_J)) in->move.x -= 1;
    if (IsKeyDown(KEY_LEFT)) in->look.x += KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_RIGHT)) in->look.x -= KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_UP)) in->look.y += KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_DOWN)) in->look.y -= KEY_LOOK_SPEED*dt;

    in->fire |= IsKeyDown(KEY_RIGHT_CONTROL);
    in->aim |= IsKeyDown(KEY_RIGHT_SHIFT);
    in->sprint |= IsKeyDown(KEY_H);
    in->reload |= IsKeyPressed(KEY_U);
    in->jump |= IsKeyPressed(KEY_O);
    in->toggleMode |= IsKeyPressed(KEY_N);
    in->switchWeapon |= IsKeyPressed(KEY_M);
}

void PlayersReadInput(float dt)
{
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->local) continue;
        PlayerInput *in = &p->in;
        *in = (PlayerInput){ 0 };
        in->selectWeapon = -1;

        if (TouchMode() && G.mode != MODE_SPLIT)
        {
            TouchApplyInput(in);
            ReadGamepad(in, 0, dt);
        }
        else if (G.mode != MODE_SPLIT)
        {
            ReadKeyboardMouse(in, true);
            ReadGamepad(in, 0, dt);
        }
        else if (i == 0)
        {
            ReadKeyboardMouse(in, false);
            ReadGamepad(in, 1, dt);
        }
        else
        {
            ReadGamepad(in, 0, dt);
            ReadSecondKeyboard(in, dt);
        }

        if (Vector2Length(in->move) > 1.0f) in->move = Vector2Normalize(in->move);
        in->firePressed = in->fire && !p->prevFire;
        p->prevFire = in->fire;
    }
}

//----------------------------------------------------------------------------------
// Mise a jour
//----------------------------------------------------------------------------------
void PlayerHurt(Player *p, float dmg)
{
    if (!p->alive || G.state != ST_PLAY) return;
    p->hp -= dmg;
    p->hurtT = 1.0f;
    p->regenDelay = 5.0f;
    p->shake = fmaxf(p->shake, 0.3f);
    if (p->local) PlaySfx(SND_HURT, Randf(0.9f, 1.1f), 0.8f);
    if (p->hp > 0) return;

    p->hp = 0;
    p->alive = false;
    p->deadT = 0;
    if (PlayersAlive() == 0)
        GameOver(PlayersConnected() == 1 ? "Vous avez ete abattu par les mercenaires..." : "Toute l'escouade est tombee...");
    else
        FeedAdd(RED, "%sest tombe ! Retour a la prochaine vague", PlayerTag(p));
}

static void MovePlayer(Player *p, float dt)
{
    const PlayerInput *in = &p->in;
    Vector3 fwd = V3(sinf(p->yaw), 0, cosf(p->yaw));
    Vector3 right = V3(-cosf(p->yaw), 0, sinf(p->yaw));

    p->sprinting = in->sprint && in->move.y > 0.3f && !in->aim && !in->fire && !p->w.reloading;
    float speed = (p->sprinting ? SPRINT_SPEED : WALK_SPEED)*(1.0f - p->w.ads*0.4f);

    // Acceleration progressive (forte au sol, faible en l'air)
    Vector3 wish = Vector3Scale(Vector3Add(Vector3Scale(fwd, in->move.y), Vector3Scale(right, in->move.x)), speed);
    float accel = p->grounded ? 12.0f : 2.5f;
    p->vel.x += (wish.x - p->vel.x)*fminf(1.0f, dt*accel);
    p->vel.z += (wish.z - p->vel.z)*fminf(1.0f, dt*accel);
    if (p->grounded && in->jump) { p->vel.y = JUMP_SPEED; p->grounded = false; }

    // Horizontal : collisions avec le decor et limites de l'arene
    p->pos.x += p->vel.x*dt;
    p->pos.z += p->vel.z*dt;
    ResolveCircle(&p->pos, PLAYER_RADIUS, p->pos.y, 1.8f);
    p->pos.x = Clamp(p->pos.x, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);
    p->pos.z = Clamp(p->pos.z, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);

    // Vertical : gravite, atterrissage au sol ou sur une caisse
    float ground = GroundHeightAt(p->pos, PLAYER_RADIUS, p->pos.y);
    p->vel.y -= GRAVITY*dt;
    p->pos.y += p->vel.y*dt;
    if (p->pos.y <= ground)
    {
        p->pos.y = ground;
        p->vel.y = 0;
        p->grounded = true;
    }
    else if (p->pos.y > ground + 0.05f) p->grounded = false;

    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    if (p->grounded) p->bob += dt*hspeed*1.6f;
}

void PlayerUpdate(Player *p, float dt)
{
    p->hitT -= dt;
    p->hurtT = fmaxf(0, p->hurtT - dt*1.5f);
    p->shake = fmaxf(0, p->shake - dt*1.5f);
    if (!p->alive) { p->deadT += dt; return; }

    float sens = 1.0f - p->w.ads*0.35f;   // plus precis en visant
    p->yaw += p->in.look.x*sens;
    p->pitch = Clamp(p->pitch + p->in.look.y*sens, -1.5f, 1.5f);

    MovePlayer(p, dt);

    // La vie est geree par l'hote : un client se contente d'afficher celle recue
    p->regenDelay -= dt;
    if (NetIsAuthority() && p->regenDelay <= 0 && p->hp < 100) p->hp = fminf(100, p->hp + dt*6.0f);
}

static float LerpAngle(float a, float b, float t)
{
    float d = b - a;
    while (d > PI) d -= 2*PI;
    while (d < -PI) d += 2*PI;
    return a + d*t;
}

void PlayerUpdateRemote(Player *p, float dt)
{
    // Lissage vers la derniere position recue (les paquets arrivent 30 a 60 fois/s)
    float k = fminf(1.0f, dt*15.0f);
    if (Vector3Distance(p->pos, p->netPos) > 4.0f) p->pos = p->netPos;   // teleportation (reapparition)
    else p->pos = Vector3Lerp(p->pos, p->netPos, k);
    p->yaw = LerpAngle(p->yaw, p->netYaw, k);
    p->pitch += (p->netPitch - p->pitch)*k;
    p->w.muzzleT -= dt;
    p->hurtT = fmaxf(0, p->hurtT - dt*1.5f);
    if (!p->alive) p->deadT += dt;
    p->regenDelay -= dt;
    if (NetIsAuthority() && p->alive && p->regenDelay <= 0 && p->hp < 100) p->hp = fminf(100, p->hp + dt*6.0f);
}

void PlayerUpdateCamera(Player *p, float dt)
{
    Vector3 eye = PlayerEye(p);
    if (!p->alive)
    {
        // Mort : la camera tombe au sol
        eye.y = Lerp(eye.y, p->pos.y + 0.35f, fminf(p->deadT*2.0f, 1.0f));
        p->cam.position = eye;
        p->cam.target = Vector3Add(eye, V3(sinf(p->yaw), -0.3f, cosf(p->yaw)));
        p->cam.up = Vector3Normalize(V3(cosf(p->yaw)*0.6f, 1.0f, 0));
        return;
    }
    if (p->grounded)
    {
        float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
        eye.y += sinf(p->bob*2.0f)*0.03f*fminf(hspeed/WALK_SPEED, 1.4f)*(1.0f - p->w.ads*0.8f);
    }
    Vector3 shake = V3(Randf(-1, 1), Randf(-1, 1), Randf(-1, 1));
    p->cam.position = eye;
    p->cam.target = Vector3Add(Vector3Add(eye, PlayerForward(p)), Vector3Scale(shake, p->shake*0.05f));
    p->cam.up = V3(0, 1, 0);
    p->cam.projection = CAMERA_PERSPECTIVE;

    float fov = Lerp(FOV_HIP, FOV_ADS, p->w.ads);
    fov = Lerp(fov, FOV_SPRINT, p->w.sprintT);
    p->cam.fovy += (fov - p->cam.fovy)*fminf(1.0f, dt*14.0f);
}

//----------------------------------------------------------------------------------
// Modele du soldat (vu par les autres joueurs)
//----------------------------------------------------------------------------------
void PlayerDrawBody(const Player *p)
{
    const Color uniform = { 92, 98, 66, 255 }, vest = { 125, 115, 82, 255 };
    const Color skin = { 225, 180, 140, 255 }, boots = { 45, 40, 32, 255 };

    Matrix root;
    if (p->alive) root = MatrixMultiply(MatrixRotateY(p->yaw), MatrixTranslate(p->pos.x, p->pos.y, p->pos.z));
    else root = MatrixMultiply(MatrixMultiply(MatrixRotateX(-PI*0.5f), MatrixRotateY(p->yaw)), MatrixTranslate(p->pos.x, 0.2f, p->pos.z));

    DrawBlobShadow(p->pos, 0.4f);

    // Jambes (balancement a la marche)
    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    float swing = p->alive ? sinf(p->bob*2.0f)*0.3f*fminf(hspeed/WALK_SPEED, 1.0f) : 0.0f;
    for (int k = -1; k <= 1; k += 2)
    {
        Vector3 foot = V3(0.13f*k, 0.12f, swing*k);
        Limb(root, V3(0.12f*k, 0.85f, 0), foot, 0.09f, uniform);
        Box3(root, V3(foot.x, 0.06f, foot.z + 0.05f), NOROT, V3(0.14f, 0.12f, 0.28f), boots);
    }

    // Torse, gilet tactique, tete et casque (bande a la couleur du joueur)
    Box3(root, V3(0, 1.2f, 0), NOROT, V3(0.42f, 0.62f, 0.25f), uniform);
    Box3(root, V3(0, 1.22f, 0.02f), NOROT, V3(0.46f, 0.45f, 0.31f), vest);
    Ball(root, V3(0, 1.62f, 0), V3(0.13f, 0.14f, 0.13f), skin);
    Ball(root, V3(0, 1.69f, -0.01f), V3(0.16f, 0.11f, 0.17f), (Color){ 75, 80, 55, 255 });
    Box3(root, V3(0, 1.67f, 0), NOROT, V3(0.33f, 0.035f, 0.35f), p->color);
    Box3(root, V3(-0.23f, 1.38f, 0), NOROT, V3(0.03f, 0.08f, 0.14f), p->color);   // brassard

    // HK416 tenu a l'epaule, incline selon le regard
    Matrix gunLocal = MatrixMultiply(MatrixMultiply(MatrixRotateX(p->alive ? p->pitch : 0.0f), MatrixRotateY(PI)),
                                     MatrixTranslate(0.12f, 1.4f, 0.28f));
    if (p->w.active == WPN_HARPOON) DrawHarpoonGun(MatrixMultiply(gunLocal, root), &p->w);
    else DrawHK416(MatrixMultiply(gunLocal, root), &p->w);

    // Bras vers la poignee et le garde-main
    Vector3 grip = Vector3Transform(V3(0, -0.1f, 0.09f), gunLocal);
    Vector3 fore = Vector3Transform(V3(0, -0.04f, -0.33f), gunLocal);
    Limb(root, V3(0.24f, 1.42f, 0), grip, 0.055f, uniform);
    Limb(root, V3(-0.24f, 1.42f, 0), fore, 0.055f, uniform);
}
