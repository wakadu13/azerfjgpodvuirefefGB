/*
 * effects.c - Systeme de particules tres simple et balles tracantes.
 *
 * Les particules sont de petits cubes soumis a la gravite, stockes dans un
 * tableau de taille fixe : une case dont life <= 0 est libre. Chaque fonction
 * "Burst..." enregistre aussi un evenement reseau, pour que les autres
 * joueurs voient le meme effet chez eux.
 */
#include "effects.h"
#include "config.h"
#include "net.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

typedef struct {
    Vector3 p, v;                              // position (m) et vitesse (m/s)
    float life, maxLife, size, gravity;        // temps restant, duree totale, cote du cube, gravite
    Color c;
} Particle;

// Trainee de balle de a vers b ; life decroit de TRACER_LIFE a 0
typedef struct { Vector3 a, b; float life; } Tracer;

#define TRACER_LIFE 0.06f   // une trainee reste visible 60 ms (quelques images)

static Particle parts[MAX_PARTICLES];
static Tracer tracers[MAX_TRACERS];

// Remise a zero : life = 0 partout, donc toutes les cases sont libres
void EffectsClear(void)
{
    memset(parts, 0, sizeof(parts));
    memset(tracers, 0, sizeof(tracers));
}

// Cherche la premiere case libre et y place la particule. Si le tableau est
// plein, la particule est simplement abandonnee (pas grave visuellement).
void SpawnParticle(Vector3 p, Vector3 v, float life, float size, float gravity, Color c)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (parts[i].life > 0) continue;
        parts[i] = (Particle){ p, v, life, life, size, gravity, c };
        return;
    }
}

// Gerbe de sang quand une balle touche un mercenaire : "count" gouttes
// projetees dans la direction du tir (dir) avec une dispersion aleatoire et
// une composante vers le haut, puis retombant avec une forte gravite (12).
// 1 goutte sur 3 est d'un rouge plus sombre.
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

// Impact sur le decor : 8 etincelles jaunes rapides et breves, rejaillissant
// selon la normale de la surface, plus 5 petits nuages de poussiere lents qui
// montent doucement (gravite negative).
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

// 14 plumes jaunes (2 sur 3) ou jaune pale, projetees vers le haut et qui
// retombent lentement (gravite faible de 3 m/s^2, comme une plume qui flotte).
void BurstFeathers(Vector3 p)
{
    NetRecordEvent(EV_FEATHERS, p, V3(0, 0, 0), 0, 0, 0);
    for (int i = 0; i < 14; i++)
    {
        Color c = GetRandomValue(0, 2) ? (Color){ 255, 214, 50, 255 } : (Color){ 255, 240, 160, 255 };
        SpawnParticle(p, V3(Randf(-1.2f, 1.2f), Randf(0.5f, 2.0f), Randf(-1.2f, 1.2f)), Randf(0.8f, 1.4f), Randf(0.025f, 0.05f), 3.0f, c);
    }
}

// Ajoute une trainee de balle (meme principe de case libre que les particules)
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

// Joue un son localement ET l'envoie aux autres joueurs (contrairement a
// PlaySfx seul, reserve aux sons personnels comme ceux de l'interface).
void SfxWorld(SndId id, float pitch, float volume)
{
    NetRecordEvent(EV_SOUND, V3(0, 0, 0), V3(0, 0, 0), id, pitch, volume);
    PlaySfx(id, pitch, volume);
}

// Physique des particules par integration d'Euler explicite :
//   vitesse  += acceleration * dt   (ici seulement la gravite, sur Y)
//   position += vitesse * dt
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
            // Pose sur le sol (2 cm pour rester visible), forte friction (on
            // garde 30 % de la vitesse) et plus de mouvement vertical.
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
        // Fondu : opacite pleine pendant la premiere moitie de vie, puis
        // decroissance lineaire jusqu'a 0 (life/maxLife*2 depasse 1 au debut).
        Color c = p->c;
        c.a = (unsigned char)(c.a*fminf(1.0f, p->life/p->maxLife*2.0f));
        DrawCube(p->p, p->size, p->size, p->size, c);
    }
    for (int i = 0; i < MAX_TRACERS; i++)
    {
        if (tracers[i].life <= 0) continue;
        // Trainee courte qui "voyage" le long de la trajectoire
        // t va de 0 (tir) a 1 (fin de vie). On dessine le morceau de la
        // trajectoire compris entre t - 0.25 et t + 0.15 (bornes entre 0 et 1) :
        // un trait d'environ 40 % de la longueur qui avance vers la cible.
        Vector3 a = tracers[i].a, d = Vector3Subtract(tracers[i].b, a);
        float t = 1.0f - tracers[i].life/TRACER_LIFE;
        Vector3 s = Vector3Add(a, Vector3Scale(d, fmaxf(0, t - 0.25f)));
        Vector3 e = Vector3Add(a, Vector3Scale(d, fminf(1, t + 0.15f)));
        // Cylindre tres fin (1,2 cm de rayon, 4 cotes suffisent) jaune pale semi-transparent
        DrawCylinderEx(s, e, 0.012f, 0.012f, 4, (Color){ 255, 230, 150, 200 });
    }
}
