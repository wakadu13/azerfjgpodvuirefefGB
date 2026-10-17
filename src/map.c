/*
 * map.c - Construction, rendu et collisions de l'arene.
 */
#include "map.h"
#include "render.h"

#include "raymath.h"

#include <math.h>

Box boxes[MAX_BOXES];
int boxCount;

//----------------------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------------------
static void AddBox(float x, float z, float sx, float sy, float sz, BoxStyle style)
{
    if (boxCount >= MAX_BOXES) return;
    boxes[boxCount++] = (Box){ { V3(x - sx/2, 0, z - sz/2), V3(x + sx/2, sy, z + sz/2) }, style };
}

void MapBuild(void)
{
    boxCount = 0;

    // Comptoir du stand des poussins (toujours en premier)
    AddBox(0, 0, 4.4f, 1.1f, 1.6f, BX_COUNTER);

    // Murs exterieurs avec une porte au milieu de chaque cote
    const float gate = 5.0f, th = 1.0f, h = 4.0f;
    float segLen = ARENA - gate/2 + th;
    float segC = gate/2 + segLen/2;
    for (int s = -1; s <= 1; s += 2)
    {
        AddBox(s*segC, -ARENA - th/2, segLen, h, th, BX_WALL);
        AddBox(s*segC, ARENA + th/2, segLen, h, th, BX_WALL);
        AddBox(-ARENA - th/2, s*segC, th, h, segLen, BX_WALL);
        AddBox(ARENA + th/2, s*segC, th, h, segLen, BX_WALL);
    }

    // Couverts : caisses et blocs de beton (placement symetrique)
    AddBox(7, 7, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(8.4f, 6.2f, 1.2f, 2.0f, 1.2f, BX_CRATE);
    AddBox(-7, -7, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(-8.5f, -6.3f, 1.2f, 2.0f, 1.2f, BX_CRATE);
    AddBox(-7.5f, 7.5f, 3.0f, 1.0f, 0.7f, BX_CONCRETE);
    AddBox(7.5f, -7.5f, 3.0f, 1.0f, 0.7f, BX_CONCRETE);
    AddBox(15, 4, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(-15, -4, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(4, -15, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(-4, 15, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(14, -13, 0.7f, 1.0f, 3.5f, BX_CONCRETE);
    AddBox(-14, 13, 0.7f, 1.0f, 3.5f, BX_CONCRETE);
    AddBox(-13, -15, 3.5f, 1.0f, 0.7f, BX_CONCRETE);
    AddBox(13, 15, 3.5f, 1.0f, 0.7f, BX_CONCRETE);
    AddBox(21, 21, 2.2f, 2.2f, 2.2f, BX_CRATE);
    AddBox(-21, -21, 2.2f, 2.2f, 2.2f, BX_CRATE);
    AddBox(-21, 21, 2.2f, 2.2f, 2.2f, BX_CRATE);
    AddBox(21, -21, 2.2f, 2.2f, 2.2f, BX_CRATE);
    AddBox(23, 6, 1.4f, 1.0f, 1.4f, BX_CRATE);
    AddBox(-23, -6, 1.4f, 1.0f, 1.4f, BX_CRATE);
}

//----------------------------------------------------------------------------------
// Collisions
//----------------------------------------------------------------------------------
void ResolveCircle(Vector3 *p, float radius, float feetY, float height)
{
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        if (feetY >= b.max.y - 0.05f || feetY + height <= b.min.y) continue;
        float cx = Clamp(p->x, b.min.x, b.max.x);
        float cz = Clamp(p->z, b.min.z, b.max.z);
        float dx = p->x - cx, dz = p->z - cz;
        float d2 = dx*dx + dz*dz;
        if (d2 >= radius*radius) continue;
        if (d2 > 1e-8f)
        {
            float d = sqrtf(d2);
            p->x += dx/d*(radius - d);
            p->z += dz/d*(radius - d);
        }
        else
        {
            // Centre a l'interieur : sortie par la face la plus proche
            float l = p->x - b.min.x, r = b.max.x - p->x, n = p->z - b.min.z, f = b.max.z - p->z;
            float m = fminf(fminf(l, r), fminf(n, f));
            if (m == l) p->x = b.min.x - radius;
            else if (m == r) p->x = b.max.x + radius;
            else if (m == n) p->z = b.min.z - radius;
            else p->z = b.max.z + radius;
        }
    }
}

float GroundHeightAt(Vector3 p, float radius, float feetY)
{
    float g = 0.0f;
    float r = radius*0.7f;
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        if (p.x + r < b.min.x || p.x - r > b.max.x || p.z + r < b.min.z || p.z - r > b.max.z) continue;
        if (feetY >= b.max.y - 0.3f && b.max.y > g) g = b.max.y;
    }
    return g;
}

Vector3 CounterPoint(Vector3 p)
{
    BoundingBox b = boxes[0].b;
    return V3(Clamp(p.x, b.min.x, b.max.x), 0, Clamp(p.z, b.min.z, b.max.z));
}

bool MapRaycast(Ray ray, float *dist, Vector3 *normal)
{
    bool hit = false;
    if (ray.direction.y < -1e-4f)
    {
        float t = -ray.position.y/ray.direction.y;
        if (t < *dist) { *dist = t; *normal = V3(0, 1, 0); hit = true; }
    }
    for (int i = 0; i < boxCount; i++)
    {
        RayCollision c = GetRayCollisionBox(ray, boxes[i].b);
        if (c.hit && c.distance < *dist) { *dist = c.distance; *normal = c.normal; hit = true; }
    }
    return hit;
}

//----------------------------------------------------------------------------------
// Rendu
//----------------------------------------------------------------------------------
static void DrawCrate(Vector3 c, Vector3 s)
{
    // Boite sombre + trois boites claires legerement en retrait = cadre sur chaque face
    const Color dark = { 110, 72, 36, 255 }, light = { 168, 118, 64, 255 };
    const float b = 0.09f;
    Matrix I = MatrixIdentity();
    Box3(I, c, NOROT, s, dark);
    Box3(I, c, NOROT, V3(s.x + 0.004f, s.y - 2*b, s.z - 2*b), light);
    Box3(I, c, NOROT, V3(s.x - 2*b, s.y + 0.004f, s.z - 2*b), light);
    Box3(I, c, NOROT, V3(s.x - 2*b, s.y - 2*b, s.z + 0.004f), light);
}

static void DrawStand(Vector3 c, Vector3 s, float time)
{
    Matrix I = MatrixIdentity();
    const Color red = { 200, 35, 35, 255 }, white = { 245, 245, 240, 255 };

    // Comptoir raye rouge et blanc
    Box3(I, c, NOROT, s, red);
    for (int i = 0; i < 5; i++)
        Box3(I, V3(-1.76f + i*0.88f, 0.55f, c.z), NOROT, V3(0.44f, 1.104f, s.z + 0.004f), white);
    Box3(I, V3(c.x, s.y + 0.04f, c.z), NOROT, V3(s.x + 0.2f, 0.08f, s.z + 0.2f), (Color){ 220, 220, 225, 255 });

    // Poteaux et toit
    for (int k = 0; k < 4; k++)
    {
        float px = (k & 1) ? 2.1f : -2.1f, pz = (k & 2) ? 0.7f : -0.7f;
        Seg(I, V3(px, 1.1f, pz), V3(px, 3.0f, pz), 0.05f, (Color){ 200, 200, 205, 255 });
    }
    for (int i = 0; i < 6; i++)
        Box3(I, V3(-2.3f + i*0.92f + 0.46f, 3.05f, 0), NOROT, V3(0.92f, 0.12f, 2.2f), (i % 2) ? white : red);

    // Poussin geant tournant en enseigne
    Seg(I, V3(0, 3.1f, 0), V3(0, 4.2f, 0), 0.08f, (Color){ 120, 120, 125, 255 });
    DrawChick(MatrixMultiply(MatrixMultiply(MatrixScale(7, 7, 7), MatrixRotateY(time*0.8f)), MatrixTranslate(0, 4.1f, 0)), time, 0.25f);
}

static void DrawBox(const Box *bx, float time)
{
    Vector3 c = Vector3Scale(Vector3Add(bx->b.min, bx->b.max), 0.5f);
    Vector3 s = Vector3Subtract(bx->b.max, bx->b.min);
    Matrix I = MatrixIdentity();
    switch (bx->style)
    {
        case BX_CRATE:
            DrawCrate(c, s);
            break;
        case BX_CONCRETE:
            Box3(I, c, NOROT, s, (Color){ 165, 165, 160, 255 });
            Box3(I, V3(c.x, s.y - 0.05f, c.z), NOROT, V3(s.x + 0.06f, 0.1f, s.z + 0.06f), (Color){ 185, 185, 180, 255 });
            Box3(I, V3(c.x, 0.15f, c.z), NOROT, V3(s.x + 0.004f, 0.06f, s.z + 0.004f), (Color){ 230, 190, 30, 255 });
            break;
        case BX_WALL:
            Box3(I, c, NOROT, s, (Color){ 150, 142, 132, 255 });
            Box3(I, V3(c.x, s.y + 0.1f, c.z), NOROT, V3(s.x + 0.2f, 0.2f, s.z + 0.2f), (Color){ 120, 112, 104, 255 });
            Box3(I, V3(c.x, 0.4f, c.z), NOROT, V3(s.x + 0.02f, 0.8f, s.z + 0.02f), (Color){ 110, 105, 100, 255 });
            break;
        case BX_COUNTER:
            DrawStand(c, s, time);
            break;
    }
}

// Les poussins sautillent sur le comptoir, chacun a son rythme
static void DrawStandChicks(int chicks, float time)
{
    for (int i = 0; i < chicks; i++)
    {
        int row = i/6, col = i % 6;
        float t = time + i*1.37f;
        float x = -1.75f + col*0.7f + 0.06f*sinf(t*0.9f);
        float z = (row == 0) ? 0.35f : -0.35f;
        float hop = fmaxf(0.0f, sinf(t*5.0f + i))*0.05f*(sinf(t*0.7f) > 0.3f);
        float yaw = (row == 0 ? 0.0f : PI) + 0.8f*sinf(t*0.45f + i*2.1f);
        DrawChick(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(x, 1.18f + hop, z)), t, hop > 0.01f ? 0.4f : 0.0f);
    }
}

void MapDraw(float time, int chicks)
{
    Matrix I = MatrixIdentity();

    // Sol : un grand cube dont le shader dessine carrelage et herbe
    RenderSetChecker(true);
    Box3(I, V3(0, -0.5f, 0), NOROT, V3(240, 1, 240), (Color){ 140, 138, 134, 255 });
    RenderSetChecker(false);

    for (int i = 0; i < boxCount; i++) DrawBox(&boxes[i], time);
    DrawStandChicks(chicks, time);

    // Lampadaires decoratifs
    for (int k = 0; k < 4; k++)
    {
        float x = (k & 1) ? 26.0f : -26.0f, z = (k & 2) ? 26.0f : -26.0f;
        Seg(I, V3(x, 0, z), V3(x, 6, z), 0.12f, (Color){ 60, 62, 66, 255 });
        Box3(I, V3(x, 6.05f, z), NOROT, V3(0.7f, 0.2f, 0.4f), (Color){ 60, 62, 66, 255 });
    }
}
