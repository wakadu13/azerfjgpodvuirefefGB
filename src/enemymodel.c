/*
 * enemymodel.c - Modele 3D des mercenaires : treillis camoufle (motif
 * calcule par le shader), visage grime, chaines metalliques enroulees
 * autour du corps, casque / beret / bandana.
 *
 * Les chaines de chaque carrure forment un seul mesh, genere a la demande
 * puis garde en cache : des dizaines de maillons pour un seul appel de
 * dessin.
 */
#include "enemy.h"
#include "weapon.h"
#include "render.h"
#include "util.h"

#include "raymath.h"

#include <math.h>

typedef struct { Color base, a, b, c; } Camo;   // couleur de fond + 3 couleurs de taches

static const Camo CAMOS[CAMO_COUNT] = {
    { { 92, 102, 58, 255 }, { 62, 50, 34, 255 }, { 34, 38, 28, 255 }, { 130, 134, 86, 255 } },       // foret
    { { 192, 168, 122, 255 }, { 150, 118, 80, 255 }, { 108, 88, 60, 255 }, { 218, 204, 165, 255 } },  // desert
    { { 118, 120, 124, 255 }, { 72, 74, 78, 255 }, { 40, 41, 45, 255 }, { 172, 173, 176, 255 } },     // urbain
    { { 66, 86, 54, 255 }, { 42, 60, 38, 255 }, { 104, 118, 72, 255 }, { 28, 32, 24, 255 } },         // jungle
};
static const Camo CAMO_BOSS = { { 44, 46, 44, 255 }, { 24, 24, 24, 255 }, { 74, 54, 48, 255 }, { 92, 94, 90, 255 } };   // nuit
static const Color SKINS[SKIN_COUNT] = { { 245, 200, 165, 255 }, { 222, 172, 132, 255 }, { 182, 128, 90, 255 }, { 120, 82, 56, 255 } };

static const Color PAINT_BLACK = { 26, 28, 22, 255 };
static const Color PAINT_GREEN = { 62, 88, 44, 255 };
static const Color STEEL = { 158, 162, 168, 255 };
static const Color BOOTS = { 34, 30, 26, 255 };
static const Color BRASS = { 230, 180, 50, 255 };

// Volumes du torse (repere du corps, avant = +z) : bassin, ventre, epaules.
// Les chaines s'enroulent autour, la carrure f elargit x et z.
typedef struct { Vector3 c, r; } Ellipsoid;

static void TorsoShape(float f, Ellipsoid out[3])
{
    out[0] = (Ellipsoid){ V3(0, 0.92f, 0), V3(0.46f*f, 0.3f, 0.4f*f) };
    out[1] = (Ellipsoid){ V3(0, 1.28f, 0.04f*f), V3(0.57f*f, 0.52f, 0.48f*f) };
    out[2] = (Ellipsoid){ V3(0, 1.62f, 0), V3(0.52f*f, 0.3f, 0.42f*f) };
}

//----------------------------------------------------------------------------------
// Chaines
//----------------------------------------------------------------------------------
#define LINK_SEG    10    // segments autour d'un maillon
#define LINK_SIDES  6     // segments autour du fil
#define LINK_VERTS  (LINK_SEG*LINK_SIDES)
#define CHAIN_STEPS 32    // carrures en cache : 1.0 a 2.55 par pas de 0.05
#define PATH_SAMPLES 200

typedef enum { CHAIN_LIGHT, CHAIN_HEAVY, CHAIN_KINDS } ChainKind;   // en bandouliere, ou en croix (colosse, chef)

static Model chainModels[CHAIN_KINDS][CHAIN_STEPS];
static bool chainReady[CHAIN_KINDS][CHAIN_STEPS];

