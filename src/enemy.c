/*
 * enemy.c - Les gloutons.
 *
 * IA : chaque glouton entre par une porte puis va manger au comptoir.
 * S'il passe pres d'un joueur (ou si c'est un "Presse"), il le poursuit et
 * le frappe au corps a corps. Chaque burger mange le fait grossir.
 */
#include "enemy.h"
#include "map.h"
#include "net.h"
#include "weapon.h"
#include "effects.h"
#include "pickup.h"
#include "audio.h"
#include "hud.h"
#include "game.h"
#include "render.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define AGGRO_RANGE  7.0f    // distance a laquelle un glouton prefere le joueur au burger
#define EAT_TIME     4.0f    // secondes pour avaler un burger

Enemy enemies[MAX_ENEMIES];

static const Color SHIRTS[] = { { 230, 70, 60, 255 }, { 60, 120, 220, 255 }, { 250, 250, 245, 255 }, { 250, 200, 60, 255 },
                                { 120, 190, 90, 255 }, { 200, 90, 200, 255 }, { 255, 140, 40, 255 } };
static const Color SKINS[] = { { 255, 210, 170, 255 }, { 230, 180, 140, 255 }, { 190, 135, 95, 255 }, { 130, 90, 60, 255 } };
static const Color PANTS[] = { { 50, 70, 120, 255 }, { 70, 70, 75, 255 }, { 110, 90, 60, 255 }, { 40, 40, 45, 255 } };
#define SHIRT_WHITE 2

void EnemiesClear(void) { memset(enemies, 0, sizeof(enemies)); }

const char *EnemyName(EnemyType type)
{
    switch (type)
    {
        case E_FAST: return "Le Presse";
        case E_TANK: return "Le Glouton";
        case E_BOSS: return "LE ROI DU GRAS";
        default:     return "Le Gourmand";
    }
}

float EnemyRadius(const Enemy *e) { return 0.72f*e->sz*e->fat; }

int EnemiesAlive(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active && enemies[i].state != ES_DEAD) n++;
    return n;
}

//----------------------------------------------------------------------------------
// Apparition
//----------------------------------------------------------------------------------
static void SetStats(Enemy *e, int wave)
{
    switch (e->type)
    {
        case E_FAST: e->maxHp = 60;  e->speed = 4.6f; e->sz = 0.85f; e->fat = 1.0f;  e->dmg = 7;  break;
        case E_TANK: e->maxHp = 320; e->speed = 1.7f; e->sz = 1.35f; e->fat = 1.35f; e->dmg = 20; break;
        case E_BOSS:
            e->maxHp = 2200 + 300*wave; e->speed = 1.9f; e->sz = 2.3f; e->fat = 1.4f; e->dmg = 35;
            e->shirt = SHIRT_WHITE;
            e->hasCap = false;
            break;
        default:     e->maxHp = 90;  e->speed = 2.7f; e->sz = 1.0f;  e->fat = 1.0f;  e->dmg = 12; break;
    }
    // Plus resistants et un peu plus rapides au fil des vagues (et en coop)
    if (e->type != E_BOSS) e->maxHp *= 1.0f + 0.12f*(wave - 1);
    if (PlayersConnected() > 1) e->maxHp *= 1.25f;
    e->speed *= fminf(1.0f + 0.03f*(wave - 1), 1.4f);
    e->hp = e->maxHp;
}

static Vector3 GateSpawnPoint(void)
{
    // Derriere une des quatre portes, un peu decale pour ne pas s'empiler
    float off = Randf(-1.6f, 1.6f), dist = ARENA + Randf(3.0f, 6.0f);
    switch (GetRandomValue(0, 3))
    {
        case 0:  return V3(off, 0, -dist);
        case 1:  return V3(off, 0, dist);
        case 2:  return V3(-dist, 0, off);
        default: return V3(dist, 0, off);
    }
}

void EnemySpawn(EnemyType type, int wave)
{
    Enemy *e = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!enemies[i].active) { e = &enemies[i]; break; }
    if (!e) return;

    memset(e, 0, sizeof(*e));
    e->active = true;
    e->type = type;
    e->state = ES_WALK;
    e->shirt = (unsigned char)GetRandomValue(0, 6);
    e->skin = (unsigned char)GetRandomValue(0, 3);
    e->pants = (unsigned char)GetRandomValue(0, 3);
    e->hasCap = GetRandomValue(0, 2) == 0;
    e->cap = (unsigned char)GetRandomValue(0, 6);
    e->phase = Randf(0, 2*PI);
    e->sideSign = GetRandomValue(0, 1) ? 1.0f : -1.0f;
    SetStats(e, wave);

    e->pos = GateSpawnPoint();
    e->yaw = atan2f(-e->pos.x, -e->pos.z);
    if (type == E_BOSS) FeedAdd(GOLD, "%s est arrive !", EnemyName(type));
}

