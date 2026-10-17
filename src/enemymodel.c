/*
 * enemymodel.c - Modele 3D des mercenaires.
 *
 * Silhouette seche en treillis olive (manches retroussees, brassard orange
 * tache de rouge, epaulettes rouges, beret glisse sous l'epaulette ou
 * foulard), visage peint en vert aux yeux cernes de noir avec des
 * coulures, cheveux rases sur les cotes, chaine en metal autour du cou
 * (et du torse pour les gros calibres). Demarche de zombie, bras tendus.
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

typedef struct { Color base, a, b, c; } Cloth;   // tissu + 3 nuances (usure, boue, delave)

static const Cloth UNIFORMS[CAMO_COUNT] = {
    { { 88, 96, 68, 255 }, { 78, 86, 60, 255 }, { 62, 66, 48, 255 }, { 102, 108, 80, 255 } },   // olive
    { { 80, 86, 68, 255 }, { 70, 76, 60, 255 }, { 56, 58, 48, 255 }, { 94, 98, 80, 255 } },     // olive gris
    { { 92, 92, 66, 255 }, { 80, 80, 58, 255 }, { 64, 60, 44, 255 }, { 106, 104, 78, 255 } },   // olive brun
    { { 70, 78, 56, 255 }, { 62, 70, 50, 255 }, { 48, 52, 38, 255 }, { 84, 90, 66, 255 } },     // olive fonce
};
static const Cloth UNIFORM_BOSS = { { 58, 62, 50, 255 }, { 48, 52, 42, 255 }, { 36, 36, 30, 255 }, { 72, 76, 60, 255 } };
static const Color SKINS[SKIN_COUNT] = { { 238, 196, 170, 255 }, { 222, 172, 140, 255 }, { 182, 128, 96, 255 }, { 120, 84, 60, 255 } };
static const Color HAIRS[4] = { { 86, 62, 42, 255 }, { 50, 38, 28, 255 }, { 132, 102, 70, 255 }, { 30, 26, 22, 255 } };
static const Color PAINTS[PAINT_COUNT] = { { 112, 126, 80, 255 }, { 96, 112, 70, 255 }, { 120, 128, 94, 255 } };   // vert du visage

static const Color PAINT_BLACK = { 24, 26, 22, 255 };
static const Color PAINT_DARK = { 54, 70, 40, 255 };     // vert fonce
static const Color EYE_WHITE = { 238, 212, 206, 255 };   // yeux injectes
static const Color ARMBAND = { 238, 112, 38, 255 };
static const Color BLOOD = { 168, 24, 20, 255 };
static const Color EPAULET_RED = { 190, 36, 30, 255 };
static const Color BERET_RED = { 112, 24, 32, 255 };
static const Color STEEL = { 158, 162, 168, 255 };
static const Color BOOTS = { 28, 26, 24, 255 };
static const Color BRASS = { 230, 180, 50, 255 };

// Volumes du torse (repere du corps, avant = +z) : taille, poitrine,
// epaules. Les chaines s'y posent, la carrure f elargit x et z.
typedef struct { Vector3 c, r; } Ellipsoid;

static void TorsoShape(float f, Ellipsoid out[3])
{
    out[0] = (Ellipsoid){ V3(0, 1.1f, 0), V3(0.19f*f, 0.17f, 0.13f*f) };
    out[1] = (Ellipsoid){ V3(0, 1.33f, 0.012f*f), V3(0.215f*f, 0.2f, 0.14f*f) };
    out[2] = (Ellipsoid){ V3(0, 1.48f, 0), V3(0.25f*f, 0.09f, 0.13f*f) };
}

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

//----------------------------------------------------------------------------------
// Chaines
//----------------------------------------------------------------------------------
#define LINK_SEG     10    // segments autour d'un maillon
#define LINK_SIDES   6     // segments autour du fil
#define LINK_VERTS   (LINK_SEG*LINK_SIDES)
#define CHAIN_STEPS  20    // carrures en cache : 1.0 a 1.95 par pas de 0.05
#define PATH_SAMPLES 200

typedef enum { CHAIN_LIGHT, CHAIN_HEAVY, CHAIN_KINDS } ChainKind;   // autour du cou, + en bandouliere (colosse, chef)

static Model chainModels[CHAIN_KINDS][CHAIN_STEPS];
static bool chainReady[CHAIN_KINDS][CHAIN_STEPS];

// Une boucle de chaine. Collier : fait le tour du cou et pend sur la
// poitrine. Bandouliere : cercle incline autour du torse (directions u, v).
typedef struct { bool necklace; Vector3 c, u, v; } ChainLoop;

static Vector3 LoopPoint(const Ellipsoid *shape, const ChainLoop *l, float a, Vector3 *normal)
{
    if (l->necklace)
    {
        float front = fmaxf(0.0f, cosf(a));
        Vector3 from = V3(0, 1.555f - 0.4f*front*front*front, 0);
        return SurfaceAlong(shape, from, V3(sinf(a), 0, cosf(a)), normal);
    }
    Vector3 d = Vector3Add(Vector3Scale(l->u, cosf(a)), Vector3Scale(l->v, sinf(a)));
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
    float a = 0.035f*scale, b = 0.021f*scale, r = 0.0085f*scale, pitch = 2*(a - r);
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
        Vector3 center = Vector3Add(p, Vector3Scale(nm, (standing ? b + r : r) + 0.003f));
        AddLink(m, center, t, w, a, b, r);
    }
}

static Model *ChainModel(ChainKind kind, float f)
{
    int step = (int)Clamp(roundf((f - 1.0f)/0.05f), 0, CHAIN_STEPS - 1);
    Model *model = &chainModels[kind][step];
    if (chainReady[kind][step]) return model;

    float fq = 1.0f + step*0.05f;
    Ellipsoid shape[3];
    TorsoShape(fq, shape);
    ChainLoop neck = { true, { 0 }, { 0 }, { 0 } };
    ChainLoop bandolier = { false, V3(0, 1.3f, 0.01f*fq), V3(cosf(0.85f), sinf(0.85f), 0), V3(0, 0, 1) };

    // Deux passes : compter, puis remplir
    Mesh mesh = { 0 };
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            int verts = mesh.vertexCount, tris = mesh.triangleCount;
            mesh = (Mesh){ 0 };
            mesh.vertices = (float *)MemAlloc(verts*3*sizeof(float));
            mesh.normals = (float *)MemAlloc(verts*3*sizeof(float));
            mesh.indices = (unsigned short *)MemAlloc(tris*3*sizeof(unsigned short));
        }
        AddLoop(&mesh, shape, &neck, 1.0f, pass == 1);
        if (kind == CHAIN_HEAVY) AddLoop(&mesh, shape, &bandolier, 1.3f, pass == 1);
    }

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
// Tete : visage peint
//----------------------------------------------------------------------------------
static const Vector3 HEAD_C = { 0, 1.775f, 0.012f };
static const Vector3 HEAD_R = { 0.105f, 0.13f, 0.115f };

// Point du visage dans la direction (lacet, tangage) et sa normale
static Vector3 OnHead(float yaw, float pitch, Vector3 *normal)
{
    Vector3 d = V3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
    float t = 1.0f/sqrtf(powf(d.x/HEAD_R.x, 2) + powf(d.y/HEAD_R.y, 2) + powf(d.z/HEAD_R.z, 2));
    Vector3 p = Vector3Scale(d, t);
    *normal = Vector3Normalize(V3(p.x/(HEAD_R.x*HEAD_R.x), p.y/(HEAD_R.y*HEAD_R.y), p.z/(HEAD_R.z*HEAD_R.z)));
    return Vector3Add(HEAD_C, p);
}

// Trait de peinture pose sur le visage ; angle = inclinaison (PI/2 = vertical)
static void Stroke(Matrix body, float yaw, float pitch, float angle, float len, float wid, Color c)
{
    Vector3 n;
    Vector3 p = OnHead(yaw, pitch, &n);
    Vector3 east = Vector3Normalize(Vector3CrossProduct(V3(0, 1, 0), n));
    Vector3 north = Vector3CrossProduct(n, east);
    Vector3 x = Vector3Add(Vector3Scale(east, cosf(angle)), Vector3Scale(north, sinf(angle)));
    BallAxes(body, Vector3Subtract(p, Vector3Scale(n, 0.004f)), x, Vector3CrossProduct(n, x), V3(len, wid, 0.011f), c);
}

static void DrawFacePaint(Matrix body, int style, float hf)
{
    Color black = Flash(PAINT_BLACK, hf), dark = Flash(PAINT_DARK, hf);
    for (int k = -1; k <= 1; k += 2)
    {
        Stroke(body, 0.36f*k, 0.09f, 0.1f*k, 0.05f, 0.036f, black);   // yeux cernes de noir
        Stroke(body, 0.36f*k, 0.3f, 0.25f*k, 0.045f, 0.014f, black);  // sourcils
    }
    switch (style)
    {
        case 0:   // coulures noires sous les yeux et sur le front
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.35f*k, -0.2f, PI/2 + 0.08f*k, 0.06f, 0.014f, black);
                Stroke(body, 0.33f*k, -0.5f, PI/2 + 0.12f*k, 0.06f, 0.011f, black);
            }
            Stroke(body, 0.12f, 0.52f, PI/2 - 0.15f, 0.06f, 0.009f, black);
            Stroke(body, -0.14f, 0.48f, PI/2 + 0.2f, 0.05f, 0.008f, black);
            break;
        case 1:   // taches vert fonce et noires
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.55f*k, -0.25f, 0.6f*k, 0.05f, 0.028f, dark);
                Stroke(body, 0.5f*k, -0.45f, PI/2 - 0.2f*k, 0.05f, 0.016f, black);
            }
            Stroke(body, 0.1f, 0.45f, 0.2f, 0.07f, 0.024f, dark);
            break;
        default:  // gros cernes, arete du nez noire, joues fonces
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.38f*k, 0.06f, 0.2f*k, 0.06f, 0.045f, black);
                Stroke(body, 0.55f*k, -0.2f, PI/2, 0.06f, 0.022f, dark);
            }
            Stroke(body, 0, 0.12f, PI/2, 0.05f, 0.014f, black);
            break;
    }
}

static void DrawHead(Matrix body, const Enemy *e, Color skin, float hf)
{
    float shout = 0.0f;   // bouche : grande ouverte quand il frappe
    if (e->attackAnim > 0) shout = 1.0f;
    else if (e->state == ES_GRAB) shout = 0.4f + 0.4f*sinf(e->grabT*9.0f);

    Color paint = Flash(PAINTS[e->paint % PAINT_COUNT], hf);
    Color hair = Flash(HAIRS[(e->skin + e->camo) % 4], hf);

    Seg(body, V3(0, 1.5f, -0.005f), V3(0, 1.68f, 0.01f), 0.052f, paint);       // cou (peint aussi)
    Ball(body, V3(0, 1.69f, 0.025f), V3(0.085f, 0.06f, 0.085f), paint);        // machoire
    Ball(body, HEAD_C, HEAD_R, paint);
    for (int k = -1; k <= 1; k += 2) Ball(body, V3(0.103f*k, 1.765f, -0.005f), V3(0.018f, 0.04f, 0.028f), skin);   // oreilles

    // Cotes et nuque rases, un peu plus long sur le dessus
    Ball(body, V3(0, 1.79f, -0.02f), V3(0.108f, 0.118f, 0.1f), Shade(hair, 0.75f));
    if (e->type == E_BOSS)
    {
        // Le chef porte son beret
        Color beret = Flash(BERET_RED, hf);
        Seg(body, V3(0, 1.85f, 0.005f), V3(0, 1.87f, 0.005f), 0.105f, Shade(beret, 0.6f));
        Ball(MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.3f), MatrixTranslate(-0.025f, 1.9f, 0.0f)), body),
             V3(0, 0, 0), V3(0.12f, 0.04f, 0.12f), beret);
        Ball(body, V3(0.06f, 1.885f, 0.1f), V3(0.016f, 0.018f, 0.006f), Flash(BRASS, hf));   // insigne
    }
    else Ball(body, V3(0, 1.86f, 0.008f), V3(0.092f, 0.06f, 0.1f), hair);

    DrawFacePaint(body, e->paint % PAINT_COUNT, hf);

    Vector3 n;
    Vector3 nose = OnHead(0, -0.08f, &n);
    Ball(body, Vector3Add(nose, Vector3Scale(n, 0.012f)), V3(0.018f, 0.03f, 0.022f), Shade(paint, 1.05f));
    for (int k = -1; k <= 1; k += 2)
    {
        // Yeux grands ouverts, injectes de sang
        Vector3 eye = OnHead(0.36f*k, 0.09f, &n);
        Ball(body, Vector3Subtract(eye, Vector3Scale(n, 0.002f)), V3(0.024f, 0.017f, 0.012f), Flash(EYE_WHITE, hf));
        Ball(body, Vector3Add(eye, Vector3Scale(n, 0.009f)), V3(0.009f, 0.01f, 0.005f), (Color){ 40, 28, 20, 255 });
    }
    Color lips = shout > 0.6f ? (Color){ 60, 18, 18, 255 } : Flash((Color){ 150, 96, 90, 255 }, hf);
    Stroke(body, 0, -0.42f, 0, 0.035f, 0.007f + 0.02f*shout, lips);
}

//----------------------------------------------------------------------------------
// Corps
//----------------------------------------------------------------------------------
typedef struct { Vector3 shoulder, elbow, hand; } ArmPose;

// Le mercenaire se penche sur le comptoir pour attraper un poussin
static float GrabLean(const Enemy *e)
{
    if (e->state != ES_GRAB) return e->type == E_FAST ? 0.12f : 0.06f;   // demarche penchee
    float lean = Clamp(0.35f + (e->sz - 1.0f)*0.4f, 0.25f, 0.9f);
    return lean*(1.0f - Clamp((e->grabT - (GRAB_HOLD - 0.3f))/0.3f, 0.0f, 1.0f));   // se redresse avec le poussin
}

static ArmPose PoseArm(const Enemy *e, int k, float walkSwing, float lean)
{
    float f = e->fat, s = e->sz;
    ArmPose a;
    a.shoulder = V3(0.235f*k*f, 1.47f, 0);
    if (e->state == ES_DEAD)
    {
        a.elbow = V3((0.25f*f + 0.2f)*k, 1.42f, -0.08f);
        a.hand = V3((0.25f*f + 0.42f)*k, 1.35f, -0.12f);
        return a;
    }
    if (e->state == ES_GRAB)
    {
        if (EnemyHoldsChick(e) && k == 1)
        {
            // Brandit le poussin attrape
            a.elbow = V3(0.3f*f, 1.55f, 0.2f);
            a.hand = V3(0.16f*f, 1.78f, 0.32f);
        }
        else
        {
            // Fouille le comptoir (dessus a 1.18 m) : on vise cette hauteur une fois penche
            float tz = 0.5f + sinf(e->grabT*7.0f + k)*0.04f;
            float ty = 1.0f + (1.2f/s - 1.0f + tz*sinf(lean))/cosf(lean);
            a.elbow = V3(0.27f*k*f, (1.47f + ty)*0.5f, 0.22f);
            a.hand = V3(0.12f*k*f, ty, tz);
        }
        return a;
    }
    float sway = walkSwing*k;
    if (e->type == E_FAST)
    {
        // Court : bras plies qui balancent
        a.elbow = V3((0.25f*f + 0.04f)*k, 1.22f, -sway*0.12f);
        a.hand = V3((0.22f*f + 0.02f)*k, 1.18f, 0.16f - sway*0.3f);
    }
    else
    {
        // Avance comme un zombie, bras tendus vers l'avant
        a.elbow = V3((0.24f*f + 0.02f)*k, 1.42f + sway*0.02f, 0.27f + sway*0.04f);
        a.hand = V3(0.21f*k*f, 1.44f + sway*0.05f, 0.54f + sway*0.06f);
    }
    // Il se jette en avant, mains en griffes, quand il frappe
    float lunge = e->attackAnim*(k == 1 ? 1.0f : 0.8f);
    a.elbow = Vector3Lerp(a.elbow, V3(0.2f*k*f, 1.5f, 0.3f), lunge);
    a.hand = Vector3Lerp(a.hand, V3(0.1f*k*f, 1.55f, 0.62f), lunge);
    return a;
}

// Ecusson plat colle au torse (tissu, nom, insignes)
static void Patch(Matrix body, const Ellipsoid *shape, float x, float y, Vector3 size, Color c)
{
    Vector3 n;
    Vector3 p = SurfaceAlong(shape, V3(x, y, 0), V3(0, 0, 1), &n);
    Box3(body, Vector3Add(p, Vector3Scale(n, 0.002f)), V3(-atan2f(n.y, n.z), 0, 0), size, c);
}

static void DrawEnemy(const Enemy *e)
{
    float s = e->sz, f = e->fat, sf = sqrtf(f);

    // Mort : bascule en arriere puis s'enfonce dans le sol
    float tilt = 0, sink = 0;
    if (e->state == ES_DEAD)
    {
        float k = fminf(e->deadT/0.7f, 1.0f);
        tilt = (1.0f - (1.0f - k)*(1.0f - k))*PI*0.5f;
        if (e->deadT > 1.8f) sink = (e->deadT - 1.8f)*0.6f*s;
    }
    Matrix root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateX(-tilt)),
                                 MatrixRotateY(e->yaw)), MatrixTranslate(e->pos.x, -sink, e->pos.z));
    if (e->launched)
    {
        // Projete : tourne autour de son ventre en vol, puis couche au sol
        float centerH = e->airborne ? 1.0f*s : 0.16f*s*f;
        sink = (!e->airborne && e->deadT > 1.8f) ? (e->deadT - 1.8f)*0.6f*s : 0.0f;
        root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(0, -1.0f*s, 0)),
                              MatrixRotateX(-e->spin)), MatrixRotateY(e->yaw)),
                              MatrixTranslate(e->pos.x, e->pos.y + centerH - sink, e->pos.z));
    }

    float hf = e->hitFlash;   // flash blanc quand il est touche
    const Cloth *cl = (e->type == E_BOSS) ? &UNIFORM_BOSS : &UNIFORMS[e->camo % CAMO_COUNT];
    Color shirt = Flash(cl->base, hf), pants = Flash(Shade(cl->base, 0.8f), hf);
    Color skin = Flash(SKINS[e->skin % SKIN_COUNT], hf);

    bool walking = e->state == ES_WALK;
    float sw = walking ? sinf(e->phase) : 0.0f;
    float bob = walking ? fabsf(sinf(e->phase))*0.035f : 0.0f;
    float lean = (e->state == ES_DEAD) ? 0.0f : GrabLean(e);
    Matrix leanM = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateX(lean)), MatrixTranslate(0, 1.0f + bob, 0));
    Matrix body = MatrixMultiply(leanM, root);
    ArmPose arms[2] = { PoseArm(e, -1, sw, lean), PoseArm(e, 1, sw, lean) };

    // Pantalon de treillis (taches d'usure et de boue calculees par le shader)
    Vector3 ankle[2];
    RenderSetCamo(true, body, Flash(Shade(cl->a, 0.8f), hf), Flash(Shade(cl->b, 0.8f), hf), Flash(Shade(cl->c, 0.8f), hf));
    for (int k = -1; k <= 1; k += 2)
    {
        float swing = sw*k*0.24f, lift = fmaxf(0, -swing);
        Vector3 hip = V3(0.1f*k*f, 0.95f, 0);
        Vector3 foot = V3(0.12f*k*f, 0.1f + lift*0.18f, swing);
        Vector3 knee = V3((hip.x + foot.x)*0.5f, 0.52f + lift*0.1f, (hip.z + foot.z)*0.5f + 0.05f);
        ankle[(k + 1)/2] = foot;
        Limb(root, hip, knee, 0.085f*sf, pants);
        Seg(root, knee, Vector3Add(foot, V3(0, 0.12f, 0)), 0.075f*sf, pants);
        Vector3 pocket = Vector3Add(Vector3Lerp(hip, knee, 0.45f), V3(0.08f*k*sf, 0, 0.01f));   // poche cargo
        Box3(root, pocket, NOROT, V3(0.03f, 0.13f, 0.11f), Shade(pants, 0.88f));
    }
    Ball(body, V3(0, 0.98f, 0), V3(0.185f*f, 0.12f, 0.125f*f), pants);

    // Veste de treillis : torse et manches retroussees au-dessus du coude
    Ellipsoid shape[3];
    TorsoShape(f, shape);
    RenderSetCamo(true, body, Flash(cl->a, hf), Flash(cl->b, hf), Flash(cl->c, hf));
    for (int i = 0; i < 3; i++) Ball(body, shape[i].c, shape[i].r, shirt);
    for (int i = 0; i < 2; i++)
    {
        Vector3 cuff = Vector3Lerp(arms[i].shoulder, arms[i].elbow, 0.82f);
        Limb(body, arms[i].shoulder, cuff, 0.062f, shirt);
        Seg(body, cuff, Vector3Lerp(arms[i].shoulder, arms[i].elbow, 0.95f), 0.07f, Shade(shirt, 0.9f));   // revers roule
    }
    RenderSetCamo(false, body, BLANK, BLANK, BLANK);

    // Col : t-shirt noir dessous, pointes du col
    Seg(body, V3(0, 1.53f, 0.0f), V3(0, 1.575f, 0.005f), 0.062f, Flash((Color){ 28, 28, 30, 255 }, hf));
    for (int k = -1; k <= 1; k += 2)
        Box3(body, V3(0.045f*k, 1.56f, 0.05f), V3(0.3f, 0, 0.6f*k), V3(0.06f, 0.01f, 0.045f), shirt);

    // Poches de poitrine, bande nominative, ecusson orange, insigne rond
    for (int k = -1; k <= 1; k += 2) Patch(body, shape, 0.1f*k*f, 1.4f, V3(0.09f, 0.035f, 0.012f), Shade(shirt, 0.85f));
    Patch(body, shape, -0.1f*f, 1.452f, V3(0.09f, 0.022f, 0.01f), Flash((Color){ 52, 56, 42, 255 }, hf));
    Patch(body, shape, 0.1f*f, 1.355f, V3(0.04f, 0.03f, 0.01f), Flash(ARMBAND, hf));
    {
        Vector3 n;
        Vector3 p = SurfaceAlong(shape, V3(0.16f*f, 1.3f, 0), V3(0, 0, 1), &n);
        Ball(body, Vector3Add(p, Vector3Scale(n, 0.004f)), V3(0.024f, 0.024f, 0.008f), Flash((Color){ 205, 195, 155, 255 }, hf));
    }

    // Epaulettes rouges
    Color red = Flash(EPAULET_RED, hf);
    for (int k = -1; k <= 1; k += 2) Box3(body, V3(0.15f*k*f, 1.562f, 0), V3(0, 0, -0.27f*k), V3(0.1f, 0.02f, 0.07f), red);
    if (e->gear == GEAR_BERET)   // beret plie, glisse sous l'epaulette gauche
    {
        Color beret = Flash(BERET_RED, hf);
        Ball(MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.5f), MatrixTranslate(0.19f*f, 1.57f, 0.02f)), body),
             V3(0, 0, 0), V3(0.06f, 0.03f, 0.075f), beret);
        Box3(body, V3(0.215f*f, 1.53f, 0.03f), V3(0, 0, -0.9f), V3(0.012f, 0.06f, 0.06f), Shade(beret, 0.5f));
    }
    if (e->gear == GEAR_SCARF)   // foulard rouge et noir autour du cou
    {
        Seg(body, V3(0, 1.54f, 0.0f), V3(0, 1.6f, 0.005f), 0.075f, red);
        Seg(body, V3(0, 1.563f, 0.0f), V3(0, 1.573f, 0.003f), 0.077f, Flash((Color){ 26, 22, 22, 255 }, hf));
        Ball(body, V3(0.03f, 1.53f, 0.075f), V3(0.035f, 0.03f, 0.025f), red);
        Box3(body, V3(0.045f, 1.47f, 0.13f), V3(-0.25f, 0, 0.3f), V3(0.035f, 0.1f, 0.01f), red);
    }

    // Ceinturon
    Matrix belt = MatrixMultiply(MatrixScale(0.19f*f, 1, 0.132f*f), body);
    Seg(belt, V3(0, 1.02f, 0), V3(0, 1.06f, 0), 1.0f, Flash((Color){ 24, 24, 24, 255 }, hf));
    Box3(body, V3(0, 1.04f, 0.132f*f), NOROT, V3(0.05f, 0.035f, 0.012f), Flash(STEEL, hf));

    // Rangers
    Color boots = Flash(BOOTS, hf);
    for (int i = 0; i < 2; i++)
    {
        Vector3 a = ankle[i];
        Seg(root, a, Vector3Add(a, V3(0, 0.14f, 0)), 0.072f*sf, boots);
        Box3(root, V3(a.x, 0.05f, a.z + 0.04f), NOROT, V3(0.11f*sf, 0.1f, 0.26f), boots);
    }

    // Avant-bras nus, mains, montre au poignet gauche, brassard orange tache de sang
    for (int i = 0; i < 2; i++)
    {
        const ArmPose *a = &arms[i];
        Seg(body, Vector3Lerp(a->shoulder, a->elbow, 0.9f), a->elbow, 0.046f, skin);
        Ball(body, a->elbow, V3(0.046f, 0.046f, 0.046f), skin);
        Seg(body, a->elbow, a->hand, 0.042f, skin);
        Ball(body, a->hand, V3(0.045f, 0.055f, 0.04f), skin);
    }
    {
        const ArmPose *a = &arms[1];
        Seg(body, Vector3Lerp(a->elbow, a->hand, 0.76f), Vector3Lerp(a->elbow, a->hand, 0.84f), 0.05f, Flash((Color){ 20, 20, 22, 255 }, hf));
        Vector3 b0 = Vector3Lerp(a->shoulder, a->elbow, 0.3f), b1 = Vector3Lerp(a->shoulder, a->elbow, 0.62f);
        Seg(body, b0, b1, 0.071f, Flash(ARMBAND, hf));
        Vector3 out = V3(0.066f, 0, 0);
        Color blood = Flash(BLOOD, hf);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.25f), V3(0.064f, 0.01f, 0.02f)), V3(0.012f, 0.022f, 0.014f), blood);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.7f), V3(0.064f, -0.01f, -0.015f)), V3(0.012f, 0.016f, 0.02f), blood);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.5f), out), V3(0.01f, 0.018f, 0.014f), Flash((Color){ 60, 30, 20, 255 }, hf));   // insigne
    }

    // Chaine autour du cou (et en bandouliere pour le colosse et le chef), cadenas
    bool heavy = (e->type == E_TANK || e->type == E_BOSS);
    DrawPart(ChainModel(heavy ? CHAIN_HEAVY : CHAIN_LIGHT, f), body, Flash(STEEL, hf));
    if (heavy)
    {
        Vector3 n;
        Vector3 c = SurfaceAlong(shape, V3(0, 1.17f, 0), V3(0, 0, 1), &n);   // bas du collier
        Color lockC = Flash(e->type == E_BOSS ? BRASS : STEEL, hf);
        Box3(body, Vector3Add(c, Vector3Scale(n, 0.03f)), NOROT, V3(0.05f, 0.045f, 0.025f), lockC);
        Seg(body, Vector3Add(c, V3(0, 0.02f, 0.03f)), Vector3Add(c, V3(0, 0.055f, 0.03f)), 0.012f, Flash(STEEL, hf));
    }

    if (EnemyHoldsChick(e))
    {
        Vector3 h = arms[1].hand;
        float sc = 1.1f/s;   // meme taille pour tous les mercenaires
        DrawChick(MatrixMultiply(MatrixMultiply(MatrixScale(sc, sc, sc), MatrixTranslate(h.x, h.y + 0.03f, h.z + 0.02f)), body),
                  e->grabT, 1.0f);
    }

    DrawHead(body, e, skin, hf);

    // Fleche de harpon plantee dans le ventre (taille independante du mercenaire)
    if (e->launched)
        DrawSpear(MatrixMultiply(MatrixMultiply(MatrixScale(1.0f/s, 1.0f/s, 1.0f/s), MatrixTranslate(0, 1.2f, -0.28f*f)), body));
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