// Point de la surface du torse dans la direction d depuis c (le plus
// exterieur des trois volumes), avec la normale a cet endroit.
static Vector3 SurfaceAlong(const Ellipsoid *shape, Vector3 c, Vector3 d, Vector3 *normal)
{
    float best = 0.0f;
    *normal = d;
    for (int i = 0; i < 3; i++)
    {
        Vector3 r = shape[i].r;
        Vector3 o = V3((c.x - shape[i].c.x)/r.x, (c.y - shape[i].c.y)/r.y, (c.z - shape[i].c.z)/r.z);
        Vector3 v = V3(d.x/r.x, d.y/r.y, d.z/r.z);
        float a = Vector3DotProduct(v, v), b = Vector3DotProduct(o, v), cc = Vector3DotProduct(o, o) - 1.0f;
        float disc = b*b - a*cc;
        if (disc < 0) continue;
        float t = (-b + sqrtf(disc))/a;
        if (t <= best) continue;
        best = t;
        Vector3 p = Vector3Subtract(Vector3Add(c, Vector3Scale(d, t)), shape[i].c);
        *normal = Vector3Normalize(V3(p.x/(r.x*r.x), p.y/(r.y*r.y), p.z/(r.z*r.z)));
    }
    return Vector3Add(c, Vector3Scale(d, best));
}

// Une boucle de chaine : directions u (cos) et v (sin) depuis le centre c,
// "sag" fait pendre l'avant (chaine de taille).
typedef struct { Vector3 c, u, v; float sag; } ChainLoop;

static Vector3 LoopPoint(const Ellipsoid *shape, const ChainLoop *l, float a, Vector3 *normal)
{
    float front = fmaxf(0.0f, sinf(a));
    Vector3 d = Vector3Add(Vector3Scale(l->u, cosf(a)), Vector3Scale(l->v, sinf(a)));
    d = Vector3Normalize(Vector3Add(d, V3(0, -l->sag*front*front, 0)));
    return SurfaceAlong(shape, l->c, d, normal);
}

// Ajoute un maillon (tore aplati) : t = sens de la chaine, w = second axe du maillon.
static void AddLink(Mesh *m, Vector3 center, Vector3 t, Vector3 w, float a, float b, float r)
{
    Vector3 q = Vector3Normalize(Vector3CrossProduct(t, w));
    int base = m->vertexCount;
    for (int i = 0; i < LINK_SEG; i++)
    {
        float phi = 2*PI*i/LINK_SEG;
        Vector3 ring = Vector3Add(Vector3Scale(t, a*cosf(phi)), Vector3Scale(w, b*sinf(phi)));
        Vector3 rn = Vector3Normalize(Vector3Add(Vector3Scale(t, cosf(phi)/a), Vector3Scale(w, sinf(phi)/b)));
        for (int j = 0; j < LINK_SIDES; j++)
        {
            float psi = 2*PI*j/LINK_SIDES;
            Vector3 n = Vector3Add(Vector3Scale(rn, cosf(psi)), Vector3Scale(q, sinf(psi)));
            Vector3 p = Vector3Add(Vector3Add(center, ring), Vector3Scale(n, r));
            int k = m->vertexCount++;
            m->vertices[k*3] = p.x; m->vertices[k*3 + 1] = p.y; m->vertices[k*3 + 2] = p.z;
            m->normals[k*3] = n.x; m->normals[k*3 + 1] = n.y; m->normals[k*3 + 2] = n.z;
        }
    }
    // Sens des triangles : on verifie le premier contre sa normale (faces avant = sens trigo)
    Vector3 p0 = V3(m->vertices[base*3], m->vertices[base*3 + 1], m->vertices[base*3 + 2]);
    Vector3 p1 = V3(m->vertices[(base + LINK_SIDES)*3], m->vertices[(base + LINK_SIDES)*3 + 1], m->vertices[(base + LINK_SIDES)*3 + 2]);
    Vector3 p2 = V3(m->vertices[(base + LINK_SIDES + 1)*3], m->vertices[(base + LINK_SIDES + 1)*3 + 1], m->vertices[(base + LINK_SIDES + 1)*3 + 2]);
    Vector3 n0 = V3(m->normals[base*3], m->normals[base*3 + 1], m->normals[base*3 + 2]);
    bool flip = Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)), n0) < 0;
    for (int i = 0; i < LINK_SEG; i++)
        for (int j = 0; j < LINK_SIDES; j++)
        {
            int i0 = base + i*LINK_SIDES + j, i1 = base + ((i + 1) % LINK_SEG)*LINK_SIDES + j;
            int i2 = base + ((i + 1) % LINK_SEG)*LINK_SIDES + (j + 1) % LINK_SIDES, i3 = base + i*LINK_SIDES + (j + 1) % LINK_SIDES;
            unsigned short *tri = &m->indices[m->triangleCount*3];
            tri[0] = (unsigned short)i0; tri[1] = (unsigned short)(flip ? i2 : i1); tri[2] = (unsigned short)(flip ? i1 : i2);
            tri[3] = (unsigned short)i0; tri[4] = (unsigned short)(flip ? i3 : i2); tri[5] = (unsigned short)(flip ? i2 : i3);
            m->triangleCount += 2;
        }
}

