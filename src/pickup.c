#include "pickup.h"
#include "config.h"
#include "player.h"
#include "audio.h"
#include "hud.h"
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define PICKUP_LIFE   25.0f
#define PICKUP_RANGE  1.3f

typedef struct {
    bool active;
    PickupType type;
    Vector3 pos;
    float t;
} Pickup;

static Pickup pickups[MAX_PICKUPS];

void PickupsClear(void) { memset(pickups, 0, sizeof(pickups)); }

void PickupDrop(Vector3 pos)
{
    int r = GetRandomValue(0, 99);
    if (r >= 28) return;   // 17 % munitions, 11 % salade
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        if (pickups[i].active) continue;
        pickups[i] = (Pickup){ true, (r < 17) ? PK_AMMO : PK_SALAD, V3(pos.x, 0, pos.z), 0 };
        return;
    }
}

// Renvoie true si le joueur a pu utiliser le bonus
static bool Collect(Player *p, PickupType type)
{
    if (type == PK_AMMO)
    {
        if (p->w.reserve >= MAX_RESERVE && p->w.harpoons >= HARPOON_MAX) return false;
        PlayerGiveAmmo(p, 60);
        PlayerGiveHarpoons(p, 1);
        FeedAdd(GREEN, "%s+60 munitions 5.56 et +1 harpon", PlayerTag(p));
    }
    else
    {
        if (p->hp >= 100) return false;
        p->hp = fminf(100, p->hp + 35);
        FeedAdd(LIME, "%s+35 PV (une salade, beurk)", PlayerTag(p));
    }
    if (p->local) PlaySfx(SND_PICK, 1.0f, 0.6f);
    return true;
}

void PickupsUpdate(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        Pickup *k = &pickups[i];
        if (!k->active) continue;
        k->t += dt;
        if (k->t > PICKUP_LIFE) { k->active = false; continue; }
        for (int j = 0; j < numPlayers && k->active; j++)
        {
            Player *p = &players[j];
            float dx = k->pos.x - p->pos.x, dz = k->pos.z - p->pos.z;
            if (!p->connected || !p->alive || p->pos.y > 1.5f || dx*dx + dz*dz > PICKUP_RANGE*PICKUP_RANGE) continue;
            if (Collect(p, k->type)) k->active = false;
        }
    }
}

bool PickupGet(int idx, int *type, Vector3 *pos, float *t)
{
    const Pickup *k = &pickups[idx];
    *type = k->type;
    *pos = k->pos;
    *t = k->t;
    return k->active;
}

void PickupSet(int idx, bool active, int type, Vector3 pos, float t)
{
    pickups[idx] = (Pickup){ active, (PickupType)type, pos, t };
}

void PickupsAnimate(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; i++) if (pickups[i].active) pickups[i].t += dt;
}

void PickupsDraw(void)
{
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        Pickup *k = &pickups[i];
        if (!k->active) continue;
        if (k->t > PICKUP_LIFE - 5.0f && fmodf(k->t, 0.3f) < 0.15f) continue;   // clignote avant de disparaitre

        Matrix m = MatrixMultiply(MatrixRotateY(k->t*2.0f), MatrixTranslate(k->pos.x, 0.45f + sinf(k->t*3.0f)*0.1f, k->pos.z));
        DrawBlobShadow(k->pos, 0.35f);
        if (k->type == PK_AMMO)
        {
            Box3(m, V3(0, 0, 0), NOROT, V3(0.5f, 0.3f, 0.3f), (Color){ 80, 100, 50, 255 });
            Box3(m, V3(0, 0.16f, 0), NOROT, V3(0.52f, 0.04f, 0.32f), (Color){ 60, 75, 38, 255 });
            Box3(m, V3(0, 0, 0.152f), NOROT, V3(0.3f, 0.12f, 0.01f), (Color){ 240, 200, 40, 255 });
        }
        else
        {
            Ball(m, V3(0, -0.05f, 0), V3(0.3f, 0.15f, 0.3f), WHITE);
            for (int j = 0; j < 7; j++)
            {
                float a = j*0.9f;
                Color leaf = (j % 3) ? (Color){ 90, 190, 60, 255 } : (Color){ 220, 50, 40, 255 };
                Ball(m, V3(cosf(a)*0.13f, 0.08f, sinf(a)*0.13f), V3(0.12f, 0.06f, 0.1f), leaf);
            }
        }
    }
}