//----------------------------------------------------------------------------------
// Degats
//----------------------------------------------------------------------------------
int EnemyRaycast(Ray ray, float *dist, bool *head)
{
    int hit = -1;
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        float s = e->sz;
        Vector3 fw = V3(sinf(e->yaw), 0, cosf(e->yaw));

        // Trois spheres : tete, ventre (decale vers l'avant), jambes
        RayCollision c = GetRayCollisionSphere(ray, V3(e->pos.x, 2.02f*s, e->pos.z), 0.31f*s);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = true; }

        Vector3 belly = Vector3Add(V3(e->pos.x, 1.15f*s, e->pos.z), Vector3Scale(fw, 0.08f*s*e->fat));
        c = GetRayCollisionSphere(ray, belly, 0.64f*s*e->fat);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = false; }

        c = GetRayCollisionSphere(ray, V3(e->pos.x, 0.5f*s, e->pos.z), 0.45f*s*e->fat);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = false; }
    }
    return hit;
}

static int KillPoints(const Enemy *e, bool head)
{
    int pts = 100;
    if (e->type == E_FAST) pts = 120;
    if (e->type == E_TANK) pts = 300;
    if (e->type == E_BOSS) pts = 2500;
    return head ? pts*3/2 : pts;
}

static void KillEnemy(Enemy *e, bool head, bool harpoon, Vector3 dir, Player *shooter)
{
    e->state = ES_DEAD;
    e->deadT = 0;
    shooter->kills++;
    if (head) shooter->headshots++;
    shooter->hitKill = true;
    G.score += KillPoints(e, head);

    if (harpoon) FeedAdd(SKYBLUE, "%s%s harponne !", PlayerTag(shooter), EnemyName(e->type));
    else FeedAdd(head ? ORANGE : RAYWHITE, head ? "%s%s neutralise (HEADSHOT)" : "%s%s neutralise", PlayerTag(shooter), EnemyName(e->type));
    BurstSauce(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, e->type == E_BOSS ? 120 : 40);
    SfxWorld(SND_KILL, 1.0f, 0.5f);
    PickupDrop(e->pos);
    if (e->type == E_BOSS)
    {
        PickupDrop(e->pos);
        PickupDrop(V3(e->pos.x + 1, 0, e->pos.z));
    }
}

void EnemyDamage(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter)
{
    e->hp -= dmg;
    e->hitFlash = 1.0f;
    shooter->hitT = 0.18f;
    shooter->hitKill = false;
    shooter->hitHead = head;
    BurstSauce(hitPos, Vector3Negate(dir), head ? 14 : 7);
    SfxWorld(SND_HIT, Randf(0.9f, 1.15f), 0.7f);
    if (e->hp <= 0) KillEnemy(e, head, false, dir, shooter);
}

void EnemyPredictHit(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter)
{
    // Effets locaux seulement (non retransmis) : l'hote rejouera les siens
    NetSuppress(true);
    e->hp -= dmg;
    e->hitFlash = 1.0f;
    shooter->hitT = 0.18f;
    shooter->hitHead = head;
    shooter->hitKill = e->hp <= 0;
    BurstSauce(hitPos, Vector3Negate(dir), head ? 14 : 7);
    PlaySfx(SND_HIT, Randf(0.9f, 1.15f), 0.7f);
    if (e->hp <= 0)
    {
        e->state = ES_DEAD;
        e->deadT = 0;
        e->predictT = 0.8f;
        BurstSauce(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, 40);
        PlaySfx(SND_KILL, 1.0f, 0.5f);
    }
    NetSuppress(false);
}