// Maillons le long d'une boucle, un a plat, un debout, en alternance
static void AddLoop(Mesh *m, const Ellipsoid *shape, const ChainLoop *l, float scale, bool build)
{
    float a = 0.05f*scale, b = 0.03f*scale, r = 0.012f*scale, pitch = 2*(a - r);
    Vector3 pts[PATH_SAMPLES + 1], nrm[PATH_SAMPLES + 1];
    float len[PATH_SAMPLES + 1];
    len[0] = 0;
    for (int i = 0; i <= PATH_SAMPLES; i++)
    {
        pts[i] = LoopPoint(shape, l, 2*PI*i/PATH_SAMPLES, &nrm[i]);
        if (i > 0) len[i] = len[i - 1] + Vector3Distance(pts[i], pts[i - 1]);
    }
    int count = (int)(len[PATH_SAMPLES]/pitch) & ~1;   // nombre pair : l'alternance boucle bien
    if (!build) { m->vertexCount += count*LINK_VERTS; m->triangleCount += count*LINK_VERTS*2; return; }

    int k = 0;
    for (int n = 0; n < count; n++)
    {
        float s = len[PATH_SAMPLES]*n/count;
        while (k < PATH_SAMPLES - 1 && len[k + 1] < s) k++;
        float f = (s - len[k])/fmaxf(len[k + 1] - len[k], 1e-6f);
        Vector3 p = Vector3Lerp(pts[k], pts[k + 1], f);
        Vector3 t = Vector3Normalize(Vector3Subtract(pts[k + 1], pts[k]));
        Vector3 nm = Vector3Normalize(Vector3Lerp(nrm[k], nrm[k + 1], f));
        nm = Vector3Normalize(Vector3Subtract(nm, Vector3Scale(t, Vector3DotProduct(nm, t))));
        bool standing = n & 1;
        Vector3 w = standing ? nm : Vector3CrossProduct(t, nm);
        Vector3 center = Vector3Add(p, Vector3Scale(nm, (standing ? b + r : r) + 0.004f));
        AddLink(m, center, t, w, a, b, r);
    }
}

static void ChainLoops(ChainKind kind, float f, ChainLoop *loops, int *count)
{
    float tilt = 0.8f;
    Vector3 torso = V3(0, 1.3f, 0.02f*f);
    int n = 0;
    loops[n++] = (ChainLoop){ torso, V3(cosf(tilt), sinf(tilt), 0), V3(0, 0, 1), 0 };              // en bandouliere
    if (kind == CHAIN_HEAVY) loops[n++] = (ChainLoop){ torso, V3(-cosf(tilt), sinf(tilt), 0), V3(0, 0, 1), 0 };   // en croix
    loops[n++] = (ChainLoop){ V3(0, 1.0f, 0.02f*f), V3(1, 0, 0), V3(0, 0, 1), 0.15f };            // autour de la taille
    *count = n;
}

