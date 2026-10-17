#include "effects.h"
#include "config.h"
#include "net.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

typedef struct {
    Vector3 p, v;
    float life, maxLife, size, gravity;
    Color c;
} Particle;

typedef struct { Vector3 a, b; float life; } Tracer;

#define TRACER_LIFE 0.06f

static Particle parts[MAX_PARTICLES];
static Tracer tracers[MAX_TRACERS];

void EffectsClear(void)
{
    memset(parts, 0, sizeof(parts));
    memset(tracers, 0, sizeof(tracers));
}

void SpawnParticle(Vector3 p, Vector3 v, float life, float size, float gravity, Color c)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (parts[i].life > 0) continue;
        parts[i] = (Particle){ p, v, life, life, size, gravity, c };
        return;
    }
}

void BurstImpact(Vector3 p, Vector3 dir, int count)
{
    NetRecordEvent(EV_IMPACT, p, dir, count, 0, 0);
    for (int i = 0; i < count; i++)
    {
        Color c = (GetRandomValue(0, 2) == 0) ? (Color){ 120, 10, 10, 255 } : (Color){ 200, 20, 15, 255 };
        Vector3 v = Vector3Add(Vector3Scale(dir, Randf(1.0f, 4.0f)), V3(Randf(-2, 2), Randf(0.5f, 4), Randf(-2, 2)));
        SpawnParticle(p, v, Randf(0.5f, 1.2f), Randf(0.04f, 0.1f), 12.0f, c);
    }
}

void BurstSparks(Vector3 p, Vector3 normal)
{
    NetRecordEvent(EV_SPARKS, p, normal, 0, 0, 0);
    for (int i = 0; i < 8; i++)
    {
        Vector3 v = Vector3Add(Vector3Scale(normal, Randf(1.0f, 4.0f)), V3(Randf(-2, 2), Randf(-1, 3), Randf(-2, 2)));
        SpawnParticle(p, v, Randf(0.15f, 0.35f), 0.03f, 9.0f, (Color){ 255, 220, 120, 255 });
    }
    for (int i = 0; i < 5; i++)   // poussiere
    {
        Vector3 v = Vector3Add(Vector3Scale(normal, Randf(0.3f, 1.2f)), V3(Randf(-0.4f, 0.4f), Randf(0.2f, 0.9f), Randf(-0.4f, 0.4f)));
        SpawnParticle(p, v, Randf(0.4f, 0.8f), Randf(0.06f, 0.12f), -0.5f, (Color){ 150, 140, 125, 200 });
    }
}

void BurstFeathers(Vector3 p)
{
    NetRecordEvent(EV_FEATHERS, p, V3(0, 0, 0), 0, 0, 0);
    for (int i = 0; i < 14; i++)
    {
        Color c = GetRandomValue(0, 2) ? (Color){ 255, 214, 50, 255 } : (Color){ 255, 240, 160, 255 };
        SpawnParticle(p, V3(Randf(-1.2f, 1.2f), Randf(0.5f, 2.0f), Randf(-1.2f, 1.2f)), Randf(0.8f, 1.4f), Randf(0.025f, 0.05f), 3.0f, c);
    }
}

void AddTracer(Vector3 from, Vector3 to)
{
    NetRecordEvent(EV_TRACER, from, to, 0, 0, 0);
    for (int i = 0; i < MAX_TRACERS; i++)
    {
        if (tracers[i].life > 0) continue;
        tracers[i] = (Tracer){ from, to, TRACER_LIFE };
        return;
    }
}

void SfxWorld(SndId id, float pitch, float volume)
{
    NetRecordEvent(EV_SOUND, V3(0, 0, 0), V3(0, 0, 0), id, pitch, volume);
    PlaySfx(id, pitch, volume);
}

void EffectsUpdate(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->v.y -= p->gravity*dt;
        p->p = Vector3Add(p->p, Vector3Scale(p->v, dt));
        if (p->p.y < 0.02f)   // retombe au sol et glisse un peu
        {
            p->p.y = 0.02f;
            p->v = Vector3Scale(p->v, 0.3f);
            p->v.y = 0;
        }
    }
    for (int i = 0; i < MAX_TRACERS; i++) tracers[i].life -= dt;
}

void EffectsDraw(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        Color c = p->c;
        c.a = (unsigned char)(c.a*fminf(1.0f, p->life/p->maxLife*2.0f));
        DrawCube(p->p, p->size, p->size, p->size, c);
    }
    for (int i = 0; i < MAX_TRACERS; i++)
    {
        if (tracers[i].life <= 0) continue;
        // Trainee courte qui "voyage" le long de la trajectoire
        Vector3 a = tracers[i].a, d = Vector3Subtract(tracers[i].b, a);
        float t = 1.0f - tracers[i].life/TRACER_LIFE;
        Vector3 s = Vector3Add(a, Vector3Scale(d, fmaxf(0, t - 0.25f)));
        Vector3 e = Vector3Add(a, Vector3Scale(d, fminf(1, t + 0.15f)));
        DrawCylinderEx(s, e, 0.012f, 0.012f, 4, (Color){ 255, 230, 150, 200 });
    }
}
