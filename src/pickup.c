/*
 * pickup.c - Bonus au sol : apparition, ramassage, disparition et dessin.
 */
#include "pickup.h"
#include "config.h"
#include "player.h"
#include "audio.h"
#include "hud.h"
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define PICKUP_LIFE   25.0f   // un bonus disparait 25 s apres son apparition
#define PICKUP_RANGE  1.3f    // distance (m, dans le plan XZ) a laquelle on le ramasse

typedef struct {
    bool active;       // emplacement utilise ?
    PickupType type;
    Vector3 pos;       // position au sol
    float t;           // age en secondes (sert aussi a l'animation)
} Pickup;

static Pickup pickups[MAX_PICKUPS];

void PickupsClear(void) { memset(pickups, 0, sizeof(pickups)); }

// Appele quand un mercenaire meurt : tirage d'un nombre entre 0 et 99.
// 0..16 -> munitions, 17..27 -> salade, 28..99 -> rien.
void PickupDrop(Vector3 pos)
{
    int r = GetRandomValue(0, 99);
    if (r >= 28) return;   // 17 % munitions, 11 % salade
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        if (pickups[i].active) continue;
        // Pose au sol (y = 0) a l'endroit de la mort
        pickups[i] = (Pickup){ true, (r < 17) ? PK_AMMO : PK_SALAD, V3(pos.x, 0, pos.z), 0 };
        return;
    }
}

// Renvoie true si le joueur a pu utiliser le bonus
// (un joueur deja au maximum ne le ramasse pas : il reste pour un coequipier)
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
        // Salade : +35 PV, sans depasser 100
        if (p->hp >= 100) return false;
        p->hp = fminf(100, p->hp + 35);
        FeedAdd(LIME, "%s+35 PV (une salade, beurk)", PlayerTag(p));
    }
    // Le son n'est joue que sur la machine du joueur concerne
    if (p->local) PlaySfx(SND_PICK, 1.0f, 0.6f);
    return true;
}

// Simulation (solo ou hote) : vieillissement, expiration et ramassage.
void PickupsUpdate(float dt)
{
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        Pickup *k = &pickups[i];
        if (!k->active) continue;
        k->t += dt;
        if (k->t > PICKUP_LIFE) { k->active = false; continue; }
        // On s'arrete des que le bonus est ramasse (k->active devient faux)
        for (int j = 0; j < numPlayers && k->active; j++)
        {
            Player *p = &players[j];
            // Distance au carre dans le plan XZ (pas de racine carree necessaire)
            float dx = k->pos.x - p->pos.x, dz = k->pos.z - p->pos.z;
            // Ignore : joueur absent, mort, trop haut (sur une caisse) ou trop loin
            if (!p->connected || !p->alive || p->pos.y > 1.5f || dx*dx + dz*dz > PICKUP_RANGE*PICKUP_RANGE) continue;
            if (Collect(p, k->type)) k->active = false;
        }
    }
}

// Lecture d'un emplacement pour l'envoyer sur le reseau
bool PickupGet(int idx, int *type, Vector3 *pos, float *t)
{
    const Pickup *k = &pickups[idx];
    *type = k->type;
    *pos = k->pos;
    *t = k->t;
    return k->active;
}

// Ecriture d'un emplacement a partir des donnees recues de l'hote
void PickupSet(int idx, bool active, int type, Vector3 pos, float t)
{
    pickups[idx] = (Pickup){ active, (PickupType)type, pos, t };
}

// Cote client : on fait avancer l'age entre deux messages de l'hote pour que
// la rotation reste fluide, sans gerer ramassage ni expiration (c'est l'hote
// qui decide).
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
        // Pendant les 5 dernieres secondes : invisible 0,15 s toutes les 0,3 s
        if (k->t > PICKUP_LIFE - 5.0f && fmodf(k->t, 0.3f) < 0.15f) continue;   // clignote avant de disparaitre

        // Tourne sur lui-meme (2 rad/s) et flotte de haut en bas (+/- 10 cm autour de 45 cm)
        Matrix m = MatrixMultiply(MatrixRotateY(k->t*2.0f), MatrixTranslate(k->pos.x, 0.45f + sinf(k->t*3.0f)*0.1f, k->pos.z));
        DrawBlobShadow(k->pos, 0.35f);
        if (k->type == PK_AMMO)
        {
            // Caisse de munitions militaire : boite kaki, couvercle plus sombre, etiquette jaune
            Box3(m, V3(0, 0, 0), NOROT, V3(0.5f, 0.3f, 0.3f), (Color){ 80, 100, 50, 255 });
            Box3(m, V3(0, 0.16f, 0), NOROT, V3(0.52f, 0.04f, 0.32f), (Color){ 60, 75, 38, 255 });
            Box3(m, V3(0, 0, 0.152f), NOROT, V3(0.3f, 0.12f, 0.01f), (Color){ 240, 200, 40, 255 });
        }
        else
        {
            // Salade : bol blanc + 7 feuilles disposees en cercle (angle j*0.9 rad),
            // 1 sur 3 est rouge (une tomate)
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