//----------------------------------------------------------------------------------
// Harpon : glouton projete
//----------------------------------------------------------------------------------
static void LaunchBody(Enemy *e, Vector3 dir, float power, int killer)
{
    Vector3 h = { dir.x, 0, dir.z };
    h = (Vector3Length(h) < 0.01f) ? V3(sinf(e->yaw + PI), 0, cosf(e->yaw + PI)) : Vector3Normalize(h);
    float mass = 1.0f/(0.6f + 0.4f*e->sz*e->fat);   // les gros volent moins loin
    e->vel = Vector3Add(Vector3Scale(h, HARPOON_LAUNCH*power*mass), V3(0, HARPOON_LIFT*power*mass + 2.0f, 0));
    e->launched = e->airborne = true;
    e->spin = 0;
    e->yaw = atan2f(-h.x, -h.z);   // face au tireur : il part a la renverse
    e->deadT = 0;
    e->killer = killer;
    e->bowled = 0;
}

void EnemyHarpoon(Enemy *e, Vector3 dir, Player *shooter)
{
    e->hitFlash = 1.0f;
    shooter->hitT = 0.25f;
    shooter->hitHead = false;
    KillEnemy(e, false, true, dir, shooter);
    LaunchBody(e, dir, 1.0f, shooter->id);
    SfxWorld(SND_THUD, 0.9f, 0.9f);
}

void EnemyPredictHarpoon(Enemy *e, Vector3 dir, Player *shooter)
{
    NetSuppress(true);
    e->hitFlash = 1.0f;
    e->state = ES_DEAD;
    e->predictT = 1.5f;
    shooter->hitT = 0.25f;
    shooter->hitKill = true;
    shooter->hitHead = false;
    BurstSauce(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, 40);
    LaunchBody(e, dir, 1.0f, shooter->id);
    PlaySfx(SND_KILL, 1.0f, 0.5f);
    PlaySfx(SND_THUD, 0.9f, 0.9f);
    NetSuppress(false);
}

// En vol, le glouton harponne renverse ceux qu'il percute
static void Bowl(Enemy *e)
{
    Vector3 push = Vector3Normalize(V3(e->vel.x, 0, e->vel.z));
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *o = &enemies[i];
        if (o == e || !o->active || o->state == ES_DEAD || (e->bowled & (1ull << i))) continue;
        float dx = o->pos.x - e->pos.x, dz = o->pos.z - e->pos.z, rr = EnemyRadius(e) + EnemyRadius(o);
        if (dx*dx + dz*dz > rr*rr || e->pos.y > 2.2f*o->sz) continue;
        e->bowled |= 1ull << i;
        o->pos = Vector3Add(o->pos, push);
        EnemyDamage(o, HARPOON_BOWL_DMG, false, V3(o->pos.x, 1.2f*o->sz, o->pos.z), push, &players[e->killer]);
        if (o->state == ES_DEAD) LaunchBody(o, push, 0.6f, e->killer);   // la quille s'envole aussi
    }
}

static void UpdateLaunched(Enemy *e, float dt, bool authority)
{
    if (!e->airborne)
    {
        // Au sol : se couche sur le dos ou le ventre, puis disparait
        e->deadT += dt;
        e->spin += (e->restSpin - e->spin)*fminf(1.0f, dt*12.0f);
        return;
    }
    e->deadT += dt;
    e->vel.y -= 22.0f*dt;
    e->pos = Vector3Add(e->pos, Vector3Scale(e->vel, dt));
    e->spin += dt*9.0f/e->sz;

    // S'ecrase contre les murs et les caisses (et glisse le long), sans revenir vers le tireur
    Vector3 before = e->pos;
    ResolveCircle(&e->pos, EnemyRadius(e)*0.7f, e->pos.y, 2.0f*e->sz);
    Vector3 pushed = Vector3Subtract(e->pos, before);
    float pl = Vector3Length(pushed);
    if (pl > 1e-4f)
    {
        Vector3 n = Vector3Scale(pushed, 1.0f/pl);
        float into = e->vel.x*n.x + e->vel.z*n.z;
        if (into < 0) { e->vel.x -= into*n.x; e->vel.z -= into*n.z; }   // on retire la composante vers l'obstacle
        e->vel.x *= 0.5f;
        e->vel.z *= 0.5f;
    }
    e->pos.x = Clamp(e->pos.x, -ARENA - 8, ARENA + 8);
    e->pos.z = Clamp(e->pos.z, -ARENA - 8, ARENA + 8);
    if (authority) Bowl(e);

    if (e->pos.y <= 0 && e->vel.y < 0)
    {
        e->pos.y = 0;
        if (e->vel.y < -5.0f)   // rebondit une fois
        {
            e->vel = V3(e->vel.x*0.5f, -e->vel.y*0.3f, e->vel.z*0.5f);
            PlaySfx(SND_THUD, Randf(0.8f, 1.0f), 0.6f);
        }
        else
        {
            e->airborne = false;
            e->vel = V3(0, 0, 0);
            e->deadT = 0;
            float m = fmodf(e->spin, 2*PI);
            if (m < 0) m += 2*PI;
            e->restSpin = e->spin - m + ((m < PI) ? PI*0.5f : PI*1.5f);
        }
    }
    if (e->deadT > 5.0f) { e->airborne = false; e->pos.y = 0; e->deadT = 0; e->restSpin = PI*0.5f; }
}