static Model *ChainModel(ChainKind kind, float f)
{
    int step = (int)Clamp(roundf((f - 1.0f)/0.05f), 0, CHAIN_STEPS - 1);
    Model *model = &chainModels[kind][step];
    if (chainReady[kind][step]) return model;

    float fq = 1.0f + step*0.05f;
    float scale = (kind == CHAIN_HEAVY) ? 1.3f : 1.0f;
    Ellipsoid shape[3];
    TorsoShape(fq, shape);
    ChainLoop loops[3];
    int nLoops;
    ChainLoops(kind, fq, loops, &nLoops);

    // Deux passes : compter, puis remplir
    Mesh mesh = { 0 };
    for (int i = 0; i < nLoops; i++) AddLoop(&mesh, shape, &loops[i], scale, false);
    int verts = mesh.vertexCount, tris = mesh.triangleCount;
    mesh = (Mesh){ 0 };
    mesh.vertices = (float *)MemAlloc(verts*3*sizeof(float));
    mesh.normals = (float *)MemAlloc(verts*3*sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(tris*3*sizeof(unsigned short));
    for (int i = 0; i < nLoops; i++) AddLoop(&mesh, shape, &loops[i], scale, true);

    *model = RenderLoadModel(mesh);
    chainReady[kind][step] = true;
    return model;
}

void EnemyModelsUnload(void)
{
    for (int k = 0; k < CHAIN_KINDS; k++)
        for (int i = 0; i < CHAIN_STEPS; i++)
            if (chainReady[k][i]) { RenderUnloadModel(chainModels[k][i]); chainReady[k][i] = false; }
}

//----------------------------------------------------------------------------------
// Tete : visage grime
//----------------------------------------------------------------------------------
static const Vector3 HEAD_C = { 0, 2.02f, 0.02f };
static const Vector3 HEAD_R = { 0.29f, 0.3f, 0.29f };

// Point du visage dans la direction (lacet, tangage) et sa normale
static Vector3 OnHead(float yaw, float pitch, Vector3 *normal)
{
    Vector3 d = V3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
    float t = 1.0f/sqrtf(powf(d.x/HEAD_R.x, 2) + powf(d.y/HEAD_R.y, 2) + powf(d.z/HEAD_R.z, 2));
    Vector3 p = Vector3Scale(d, t);
    *normal = Vector3Normalize(V3(p.x/(HEAD_R.x*HEAD_R.x), p.y/(HEAD_R.y*HEAD_R.y), p.z/(HEAD_R.z*HEAD_R.z)));
    return Vector3Add(HEAD_C, p);
}

// Trait plat pose sur le visage (peinture, sourcils, bouche) ; angle = inclinaison
static void Stroke(Matrix body, float yaw, float pitch, float angle, float len, float wid, Color c)
{
    Vector3 n;
    Vector3 p = OnHead(yaw, pitch, &n);
    Vector3 east = Vector3Normalize(Vector3CrossProduct(V3(0, 1, 0), n));
    Vector3 north = Vector3CrossProduct(n, east);
    Vector3 x = Vector3Add(Vector3Scale(east, cosf(angle)), Vector3Scale(north, sinf(angle)));
    BallAxes(body, Vector3Subtract(p, Vector3Scale(n, 0.008f)), x, Vector3CrossProduct(n, x), V3(len, wid, 0.022f), c);
}

static void DrawFacePaint(Matrix body, int style, float hf)
{
    Color black = Flash(PAINT_BLACK, hf), green = Flash(PAINT_GREEN, hf);
    switch (style)
    {
        case 0:   // bandes sous les yeux + front vert
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.4f*k, 0.02f, -0.15f*k, 0.09f, 0.024f, black);
                Stroke(body, 0.44f*k, -0.12f, -0.2f*k, 0.08f, 0.02f, green);
            }
            Stroke(body, 0, 0.45f, 0, 0.17f, 0.035f, green);
            break;
        case 1:   // masque noir sur les yeux, joues vertes
            Stroke(body, 0, 0.17f, 0, 0.27f, 0.065f, black);
            for (int k = -1; k <= 1; k += 2) Stroke(body, 0.6f*k, -0.08f, 0.6f*k, 0.07f, 0.03f, green);
            break;
        default:  // rayures de tigre
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.22f*k, 0.4f, 0.9f*k, 0.08f, 0.02f, black);
                Stroke(body, 0.5f*k, 0.08f, 0.9f*k, 0.09f, 0.022f, black);
                Stroke(body, 0.36f*k, -0.12f, 0.9f*k, 0.08f, 0.02f, green);
            }
            Stroke(body, 0, 0.1f, PI/2, 0.12f, 0.025f, black);   // arete du nez
            break;
    }
}

