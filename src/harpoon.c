#include "harpoon.h"
#include "config.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "audio.h"
#include "net.h"
#include "weapon.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define MAX_HARPOONS   32
#define FLIGHT_TIME    3.0f    // une fleche qui ne touche rien disparait
#define STUCK_TIME     6.0f    // une fleche plantee dans le decor reste visible

typedef struct {
    bool active, flying;
    int owner;                 // joueur qui a tire (-1 : fleche d'un autre PC)
    Vector3 pos, vel;
    float life;
} Harpoon;

static Harpoon harpoons[MAX_HARPOONS];

void HarpoonsClear(void) { memset(harpoons, 0, sizeof(harpoons)); }

static void Spawn(Vector3 pos, Vector3 vel, int owner)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        if (harpoons[i].active) continue;
        harpoons[i] = (Harpoon){ true, true, owner, pos, vel, FLIGHT_TIME };
        return;
    }
}

void HarpoonFire(Player *p)
{
    // La fleche part de la pointe du harpon et vise le point sous le reticule
    Vector3 start = WeaponMuzzleWorld(p);
    Vector3 eye = PlayerEye(p), f = PlayerForward(p);
    Ray ray = { eye, f };
    float dist = 200.0f;
    Vector3 normal;
    bool head;
    MapRaycast(ray, &dist, &normal);
    EnemyRaycast(ray, &dist, &head);
    Vector3 dir = (dist < 1.5f) ? f : Vector3Normalize(Vector3Subtract(Vector3Add(eye, Vector3Scale(f, dist)), start));

    Vector3 vel = Vector3Scale(dir, HARPOON_SPEED);
    Spawn(start, vel, p->id);
    NetRecordEvent(EV_HARPOON, start, vel, 0, 0, 0);
    SfxWorld(SND_HARPOON, 1.0f, 0.8f);
}

void HarpoonSpawnRemote(Vector3 pos, Vector3 vel) { Spawn(pos, vel, -1); }

static void HitEnemy(const Harpoon *h, int enemy, Vector3 dir)
{
    // Seul le tireur decide de la touche ; les fleches des autres PC sont visuelles
    if (h->owner < 0 || !players[h->owner].local) return;
    Player *shooter = &players[h->owner];
    Enemy *e = &enemies[enemy];
    if (NetIsClient())
    {
        EnemyPredictHarpoon(e, dir, shooter);
        NetReportHit(enemy, 0, false, h->pos, dir, HIT_HARPOON);
    }
    else EnemyHarpoon(e, dir, shooter);
}

void HarpoonsUpdate(float dt)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        Harpoon *h = &harpoons[i];
        if (!h->active) continue;
        h->life -= dt;
        if (h->life <= 0) { h->active = false; continue; }
        if (!h->flying) continue;

        h->vel.y -= HARPOON_GRAVITY*dt;
        float step = Vector3Length(h->vel)*dt;
        Ray ray = { h->pos, Vector3Normalize(h->vel) };
        float dist = step;
        Vector3 normal;
        bool head;
        bool hitWorld = MapRaycast(ray, &dist, &normal);
        int enemy = EnemyRaycast(ray, &dist, &head);

        if (enemy >= 0)
        {
            HitEnemy(h, enemy, ray.direction);
            h->active = false;   // la fleche reste plantee dans le mercenaire (dessinee avec lui)
            continue;
        }
        if (hitWorld)
        {
            // Plantee dans le decor
            h->pos = Vector3Add(h->pos, Vector3Scale(ray.direction, dist));
            h->flying = false;
            h->life = STUCK_TIME;
            NetSuppress(true);   // chaque PC fait ses propres etincelles
            BurstSparks(h->pos, normal);
            NetSuppress(false);
            PlaySfx(SND_THUD, 1.3f, 0.5f);
            continue;
        }
        h->pos = Vector3Add(h->pos, Vector3Scale(h->vel, dt));
        if (fabsf(h->pos.x) > 100 || fabsf(h->pos.z) > 100 || h->pos.y < -5) h->active = false;
    }
}

void HarpoonsDraw(void)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        const Harpoon *h = &harpoons[i];
        if (!h->active) continue;
        // Repere de la fleche : pointe a la position, orientee selon la vitesse
        Vector3 dir = Vector3Normalize(h->vel);
        Quaternion q = (dir.z > 0.999f) ? QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI)
                                        : QuaternionFromVector3ToVector3((Vector3){ 0, 0, -1 }, dir);
        Matrix m = MatrixMultiply(QuaternionToMatrix(q), MatrixTranslate(h->pos.x, h->pos.y, h->pos.z));
        DrawSpear(m);
    }
}