//----------------------------------------------------------------------------------
// IA
//----------------------------------------------------------------------------------
static void EatBurger(Enemy *e)
{
    if (G.burgers <= 0 || G.state != ST_PLAY) return;
    G.burgers--;
    e->fat = fminf(e->fat + 0.15f, e->type == E_BOSS ? 2.0f : 2.2f);
    e->hp = fminf(e->hp + e->maxHp*0.3f, e->maxHp*1.6f);
    BurstCrumbs(V3(e->pos.x, 1.9f*e->sz, e->pos.z));
    SfxWorld(SND_NOM, Randf(0.85f, 1.15f), 0.9f);
    FeedAdd((Color){ 255, 120, 80, 255 }, "%s a mange un burger !", EnemyName(e->type));
    if (G.burgers == 0) GameOver("Les gloutons ont devore tous les burgers !");
}

static void TurnTowards(Enemy *e, Vector3 dir, float dt)
{
    float dy = atan2f(dir.x, dir.z) - e->yaw;
    while (dy > PI) dy -= 2*PI;
    while (dy < -PI) dy += 2*PI;
    e->yaw += dy*fminf(1.0f, dt*8.0f);
}

static void UpdateEnemy(Enemy *e, float dt)
{
    float rad = EnemyRadius(e);
    float dPlayer;
    Player *target = NearestAlivePlayer(e->pos, &dPlayer);
    bool outside = fabsf(e->pos.x) > ARENA - 0.5f || fabsf(e->pos.z) > ARENA - 0.5f;

    // Attaque au corps a corps
    bool inReach = target && dPlayer < rad + PLAYER_RADIUS + 0.5f && target->pos.y < 1.6f*e->sz;
    if (inReach && e->attackCd <= 0)
    {
        e->attackCd = 1.3f;
        e->attackAnim = 1.0f;
        PlayerHurt(target, e->dmg);
    }

    // En train de manger : reste sur place sauf si un joueur s'approche
    if (e->state == ES_EAT)
    {
        Vector3 cp = CounterPoint(e->pos);
        e->yaw = atan2f(cp.x - e->pos.x, cp.z - e->pos.z);
        e->eatT += dt;
        if (target && dPlayer < 3.0f) e->state = ES_WALK;
        else if (e->eatT > EAT_TIME) { e->eatT = 0; EatBurger(e); }
        return;
    }

    // Choix de la destination
    Vector3 dest;
    bool goStand = false;
    if (outside) dest = V3(0, 0, 0);   // franchir la porte d'abord
    else if (target && (e->type == E_FAST || dPlayer < AGGRO_RANGE)) dest = V3(target->pos.x, 0, target->pos.z);
    else { dest = CounterPoint(e->pos); goStand = true; }

    Vector3 to = Vector3Subtract(dest, e->pos);
    to.y = 0;
    float dist = Vector3Length(to);

    if (goStand && dist < rad + 0.35f)
    {
        e->state = ES_EAT;
        e->eatT = 0;
        return;
    }

    if (!goStand && !outside && inReach)
    {
        e->yaw = atan2f(target->pos.x - e->pos.x, target->pos.z - e->pos.z);
        return;
    }
    if (dist < 0.01f) return;

    // Avance ; si bloque contre un obstacle, contourne par le cote
    float spd = e->speed/(0.7f + 0.3f*e->fat);   // plus gros = plus lent
    Vector3 dir = Vector3Scale(to, 1.0f/dist);
    if (e->stuckT > 0.4f) dir = Vector3Normalize(Vector3Add(dir, V3(-dir.z*e->sideSign*1.5f, 0, dir.x*e->sideSign*1.5f)));

    Vector3 old = e->pos;
    e->pos = Vector3Add(e->pos, Vector3Scale(dir, spd*dt));
    ResolveCircle(&e->pos, rad, 0.0f, 2.0f*e->sz);
    if (Vector3Distance(old, e->pos) < spd*dt*0.3f) e->stuckT += dt;
    else e->stuckT = fmaxf(0, e->stuckT - dt*0.5f);
    if (e->stuckT > 2.0f) { e->stuckT = 0; e->sideSign = -e->sideSign; }

    TurnTowards(e, dir, dt);
    e->phase += dt*spd*3.2f/e->sz;
}