static void DrawHead(Matrix body, const Enemy *e, Color skin, float hf)
{
    float shout = 0.3f;   // bouche : crie quand il frappe
    if (e->attackAnim > 0) shout = 1.0f;
    else if (e->state == ES_GRAB) shout = 0.5f + 0.5f*sinf(e->grabT*9.0f);

    Seg(body, V3(0, 1.72f, 0.02f), V3(0, 1.92f, 0.03f), 0.15f, skin);              // cou
    Ball(body, V3(0, 1.88f, 0.08f), V3(0.23f, 0.13f, 0.21f), skin);                 // machoire carree
    Ball(body, HEAD_C, HEAD_R, skin);
    for (int k = -1; k <= 1; k += 2) Ball(body, V3(0.285f*k, 2.02f, 0.0f), V3(0.04f, 0.08f, 0.06f), skin);   // oreilles

    DrawFacePaint(body, e->paint % PAINT_COUNT, hf);

    Vector3 n;
    Vector3 nose = OnHead(0, -0.05f, &n);
    Ball(body, Vector3Add(nose, Vector3Scale(n, 0.02f)), V3(0.04f, 0.06f, 0.05f), Shade(skin, 0.92f));
    for (int k = -1; k <= 1; k += 2)
    {
        Vector3 eye = OnHead(0.36f*k, 0.17f, &n);
        Ball(body, Vector3Subtract(eye, Vector3Scale(n, 0.004f)), V3(0.05f, 0.036f, 0.035f), Flash((Color){ 235, 235, 225, 255 }, hf));
        Ball(body, Vector3Add(eye, Vector3Scale(n, 0.022f)), V3(0.022f, 0.024f, 0.014f), BLACK);
        Stroke(body, 0.34f*k, 0.32f, 0.35f*k, 0.075f, 0.02f, Flash((Color){ 40, 30, 22, 255 }, hf));   // sourcils fronces
    }
    Stroke(body, 0, -0.26f, 0, 0.09f, 0.012f + 0.035f*shout, (Color){ 80, 20, 20, 255 });

    switch (e->gear)
    {
        case GEAR_BERET:
        {
            bool boss = e->type == E_BOSS;
            Color beret = Flash(boss ? (Color){ 30, 30, 32, 255 } : (Color){ 112, 26, 32, 255 }, hf);
            Seg(body, V3(0, 2.14f, 0.02f), V3(0, 2.17f, 0.02f), 0.28f, Shade(beret, 0.7f));
            Ball(MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.28f), MatrixTranslate(0.05f, 2.22f, 0.0f)), body),
                 V3(0, 0, 0), V3(0.32f, 0.09f, 0.31f), beret);
            Ball(body, V3(0.15f, 2.2f, 0.25f), V3(0.04f, 0.045f, 0.015f), Flash(boss ? BRASS : STEEL, hf));   // insigne
        } break;
        case GEAR_BANDANA:
        {
            Color band = Flash((Color){ 150, 30, 28, 255 }, hf);
            Seg(body, V3(0, 2.12f, 0.02f), V3(0, 2.2f, 0.02f), 0.275f, band);
            Box3(body, V3(0.06f, 2.12f, -0.33f), V3(0.4f, 0, 0.3f), V3(0.05f, 0.14f, 0.03f), band);   // pans du noeud
            Box3(body, V3(-0.04f, 2.1f, -0.33f), V3(0.5f, 0, -0.3f), V3(0.05f, 0.16f, 0.03f), band);
        } break;
        default: break;   // casque : dessine avec le treillis (meme motif)
    }
}