// Les gloutons se repoussent entre eux
static void SeparateEnemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *a = &enemies[i];
        if (!a->active || a->state == ES_DEAD) continue;
        for (int j = i + 1; j < MAX_ENEMIES; j++)
        {
            Enemy *b = &enemies[j];
            if (!b->active || b->state == ES_DEAD) continue;
            float dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;
            float rr = (EnemyRadius(a) + EnemyRadius(b))*0.9f;
            float d2 = dx*dx + dz*dz;
            if (d2 >= rr*rr || d2 < 1e-6f) continue;
            float d = sqrtf(d2), push = (rr - d)*0.5f;
            float wa = (a->type == E_BOSS) ? 0.1f : 1.0f, wb = (b->type == E_BOSS) ? 0.1f : 1.0f;
            a->pos.x -= dx/d*push*wa; a->pos.z -= dz/d*push*wa;
            b->pos.x += dx/d*push*wb; b->pos.z += dz/d*push*wb;
        }
    }
}

// Les gloutons repoussent les joueurs (en reseau, chacun gere le sien)
void EnemiesPushPlayers(bool localOnly)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *a = &enemies[i];
        if (!a->active || a->state == ES_DEAD) continue;
        for (int k = 0; k < numPlayers; k++)
        {
            Player *p = &players[k];
            if (!p->connected || !p->alive || (localOnly && !p->local) || p->pos.y >= 1.5f*a->sz) continue;
            float dx = p->pos.x - a->pos.x, dz = p->pos.z - a->pos.z;
            float rr = EnemyRadius(a) + PLAYER_RADIUS;
            float d2 = dx*dx + dz*dz;
            if (d2 >= rr*rr || d2 < 1e-6f) continue;
            float d = sqrtf(d2);
            p->pos.x += dx/d*(rr - d);
            p->pos.z += dz/d*(rr - d);
        }
    }
}

void EnemiesUpdate(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        e->hitFlash = fmaxf(0, e->hitFlash - dt*6);
        e->attackAnim = fmaxf(0, e->attackAnim - dt*2.5f);
        e->attackCd -= dt;
        if (e->state == ES_DEAD)
        {
            if (e->launched) UpdateLaunched(e, dt, true);
            else e->deadT += dt;
            if (!e->airborne && e->deadT > 3.5f) e->active = false;
            continue;
        }
        UpdateEnemy(e, dt);
    }
    SeparateEnemies();
    EnemiesPushPlayers(false);
}

void EnemiesClientUpdate(float dt)
{
    // L'etat vient de l'hote ~30 fois/s : on lisse la position et on anime
    // localement entre deux paquets.
    float k = fminf(1.0f, dt*12.0f);
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        e->hitFlash = fmaxf(0, e->hitFlash - dt*6);
        e->attackAnim = fmaxf(0, e->attackAnim - dt*2.5f);
        e->predictT -= dt;
        if (e->state == ES_DEAD)
        {
            if (e->launched && e->predictT > 0) UpdateLaunched(e, dt, false);   // vol predit localement
            else if (e->launched)
            {
                e->pos = Vector3Lerp(e->pos, e->netPos, k);
                e->spin += (e->netSpin - e->spin)*k;
                if (!e->airborne) e->deadT += dt;
            }
            else e->deadT += dt;
            continue;
        }
        if (e->state == ES_EAT) e->eatT += dt;

        Vector3 old = e->pos;
        e->pos = Vector3Lerp(e->pos, e->netPos, k);
        float dy = e->netYaw - e->yaw;
        while (dy > PI) dy -= 2*PI;
        while (dy < -PI) dy += 2*PI;
        e->yaw += dy*k;
        float moved = Vector3Distance(old, e->pos);
        if (e->state == ES_WALK) e->phase += moved*3.2f/(e->sz*(0.7f + 0.3f*e->fat));
    }
    EnemiesPushPlayers(true);
}

//----------------------------------------------------------------------------------
// Modele 3D
//----------------------------------------------------------------------------------
static void DrawArms(Matrix body, const Enemy *e, Color shirt, Color skin, float walkSwing)
{
    float f = e->fat;
    for (int k = -1; k <= 1; k += 2)
    {
        Vector3 shoulder = V3(0.5f*k*f, 1.62f, 0), elbow, hand;
        bool eating = (e->state == ES_EAT && k == 1);
        if (eating)
        {
            // Porte le burger a la bouche en machant
            float chew = sinf(e->eatT*6.0f)*0.05f;
            elbow = V3(0.45f*f, 1.45f, 0.35f);
            hand = V3(0.18f, 1.85f + chew, 0.42f);
        }
        else if (e->state == ES_DEAD)
        {
            elbow = V3(0.75f*k*f, 1.4f, -0.1f);
            hand = V3(0.95f*k*f, 1.2f, -0.2f);
        }
        else
        {
            // Bras tendus vers l'avant (vers les burgers !) ; elan si attaque
            float a = e->attackAnim, sway = -walkSwing*k*0.12f;
            elbow = V3(0.6f*k*f, 1.42f, 0.32f*f + sway);
            hand = V3(0.38f*k*f, 1.5f + a*0.3f, 0.62f*f + sway + a*0.35f);
        }
        Limb(body, shoulder, elbow, 0.14f, shirt);
        Limb(body, elbow, hand, 0.11f, skin);
        Ball(body, hand, V3(0.13f, 0.13f, 0.13f), skin);
        if (eating)
        {
            Matrix bm = MatrixMultiply(MatrixMultiply(MatrixScale(0.9f, 0.9f, 0.9f), MatrixRotateX(-1.0f)),
                                       MatrixMultiply(MatrixTranslate(hand.x - 0.05f, hand.y - 0.05f, hand.z + 0.1f), body));
            DrawBurger(bm);
        }
    }
}

static void DrawHead(Matrix body, const Enemy *e, Color skin, float hf)
{
    float f = e->fat;
    float mouthOpen = 0.35f;
    if (e->state == ES_EAT) mouthOpen = 0.5f + 0.5f*sinf(e->eatT*14.0f);
    else if (e->attackAnim > 0) mouthOpen = 1.0f;

    Color cheek = Flash((Color){ 240, 130, 120, 255 }, hf);
    Ball(body, V3(0, 1.82f, 0.12f), V3(0.26f*f, 0.13f, 0.22f), skin);   // double menton
    Ball(body, V3(0, 2.02f, 0.02f), V3(0.31f + 0.03f*f, 0.3f, 0.3f), skin);
    Ball(body, V3(-0.13f, 1.98f, 0.24f), V3(0.08f, 0.07f, 0.06f), cheek);
    Ball(body, V3(0.13f, 1.98f, 0.24f), V3(0.08f, 0.07f, 0.06f), cheek);
    for (int k = -1; k <= 1; k += 2)
    {
        Ball(body, V3(0.11f*k, 2.1f, 0.25f), V3(0.065f, 0.065f, 0.04f), WHITE);
        Ball(body, V3(0.11f*k, 2.1f, 0.285f), V3(0.03f, 0.03f, 0.015f), BLACK);
    }
    Ball(body, V3(0, 1.91f, 0.27f), V3(0.11f, 0.03f + 0.07f*mouthOpen, 0.04f), (Color){ 90, 20, 20, 255 });

    if (e->hasCap)   // casquette a l'envers
    {
        Color cap = Flash(SHIRTS[e->cap % 7], hf);
        Ball(body, V3(0, 2.2f, 0), V3(0.31f, 0.16f, 0.31f), cap);
        Box3(body, V3(0, 2.2f, -0.3f), NOROT, V3(0.26f, 0.03f, 0.22f), cap);
    }
    if (e->type == E_BOSS)   // couronne + bavoir plein de ketchup
    {
        Color gold = Flash((Color){ 255, 200, 40, 255 }, hf);
        Seg(body, V3(0, 2.22f, 0), V3(0, 2.34f, 0), 0.25f, gold);
        for (int k = 0; k < 6; k++)
        {
            float a = k*PI/3;
            Ball(body, V3(cosf(a)*0.22f, 2.4f, sinf(a)*0.22f), V3(0.05f, 0.1f, 0.05f), gold);
        }
        Box3(body, V3(0, 1.5f, 0.56f*f), V3(0.35f, 0, 0), V3(0.5f, 0.4f, 0.02f), Flash(WHITE, hf));
        Ball(body, V3(0.08f, 1.45f, 0.6f*f), V3(0.09f, 0.07f, 0.02f), (Color){ 200, 20, 15, 255 });
    }
}