//----------------------------------------------------------------------------------
// Corps
//----------------------------------------------------------------------------------
typedef struct { Vector3 shoulder, elbow, hand; } ArmPose;

static ArmPose PoseArm(const Enemy *e, int k, float walkSwing)
{
    float f = e->fat, s = e->sz;
    ArmPose a;
    a.shoulder = V3(0.5f*k*f, 1.62f, 0);
    if (e->state == ES_DEAD)
    {
        a.elbow = V3((0.57f*f + 0.2f)*k, 1.4f, -0.1f);
        a.hand = V3((0.6f*f + 0.4f)*k, 1.2f, -0.2f);
        return a;
    }
    if (e->state == ES_GRAB)
    {
        if (EnemyHoldsChick(e) && k == 1)
        {
            // Brandit le poussin attrape
            a.elbow = V3(0.6f*f + 0.1f, 1.5f, 0.3f*f + 0.1f);
            a.hand = V3(0.35f*f, 1.78f, 0.52f*f + 0.15f);
        }
        else
        {
            // Fouille le comptoir (dont le dessus est a 1.18 m)
            float reach = sinf(e->grabT*7.0f + k)*0.08f;
            a.elbow = V3((0.5f*f + 0.1f)*k, 1.35f, 0.4f*f + 0.1f);
            a.hand = V3(0.22f*k*f, 1.25f/s + 0.06f, 0.6f*f + 0.35f + reach);
        }
        return a;
    }
    // Bras qui balancent en marchant, coup de poing quand il attaque
    float sway = -walkSwing*k*0.3f;
    a.elbow = V3((0.57f*f + 0.16f)*k, 1.3f, sway*0.5f);
    a.hand = V3((0.5f*f + 0.2f)*k, 1.0f, 0.12f + sway);
    float punch = e->attackAnim*(k == 1 ? 1.0f : 0.4f);
    a.elbow = Vector3Lerp(a.elbow, V3((0.5f*f + 0.05f)*k, 1.5f, 0.3f*f + 0.2f), punch);
    a.hand = Vector3Lerp(a.hand, V3(0.2f*k*f, 1.62f, 0.5f*f + 0.5f), punch);
    return a;
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
    const Camo *camo = (e->type == E_BOSS) ? &CAMO_BOSS : &CAMOS[e->camo % CAMO_COUNT];
    Color cloth = Flash(camo->base, hf), skin = Flash(SKINS[e->skin % SKIN_COUNT], hf);

    bool walking = e->state == ES_WALK;
    float sw = walking ? sinf(e->phase) : 0.0f;
    float bob = walking ? fabsf(sinf(e->phase))*0.05f : 0.0f;
    Matrix body = MatrixMultiply(MatrixTranslate(0, bob, 0), root);
    ArmPose arms[2] = { PoseArm(e, -1, sw), PoseArm(e, 1, sw) };

    // Jambes : hanche, genou, cheville
    Vector3 ankle[2];
    RenderSetCamo(true, body, Flash(camo->a, hf), Flash(camo->b, hf), Flash(camo->c, hf));
    for (int k = -1; k <= 1; k += 2)
    {
        float swing = sw*k*0.28f, lift = fmaxf(0, -swing);
        Vector3 hip = V3(0.22f*k*f, 0.85f, 0);
        Vector3 foot = V3(0.25f*k*f, 0.14f + lift*0.25f, swing);
        Vector3 knee = V3((hip.x + foot.x)*0.5f, 0.48f + lift*0.15f, (hip.z + foot.z)*0.5f + 0.08f);
        ankle[(k + 1)/2] = foot;
        Limb(root, hip, knee, 0.17f*sqrtf(f), cloth);
        Seg(root, knee, Vector3Add(foot, V3(0, 0.2f, 0)), 0.15f*sqrtf(f), cloth);
    }

    // Treillis : bassin, ventre, epaules, manches (et casque)
    Ellipsoid shape[3];
    TorsoShape(f, shape);
    for (int i = 0; i < 3; i++) Ball(body, shape[i].c, shape[i].r, cloth);
    for (int i = 0; i < 2; i++)
    {
        Limb(body, arms[i].shoulder, arms[i].elbow, 0.15f, cloth);
        Seg(body, arms[i].elbow, arms[i].hand, 0.12f, cloth);
    }
    if (e->gear == GEAR_HELMET)
    {
        Ball(body, V3(0, 2.16f, -0.05f), V3(0.33f, 0.2f, 0.33f), cloth);
        Ball(body, V3(0, 2.15f, -0.05f), V3(0.36f, 0.03f, 0.37f), cloth);
    }
    RenderSetCamo(false, body, BLANK, BLANK, BLANK);

    // Rangers
    Color boots = Flash(BOOTS, hf);
    for (int i = 0; i < 2; i++)
    {
        Vector3 a = ankle[i];
        Seg(root, a, Vector3Add(a, V3(0, 0.22f, 0)), 0.155f*sqrtf(f), boots);
        Box3(root, V3(a.x, 0.07f, a.z + 0.07f), NOROT, V3(0.25f, 0.15f, 0.4f), boots);
    }

    // Ceinturon
    Matrix belt = MatrixMultiply(MatrixScale(0.47f*f, 1, 0.41f*f), body);
    Seg(belt, V3(0, 0.8f, 0), V3(0, 0.9f, 0), 1.0f, Flash((Color){ 46, 40, 30, 255 }, hf));
    Box3(body, V3(0, 0.85f, 0.41f*f), NOROT, V3(0.12f, 0.09f, 0.03f), Flash(STEEL, hf));

    // Chaines (et cadenas pour les gros calibres)
    bool heavy = (e->type == E_TANK || e->type == E_BOSS);
    DrawPart(ChainModel(heavy ? CHAIN_HEAVY : CHAIN_LIGHT, f), body, Flash(STEEL, hf));
    if (heavy)
    {
        Vector3 n;
        Vector3 c = SurfaceAlong(shape, V3(0, 1.3f, 0.02f*f), V3(0, 0, 1), &n);   // la ou les chaines se croisent
        Color lockC = Flash(e->type == E_BOSS ? BRASS : STEEL, hf);
        Box3(body, Vector3Add(c, Vector3Scale(n, 0.09f)), NOROT, V3(0.13f, 0.11f, 0.06f), lockC);
        Seg(body, Vector3Add(c, V3(-0.04f, 0.04f, 0.09f)), Vector3Add(c, V3(-0.04f, 0.13f, 0.09f)), 0.014f, Flash(STEEL, hf));
        Seg(body, Vector3Add(c, V3(0.04f, 0.04f, 0.09f)), Vector3Add(c, V3(0.04f, 0.13f, 0.09f)), 0.014f, Flash(STEEL, hf));
    }
    if (e->type == E_BOSS)   // epaulettes dorees, posees sur la pente des epaules
        for (int k = -1; k <= 1; k += 2) Box3(body, V3(0.42f*k*f, 1.82f, 0), V3(0, 0, -0.5f*k), V3(0.26f, 0.04f, 0.22f), Flash(BRASS, hf));

    // Mains (et poussin attrape)
    for (int i = 0; i < 2; i++) Ball(body, arms[i].hand, V3(0.13f, 0.13f, 0.13f), skin);
    if (EnemyHoldsChick(e))
    {
        Vector3 h = arms[1].hand;
        float sc = 1.3f/s;   // meme taille pour tous les mercenaires
        DrawChick(MatrixMultiply(MatrixMultiply(MatrixScale(sc, sc, sc), MatrixTranslate(h.x, h.y + 0.05f, h.z + 0.05f)), body),
                  e->grabT, 1.0f);
    }

    DrawHead(body, e, skin, hf);

    // Fleche de harpon plantee dans le ventre (taille independante du mercenaire)
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