static void DrawEnemy(const Enemy *e)
{
    float s = e->sz, f = e->fat;

    // Mort : bascule en arriere puis s'enfonce dans le sol
    float tilt = 0, sink = 0;
    if (e->state == ES_DEAD)
    {
        float k = fminf(e->deadT/0.7f, 1.0f);
        tilt = (1.0f - (1.0f - k)*(1.0f - k))*PI*0.5f;
        if (e->deadT > 1.8f) sink = (e->deadT - 1.8f)*0.9f*s;
    }
    Matrix root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateX(-tilt)),
                                 MatrixRotateY(e->yaw)), MatrixTranslate(e->pos.x, -sink, e->pos.z));
    if (e->launched)
    {
        // Projete : tourne autour de son ventre en vol, puis couche au sol
        float centerH = e->airborne ? 1.1f*s : 0.6f*s*f;
        sink = (!e->airborne && e->deadT > 1.8f) ? (e->deadT - 1.8f)*0.9f*s : 0.0f;
        root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(0, -1.1f*s, 0)),
                              MatrixRotateX(-e->spin)), MatrixRotateY(e->yaw)),
                              MatrixTranslate(e->pos.x, e->pos.y + centerH - sink, e->pos.z));
    }

    float hf = e->hitFlash;   // flash blanc quand il est touche
    Color skin = Flash(SKINS[e->skin % 4], hf), shirt = Flash(SHIRTS[e->shirt % 7], hf), pants = Flash(PANTS[e->pants % 4], hf);

    bool walking = e->state == ES_WALK;
    float sw = walking ? sinf(e->phase) : 0.0f;
    float bob = walking ? fabsf(sinf(e->phase))*0.05f : 0.0f;
    float jiggle = 1.0f + 0.05f*sinf(e->phase*2.0f + 0.5f);

    // Jambes
    for (int k = -1; k <= 1; k += 2)
    {
        float swing = sw*k*0.28f;
        Vector3 foot = V3(0.27f*k*f, 0.14f + fmaxf(0, -swing)*0.25f, swing);
        Limb(root, V3(0.24f*k*f, 0.8f, 0), foot, 0.16f*sqrtf(f), pants);
        Box3(root, V3(foot.x, 0.07f, foot.z + 0.06f), NOROT, V3(0.24f, 0.14f, 0.36f), Flash((Color){ 40, 40, 40, 255 }, hf));
    }

    // Corps : t-shirt trop court qui laisse depasser le ventre
    Matrix body = MatrixMultiply(MatrixTranslate(0, bob, 0), root);
    Ball(body, V3(0, 0.88f, 0), V3(0.46f*f, 0.3f, 0.42f*f), pants);
    Ball(body, V3(0, 1.18f, 0.08f*f), V3(0.62f*f, 0.6f*jiggle, 0.62f*f), shirt);
    Ball(body, V3(0, 0.93f, 0.2f*f), V3(0.5f*f, 0.3f*jiggle, 0.5f*f), skin);
    Ball(body, V3(0, 0.95f, 0.69f*f), V3(0.03f, 0.04f, 0.02f), Shade(skin, 0.6f));   // nombril
    Ball(body, V3(0, 1.62f, 0), V3(0.48f*f, 0.33f, 0.4f*f), shirt);

    DrawHead(body, e, skin, hf);
    DrawArms(body, e, shirt, skin, sw);

    // Fleche de harpon plantee dans le ventre (taille independante du glouton)
    if (e->launched)
        DrawSpear(MatrixMultiply(MatrixMultiply(MatrixScale(1.0f/s, 1.0f/s, 1.0f/s), MatrixTranslate(0, 1.15f, -0.5f*f)), body));
}

void EnemiesDraw(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        if (e->state != ES_DEAD || e->airborne) DrawBlobShadow(e->pos, EnemyRadius(e)*1.1f);
        DrawEnemy(e);
    }
}
