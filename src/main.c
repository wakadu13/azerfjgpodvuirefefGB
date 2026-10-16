/*
 * BURGER RUSH : Operation HK416
 * FPS en C avec raylib : un operateur arme d'un HK416 defend son stand de
 * burgers contre des vagues de gloutons affames.
 *
 * Tout est procedural (modeles, sons) : aucun fichier externe n'est requis.
 */

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// glClear vient directement de la lib OpenGL (deja liee par raylib) :
// on s'en sert pour vider le depth buffer avant de dessiner l'arme,
// afin qu'elle ne rentre jamais dans les murs.
#if defined(_WIN32)
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif
extern void GL_APIENTRY glClear(unsigned int mask);
#define GL_DEPTH_BUFFER_BIT 0x00000100

//----------------------------------------------------------------------------------
// Constantes
//----------------------------------------------------------------------------------
#define ARENA          30.0f
#define MAX_ENEMIES    64
#define MAX_PARTICLES  2048
#define MAX_TRACERS    64
#define MAX_PICKUPS    32
#define MAX_BOXES      64
#define MAX_MSGS       6
#define MAX_BURGERS    12
#define START_BURGERS  12

#define EYE_HEIGHT     1.65f
#define PLAYER_RADIUS  0.35f
#define WALK_SPEED     5.5f
#define SPRINT_SPEED   8.5f
#define JUMP_SPEED     6.0f
#define GRAVITY        16.0f

#define MAG_SIZE       30
#define FIRE_DELAY     (60.0f/850.0f)   // HK416 : ~850 coups/min
#define RELOAD_TIME    2.2f
#define RELOAD_EMPTY   2.75f
#define BULLET_DAMAGE  30.0f
#define HEADSHOT_MULT  3.0f

#define FOV_HIP        75.0f
#define FOV_ADS        52.0f
#define FOV_SPRINT     82.0f

//----------------------------------------------------------------------------------
// Types
//----------------------------------------------------------------------------------
typedef enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER } GameState;
typedef enum { E_NORMAL, E_FAST, E_TANK, E_BOSS } EnemyType;
typedef enum { ES_WALK, ES_EAT, ES_DEAD } EnemyState;
typedef enum { PK_AMMO, PK_SALAD } PickupType;

typedef struct {
    bool active;
    EnemyType type;
    EnemyState state;
    Vector3 pos;
    float yaw;
    float hp, maxHp, speed, sz, fat, dmg;
    float phase, hitFlash, attackCd, attackAnim, eatT, deadT;
    float stuckT, sideSign;
    Color shirt, pants, skin, capCol;
    bool hasCap;
} Enemy;

typedef struct {
    Vector3 p, v;
    float life, maxLife, size, grav;
    Color c;
} Particle;

typedef struct { Vector3 a, b; float life; } Tracer;

typedef struct { bool active; PickupType type; Vector3 p; float t; } Pickup;

typedef enum { BX_CRATE, BX_CONCRETE, BX_WALL, BX_COUNTER } BoxStyle;
typedef struct { BoundingBox b; BoxStyle style; } Box;

typedef struct { char text[96]; float t; Color c; } Msg;

typedef enum { SND_SHOT, SND_CLICK, SND_EMPTY, SND_HIT, SND_NOM, SND_HURT, SND_KILL, SND_HORN, SND_PICK, SND_COUNT } SndId;

//----------------------------------------------------------------------------------
// Etat global
//----------------------------------------------------------------------------------
static struct {
    Vector3 pos, vel;
    float yaw, pitch;
    bool grounded;
    float hp, hurtT, regenDelay, bob, shake;
} P;

static struct {
    int ammo, reserve;
    float fireT, reloadT, reloadDur;
    bool reloading;
    int reloadStep;
    float ads, muzzleT, kick, recoilPitch, recoilYaw, bloom, sprintT;
    Vector2 sway;
    bool semi, shotThisPress;
} W;

static struct {
    GameState state;
    int wave, toSpawn, alive, score, best, kills, headshots, burgers;
    bool bossPending, inIntermission;
    float spawnT, interT, bannerT, hitT, time, titleT;
    bool hitKill, hitHead;
    char overReason[96];
} G;

static Enemy enemies[MAX_ENEMIES];
static Particle parts[MAX_PARTICLES];
static Tracer tracers[MAX_TRACERS];
static Pickup pickups[MAX_PICKUPS];
static Box boxes[MAX_BOXES];
static int boxCount;
static Msg msgs[MAX_MSGS];

static Model mCube, mSphere, mCyl;
static Shader shader;
static int locViewPos, locLightDir, locFogColor, locFogDensity, locFlashPos, locFlashPower, locEmissive, locChecker, locArena;

static Sound snd[SND_COUNT];
static Sound shotAlias[8];
static int shotIdx;

static Camera3D cam;
static const Vector3 STAND_POS = { 0.0f, 0.0f, 0.0f };
static Vector3 sunDir;
static const Color SKY_TOP = { 92, 148, 214, 255 };
static const Color SKY_HORIZON = { 196, 214, 232, 255 };

//----------------------------------------------------------------------------------
// Utilitaires
//----------------------------------------------------------------------------------
static float Randf(float a, float b) { return a + (b - a)*(float)rand()/(float)RAND_MAX; }

static Color Shade(Color c, float k)
{
    return (Color){ (unsigned char)Clamp(c.r*k, 0, 255), (unsigned char)Clamp(c.g*k, 0, 255),
                    (unsigned char)Clamp(c.b*k, 0, 255), c.a };
}

static Color Flash(Color c, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)Lerp(c.r, 255, t), (unsigned char)Lerp(c.g, 255, t),
                    (unsigned char)Lerp(c.b, 255, t), c.a };
}

static Vector4 ColorVec(Color c) { return (Vector4){ c.r/255.0f, c.g/255.0f, c.b/255.0f, c.a/255.0f }; }

static void AddMsg(Color c, const char *fmt, const char *arg)
{
    for (int i = MAX_MSGS - 1; i > 0; i--) msgs[i] = msgs[i - 1];
    snprintf(msgs[0].text, sizeof(msgs[0].text), fmt, arg);
    msgs[0].t = 4.0f;
    msgs[0].c = c;
}

static void Play(SndId id, float pitch, float vol)
{
    Sound s = snd[id];
    if (id == SND_SHOT) { s = shotAlias[shotIdx]; shotIdx = (shotIdx + 1) % 8; }
    if (!IsSoundValid(s)) return;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, vol);
    PlaySound(s);
}

//----------------------------------------------------------------------------------
// Sons proceduraux
//----------------------------------------------------------------------------------
static Sound GenSound(SndId id)
{
    float dur = 0.3f;
    switch (id)
    {
        case SND_SHOT: dur = 0.45f; break;
        case SND_CLICK: dur = 0.08f; break;
        case SND_EMPTY: dur = 0.05f; break;
        case SND_HIT: dur = 0.2f; break;
        case SND_NOM: dur = 0.5f; break;
        case SND_HURT: dur = 0.35f; break;
        case SND_KILL: dur = 0.35f; break;
        case SND_HORN: dur = 1.4f; break;
        case SND_PICK: dur = 0.25f; break;
        default: break;
    }
    const int sr = 44100;
    int n = (int)(dur*sr);
    short *data = (short *)MemAlloc(n*sizeof(short));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i/sr;
        float nz = Randf(-1.0f, 1.0f);
        lp += (nz - lp)*0.2f;
        lp2 += (nz - lp2)*0.05f;
        float v = 0.0f;
        switch (id)
        {
            case SND_SHOT:
                v = nz*expf(-t*70.0f)*0.9f + lp*expf(-t*13.0f)*1.7f + sinf(2*PI*55.0f*t)*expf(-t*10.0f)*0.8f;
                break;
            case SND_CLICK:
                v = nz*expf(-t*400.0f)*0.6f + sinf(2*PI*2400.0f*t)*expf(-t*150.0f)*0.4f;
                if (t > 0.035f) v += nz*expf(-(t - 0.035f)*500.0f)*0.5f;
                break;
            case SND_EMPTY:
                v = sinf(2*PI*3000.0f*t)*expf(-t*300.0f)*0.5f + nz*expf(-t*500.0f)*0.3f;
                break;
            case SND_HIT:
                v = lp2*expf(-t*20.0f)*2.2f + sinf(2*PI*(220.0f - 500.0f*t)*t)*expf(-t*18.0f)*0.6f;
                break;
            case SND_NOM:
            {
                float ch = 0.5f + 0.5f*sinf(2*PI*7.0f*t);
                v = (sinf(2*PI*140.0f*t) + 0.5f*sinf(2*PI*283.0f*t))*ch*0.35f*(1.0f - t/dur) + lp2*ch*0.8f;
            } break;
            case SND_HURT:
                v = sinf(2*PI*(140.0f - 120.0f*t)*t)*expf(-t*7.0f)*0.8f + lp2*expf(-t*15.0f)*0.9f;
                break;
            case SND_KILL:
                v = (sinf(2*PI*880.0f*t)*0.5f + sinf(2*PI*1320.0f*t)*0.3f)*expf(-t*9.0f);
                break;
            case SND_HORN:
            {
                float f0 = 196.0f + 6.0f*sinf(2*PI*5.0f*t);
                float saw = 2.0f*fmodf(t*f0, 1.0f) - 1.0f;
                float saw2 = 2.0f*fmodf(t*f0*1.5f, 1.0f) - 1.0f;
                float env = fminf(t*10.0f, 1.0f)*fminf((dur - t)*4.0f, 1.0f);
                v = (saw*0.22f + saw2*0.14f)*env;
            } break;
            case SND_PICK:
                v = sinf(2*PI*(600.0f + 900.0f*t/dur)*t)*expf(-t*6.0f)*0.4f;
                break;
            default: break;
        }
        data[i] = (short)(Clamp(v, -1.0f, 1.0f)*32000.0f);
    }
    Wave w = { .frameCount = (unsigned int)n, .sampleRate = sr, .sampleSize = 16, .channels = 1, .data = data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

//----------------------------------------------------------------------------------
// Shader d'eclairage (lambert + fog + flash de bouche du canon)
//----------------------------------------------------------------------------------
static const char *VS_CODE =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *FS_CODE =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 viewPos;\n"
    "uniform vec3 lightDir;\n"
    "uniform vec4 fogColor;\n"
    "uniform float fogDensity;\n"
    "uniform vec3 flashPos;\n"
    "uniform float flashPower;\n"
    "uniform float emissive;\n"
    "uniform float checker;\n"
    "uniform float arena;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 base = colDiffuse.rgb;\n"
    "    if (checker > 0.5) {\n"
    "        vec2 p = fragPosition.xz;\n"
    "        if (abs(p.x) > arena || abs(p.y) > arena) {\n"
    "            float g = fract(sin(dot(floor(p*2.0), vec2(12.9898, 78.233)))*43758.5453);\n"
    "            base = vec3(0.30, 0.52, 0.22)*(0.85 + 0.25*g);\n"
    "        } else {\n"
    "            float c = mod(floor(p.x*0.5) + floor(p.y*0.5), 2.0);\n"
    "            vec2 f = abs(fract(p*0.5) - 0.5);\n"
    "            float grout = step(0.47, max(f.x, f.y));\n"
    "            base *= mix(0.86, 1.0, c);\n"
    "            base = mix(base, base*0.6, grout);\n"
    "        }\n"
    "    }\n"
    "    vec3 n = normalize(fragNormal);\n"
    "    vec3 col;\n"
    "    if (emissive > 0.5) col = base;\n"
    "    else {\n"
    "        float diff = max(dot(n, -lightDir), 0.0);\n"
    "        float sky = 0.5 + 0.5*n.y;\n"
    "        col = base*(0.30 + 0.25*sky + 0.70*diff);\n"
    "        vec3 fl = flashPos - fragPosition;\n"
    "        float fd = length(fl);\n"
    "        float fa = flashPower*max(dot(n, fl/max(fd, 0.001)), 0.0)/(1.0 + fd*fd*0.35);\n"
    "        col += base*vec3(1.0, 0.75, 0.4)*fa;\n"
    "    }\n"
    "    float dist = length(viewPos - fragPosition);\n"
    "    float fog = 1.0 - exp(-pow(dist*fogDensity, 2.0));\n"
    "    col = mix(col, fogColor.rgb, clamp(fog, 0.0, 1.0));\n"
    "    finalColor = vec4(col, colDiffuse.a);\n"
    "}\n";

static void SetU1(int loc, float v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetU3(int loc, Vector3 v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_VEC3); }

//----------------------------------------------------------------------------------
// Primitives de dessin (cube / sphere / cylindre transformes)
//----------------------------------------------------------------------------------
static void Part(Model *m, Matrix world, Color c)
{
    m->transform = world;
    DrawModel(*m, (Vector3){ 0 }, 1.0f, c);
}

static Matrix TRS(Vector3 t, Vector3 r, Vector3 s)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateXYZ(r)), MatrixTranslate(t.x, t.y, t.z));
}

#define V3(x, y, z) ((Vector3){ (x), (y), (z) })
#define NOROT V3(0, 0, 0)

static void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c) { Part(&mCube, MatrixMultiply(TRS(t, r, s), parent), c); }
static void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c) { Part(&mSphere, MatrixMultiply(TRS(t, NOROT, radii), parent), c); }

// Cylindre de a vers b
static void Seg(Matrix parent, Vector3 a, Vector3 b, float rad, Color c)
{
    if (b.y < a.y) { Vector3 tmp = a; a = b; b = tmp; }
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-5f) return;
    Quaternion q = QuaternionFromVector3ToVector3(V3(0, 1, 0), Vector3Scale(d, 1.0f/len));
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(rad, len, rad), QuaternionToMatrix(q)), MatrixTranslate(a.x, a.y, a.z));
    Part(&mCyl, MatrixMultiply(m, parent), c);
}

// Membre arrondi : cylindre + rotules
static void Limb(Matrix parent, Vector3 a, Vector3 b, float rad, Color c)
{
    Seg(parent, a, b, rad, c);
    Ball(parent, a, V3(rad, rad, rad), c);
    Ball(parent, b, V3(rad, rad, rad), c);
}

static void DrawBurger(Matrix parent)
{
    const Color bun = { 214, 150, 62, 255 };
    Seg(parent, V3(0, 0, 0), V3(0, 0.05f, 0), 0.15f, Shade(bun, 0.9f));
    Seg(parent, V3(0, 0.05f, 0), V3(0, 0.095f, 0), 0.16f, (Color){ 92, 50, 26, 255 });
    Box3(parent, V3(0, 0.1f, 0), V3(0, PI/4, 0), V3(0.27f, 0.012f, 0.27f), (Color){ 250, 200, 40, 255 });
    Seg(parent, V3(0, 0.1f, 0), V3(0, 0.115f, 0), 0.168f, (Color){ 90, 175, 60, 255 });
    Seg(parent, V3(0, 0.115f, 0), V3(0, 0.13f, 0), 0.14f, (Color){ 210, 50, 40, 255 });
    Ball(parent, V3(0, 0.13f, 0), V3(0.155f, 0.09f, 0.155f), bun);
    for (int i = 0; i < 6; i++)
    {
        float a = i*1.1f;
        float r = 0.05f + 0.04f*(i % 2);
        Ball(parent, V3(cosf(a)*r, 0.13f + 0.085f - r*0.35f, sinf(a)*r), V3(0.012f, 0.007f, 0.018f), (Color){ 250, 240, 210, 255 });
    }
}

//----------------------------------------------------------------------------------
// Carte
//----------------------------------------------------------------------------------
static void AddBox(float x, float z, float sx, float sy, float sz, BoxStyle st)
{
    if (boxCount >= MAX_BOXES) return;
    boxes[boxCount++] = (Box){ { V3(x - sx/2, 0, z - sz/2), V3(x + sx/2, sy, z + sz/2) }, st };
}

static void BuildMap(void)
{
    boxCount = 0;
    // Comptoir du stand de burgers
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

    // Couverts : caisses et blocs de beton
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
// Repousse un cercle (plan XZ) hors des boites qu'il touche.
static void ResolveCircle(Vector3 *p, float r, float feetY, float height)
{
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        if (feetY >= b.max.y - 0.05f || feetY + height <= b.min.y) continue;
        float cx = Clamp(p->x, b.min.x, b.max.x);
        float cz = Clamp(p->z, b.min.z, b.max.z);
        float dx = p->x - cx, dz = p->z - cz;
        float d2 = dx*dx + dz*dz;
        if (d2 >= r*r) continue;
        if (d2 > 1e-8f)
        {
            float d = sqrtf(d2);
            p->x += dx/d*(r - d);
            p->z += dz/d*(r - d);
        }
        else
        {
            // Centre a l'interieur : sortie par la face la plus proche
            float l = p->x - b.min.x, rr = b.max.x - p->x, n = p->z - b.min.z, f = b.max.z - p->z;
            float m = fminf(fminf(l, rr), fminf(n, f));
            if (m == l) p->x = b.min.x - r;
            else if (m == rr) p->x = b.max.x + r;
            else if (m == n) p->z = b.min.z - r;
            else p->z = b.max.z + r;
        }
    }
}

static float GroundHeightAt(Vector3 p, float r, float feetY)
{
    float g = 0.0f;
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        if (p.x + r*0.7f < b.min.x || p.x - r*0.7f > b.max.x || p.z + r*0.7f < b.min.z || p.z - r*0.7f > b.max.z) continue;
        if (feetY >= b.max.y - 0.3f && b.max.y > g) g = b.max.y;
    }
    return g;
}

//----------------------------------------------------------------------------------
// Particules / effets
//----------------------------------------------------------------------------------
static void Spawn(Vector3 p, Vector3 v, float life, float size, float grav, Color c)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (parts[i].life > 0) continue;
        parts[i] = (Particle){ p, v, life, life, size, grav, c };
        return;
    }
}

static void BurstSauce(Vector3 p, Vector3 dir, int count)
{
    for (int i = 0; i < count; i++)
    {
        Color c = (rand() % 3 == 0) ? (Color){ 245, 200, 30, 255 } : (Color){ 200, 20, 15, 255 };
        Vector3 v = Vector3Add(Vector3Scale(dir, Randf(1.0f, 4.0f)), V3(Randf(-2, 2), Randf(0.5f, 4), Randf(-2, 2)));
        Spawn(p, v, Randf(0.5f, 1.2f), Randf(0.04f, 0.1f), 12.0f, c);
    }
}

static void BurstSparks(Vector3 p, Vector3 n)
{
    for (int i = 0; i < 8; i++)
    {
        Vector3 v = Vector3Add(Vector3Scale(n, Randf(1.0f, 4.0f)), V3(Randf(-2, 2), Randf(-1, 3), Randf(-2, 2)));
        Spawn(p, v, Randf(0.15f, 0.35f), 0.03f, 9.0f, (Color){ 255, 220, 120, 255 });
    }
    for (int i = 0; i < 5; i++)
    {
        Vector3 v = Vector3Add(Vector3Scale(n, Randf(0.3f, 1.2f)), V3(Randf(-0.4f, 0.4f), Randf(0.2f, 0.9f), Randf(-0.4f, 0.4f)));
        Spawn(p, v, Randf(0.4f, 0.8f), Randf(0.06f, 0.12f), -0.5f, (Color){ 150, 140, 125, 200 });
    }
}

static void BurstCrumbs(Vector3 p)
{
    for (int i = 0; i < 10; i++)
    {
        Color c = (rand() % 2) ? (Color){ 214, 150, 62, 255 } : (Color){ 92, 50, 26, 255 };
        Spawn(p, V3(Randf(-1.5f, 1.5f), Randf(0.5f, 2.5f), Randf(-1.5f, 1.5f)), Randf(0.5f, 1.0f), Randf(0.03f, 0.06f), 10.0f, c);
    }
}

static void AddTracer(Vector3 a, Vector3 b)
{
    for (int i = 0; i < MAX_TRACERS; i++)
    {
        if (tracers[i].life > 0) continue;
        tracers[i] = (Tracer){ a, b, 0.06f };
        return;
    }
}

static void DropPickup(Vector3 p)
{
    int r = rand() % 100;
    if (r >= 28) return;
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        if (pickups[i].active) continue;
        pickups[i] = (Pickup){ true, (r < 17) ? PK_AMMO : PK_SALAD, V3(p.x, 0, p.z), 0 };
        return;
    }
}

//----------------------------------------------------------------------------------
// Ennemis
//----------------------------------------------------------------------------------
static const char *EnemyName(EnemyType t)
{
    switch (t)
    {
        case E_FAST: return "Le Presse";
        case E_TANK: return "Le Glouton";
        case E_BOSS: return "LE ROI DU GRAS";
        default: return "Le Gourmand";
    }
}

static void SpawnEnemy(EnemyType type)
{
    int idx = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!enemies[i].active) { idx = i; break; }
    if (idx < 0) return;

    static const Color shirts[] = { { 230, 70, 60, 255 }, { 60, 120, 220, 255 }, { 250, 250, 245, 255 }, { 250, 200, 60, 255 },
                                    { 120, 190, 90, 255 }, { 200, 90, 200, 255 }, { 255, 140, 40, 255 } };
    static const Color skins[] = { { 255, 210, 170, 255 }, { 230, 180, 140, 255 }, { 190, 135, 95, 255 }, { 130, 90, 60, 255 } };
    static const Color pants[] = { { 50, 70, 120, 255 }, { 70, 70, 75, 255 }, { 110, 90, 60, 255 }, { 40, 40, 45, 255 } };

    Enemy *e = &enemies[idx];
    memset(e, 0, sizeof(*e));
    e->active = true;
    e->type = type;
    e->state = ES_WALK;
    e->shirt = shirts[rand() % 7];
    e->skin = skins[rand() % 4];
    e->pants = pants[rand() % 4];
    e->hasCap = (rand() % 3 == 0);
    e->capCol = shirts[rand() % 7];
    e->phase = Randf(0, 2*PI);
    e->sideSign = (rand() % 2) ? 1.0f : -1.0f;

    float waveMul = 1.0f + 0.12f*(G.wave - 1);
    switch (type)
    {
        case E_FAST: e->maxHp = 60; e->speed = 4.6f; e->sz = 0.85f; e->fat = 1.0f; e->dmg = 7; break;
        case E_TANK: e->maxHp = 320; e->speed = 1.7f; e->sz = 1.35f; e->fat = 1.35f; e->dmg = 20; break;
        case E_BOSS:
            e->maxHp = 2200 + 300*G.wave; e->speed = 1.9f; e->sz = 2.3f; e->fat = 1.4f; e->dmg = 35;
            e->shirt = (Color){ 240, 240, 240, 255 }; e->hasCap = false;
            break;
        default: e->maxHp = 90; e->speed = 2.7f; e->sz = 1.0f; e->fat = 1.0f; e->dmg = 12; break;
    }
    if (type != E_BOSS) e->maxHp *= waveMul;
    e->speed *= fminf(1.0f + 0.03f*(G.wave - 1), 1.4f);
    e->hp = e->maxHp;

    // Apparition derriere une des quatre portes
    int g = rand() % 4;
    float off = Randf(-1.6f, 1.6f), dist = ARENA + Randf(3.0f, 6.0f);
    switch (g)
    {
        case 0: e->pos = V3(off, 0, -dist); break;
        case 1: e->pos = V3(off, 0, dist); break;
        case 2: e->pos = V3(-dist, 0, off); break;
        default: e->pos = V3(dist, 0, off); break;
    }
    e->yaw = atan2f(-e->pos.x, -e->pos.z);
    if (type == E_BOSS) AddMsg(GOLD, "%s est arrive !", EnemyName(type));
}

static float EnemyRadius(const Enemy *e) { return 0.72f*e->sz*e->fat; }

static void KillEnemy(Enemy *e, bool head, Vector3 dir)
{
    e->state = ES_DEAD;
    e->deadT = 0;
    G.kills++;
    if (head) G.headshots++;
    int pts = 100;
    if (e->type == E_FAST) pts = 120;
    if (e->type == E_TANK) pts = 300;
    if (e->type == E_BOSS) pts = 2500;
    if (head) pts = pts*3/2;
    G.score += pts;
    G.hitKill = true;
    AddMsg(head ? ORANGE : RAYWHITE, head ? "%s neutralise (HEADSHOT)" : "%s neutralise", EnemyName(e->type));
    BurstSauce(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, e->type == E_BOSS ? 120 : 40);
    Play(SND_KILL, 1.0f, 0.5f);
    DropPickup(e->pos);
    if (e->type == E_BOSS) { DropPickup(e->pos); DropPickup(V3(e->pos.x + 1, 0, e->pos.z)); }
}

static void DamageEnemy(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir)
{
    e->hp -= dmg;
    e->hitFlash = 1.0f;
    G.hitT = 0.18f;
    G.hitKill = false;
    G.hitHead = head;
    BurstSauce(hitPos, Vector3Negate(dir), head ? 14 : 7);
    Play(SND_HIT, Randf(0.9f, 1.15f), 0.7f);
    if (e->hp <= 0) KillEnemy(e, head, dir);
}

static void HurtPlayer(float dmg)
{
    P.hp -= dmg;
    P.hurtT = 1.0f;
    P.regenDelay = 5.0f;
    P.shake = fmaxf(P.shake, 0.3f);
    Play(SND_HURT, Randf(0.9f, 1.1f), 0.8f);
    if (P.hp <= 0 && G.state == ST_PLAY)
    {
        P.hp = 0;
        G.state = ST_OVER;
        snprintf(G.overReason, sizeof(G.overReason), "Vous avez ete devore par les gloutons...");
    }
}

// Point du comptoir le plus proche
static Vector3 CounterPoint(Vector3 p)
{
    BoundingBox b = boxes[0].b;
    return V3(Clamp(p.x, b.min.x, b.max.x), 0, Clamp(p.z, b.min.z, b.max.z));
}

static void UpdateEnemies(float dt)
{
    G.alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        e->hitFlash = fmaxf(0, e->hitFlash - dt*6);
        e->attackAnim = fmaxf(0, e->attackAnim - dt*2.5f);
        e->attackCd -= dt;

        if (e->state == ES_DEAD)
        {
            e->deadT += dt;
            if (e->deadT > 3.5f) e->active = false;
            continue;
        }
        G.alive++;

        float rad = EnemyRadius(e);
        Vector3 toPlayer = Vector3Subtract(V3(P.pos.x, 0, P.pos.z), e->pos);
        float dPlayer = Vector3Length(toPlayer);
        bool outside = fabsf(e->pos.x) > ARENA - 0.5f || fabsf(e->pos.z) > ARENA - 0.5f;

        // Attaque au corps a corps
        float reach = rad + PLAYER_RADIUS + 0.5f;
        bool inReach = dPlayer < reach && P.pos.y < 1.6f*e->sz;
        if (inReach && e->attackCd <= 0)
        {
            e->attackCd = 1.3f;
            e->attackAnim = 1.0f;
            HurtPlayer(e->dmg);
        }

        // Choix de la cible
        Vector3 target;
        bool goStand = false;
        if (outside) target = V3(0, 0, 0);
        else if (e->type == E_FAST || dPlayer < 7.0f) target = V3(P.pos.x, 0, P.pos.z);
        else { target = CounterPoint(e->pos); goStand = true; }

        if (e->state == ES_EAT)
        {
            Vector3 cp = CounterPoint(e->pos);
            e->yaw = atan2f(cp.x - e->pos.x, cp.z - e->pos.z);
            e->eatT += dt;
            if (dPlayer < 3.0f) e->state = ES_WALK;
            else if (e->eatT > 4.0f)
            {
                e->eatT = 0;
                if (G.burgers > 0 && G.state == ST_PLAY)
                {
                    G.burgers--;
                    e->fat = fminf(e->fat + 0.15f, e->type == E_BOSS ? 2.0f : 2.2f);
                    e->hp = fminf(e->hp + e->maxHp*0.3f, e->maxHp*1.6f);
                    BurstCrumbs(V3(e->pos.x, 1.9f*e->sz, e->pos.z));
                    Play(SND_NOM, Randf(0.85f, 1.15f), 0.9f);
                    AddMsg((Color){ 255, 120, 80, 255 }, "%s a mange un burger !", EnemyName(e->type));
                    if (G.burgers == 0)
                    {
                        G.state = ST_OVER;
                        snprintf(G.overReason, sizeof(G.overReason), "Les gloutons ont devore tous les burgers !");
                    }
                }
            }
            continue;
        }

        Vector3 to = Vector3Subtract(target, e->pos);
        to.y = 0;
        float dist = Vector3Length(to);

        if (goStand && dist < rad + 0.35f)
        {
            e->state = ES_EAT;
            e->eatT = 0;
            continue;
        }

        float spd = e->speed/(0.7f + 0.3f*e->fat);
        bool stop = (!goStand && !outside && inReach);
        if (dist > 0.01f && !stop)
        {
            Vector3 dir = Vector3Scale(to, 1.0f/dist);
            // Contournement si bloque
            if (e->stuckT > 0.4f) dir = Vector3Normalize(Vector3Add(dir, V3(-dir.z*e->sideSign*1.5f, 0, dir.x*e->sideSign*1.5f)));
            Vector3 old = e->pos;
            e->pos = Vector3Add(e->pos, Vector3Scale(dir, spd*dt));
            ResolveCircle(&e->pos, rad, 0.0f, 2.0f*e->sz);
            float moved = Vector3Distance(old, e->pos);
            if (moved < spd*dt*0.3f) e->stuckT += dt;
            else e->stuckT = fmaxf(0, e->stuckT - dt*0.5f);
            if (e->stuckT > 2.0f) { e->stuckT = 0; e->sideSign = -e->sideSign; }

            float wantYaw = atan2f(dir.x, dir.z);
            float dy = wantYaw - e->yaw;
            while (dy > PI) dy -= 2*PI;
            while (dy < -PI) dy += 2*PI;
            e->yaw += dy*fminf(1.0f, dt*8.0f);
            e->phase += dt*spd*3.2f/e->sz;
        }
        else if (stop)
        {
            e->yaw = atan2f(toPlayer.x, toPlayer.z);
        }
    }

    // Separation entre ennemis
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
        // Les ennemis repoussent aussi le joueur
        float dx = P.pos.x - a->pos.x, dz = P.pos.z - a->pos.z;
        float rr = EnemyRadius(a) + PLAYER_RADIUS;
        float d2 = dx*dx + dz*dz;
        if (d2 < rr*rr && d2 > 1e-6f && P.pos.y < 1.5f*a->sz)
        {
            float d = sqrtf(d2);
            P.pos.x += dx/d*(rr - d);
            P.pos.z += dz/d*(rr - d);
        }
    }
}

static void DrawEnemy(const Enemy *e)
{
    float s = e->sz, f = e->fat;
    float tilt = 0, sink = 0;
    if (e->state == ES_DEAD)
    {
        float k = fminf(e->deadT/0.7f, 1.0f);
        tilt = (1.0f - (1.0f - k)*(1.0f - k))*PI*0.5f;
        if (e->deadT > 1.8f) sink = (e->deadT - 1.8f)*0.9f*s;
    }
    Matrix root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateX(-tilt)),
                                 MatrixRotateY(e->yaw)), MatrixTranslate(e->pos.x, -sink, e->pos.z));

    float hf = e->hitFlash;
    Color skin = Flash(e->skin, hf), shirt = Flash(e->shirt, hf), pants = Flash(e->pants, hf);
    Color shoe = Flash((Color){ 40, 40, 40, 255 }, hf);

    bool walking = e->state == ES_WALK;
    float sw = walking ? sinf(e->phase) : 0.0f;
    float bob = walking ? fabsf(sinf(e->phase))*0.05f : 0.0f;
    float jig = 1.0f + 0.05f*sinf(e->phase*2.0f + 0.5f);

    // Jambes
    for (int k = -1; k <= 1; k += 2)
    {
        float swing = sw*k*0.28f;
        Vector3 hip = V3(0.24f*k*f, 0.8f, 0);
        Vector3 foot = V3(0.27f*k*f, 0.14f + fmaxf(0, -swing)*0.25f, swing);
        Limb(root, hip, foot, 0.16f*sqrtf(f), pants);
        Box3(root, V3(foot.x, 0.07f, foot.z + 0.06f), NOROT, V3(0.24f, 0.14f, 0.36f), shoe);
    }

    // Corps
    Matrix body = MatrixMultiply(MatrixTranslate(0, bob, 0), root);
    Ball(body, V3(0, 0.88f, 0), V3(0.46f*f, 0.3f, 0.42f*f), pants);
    Ball(body, V3(0, 1.18f, 0.08f*f), V3(0.62f*f, 0.6f*jig, 0.62f*f), shirt);
    Ball(body, V3(0, 0.93f, 0.2f*f), V3(0.5f*f, 0.3f*jig, 0.5f*f), skin);          // ventre qui depasse
    Ball(body, V3(0, 0.95f, 0.69f*f), V3(0.03f, 0.04f, 0.02f), Shade(skin, 0.6f)); // nombril
    Ball(body, V3(0, 1.62f, 0), V3(0.48f*f, 0.33f, 0.4f*f), shirt);

    // Tete
    float mouthOpen = 0.35f;
    if (e->state == ES_EAT) mouthOpen = 0.5f + 0.5f*sinf(e->eatT*14.0f);
    else if (e->attackAnim > 0) mouthOpen = 1.0f;
    Ball(body, V3(0, 1.82f, 0.12f), V3(0.26f*f, 0.13f, 0.22f), skin);              // double menton
    Ball(body, V3(0, 2.02f, 0.02f), V3(0.31f + 0.03f*f, 0.3f, 0.3f), skin);
    Ball(body, V3(-0.13f, 1.98f, 0.24f), V3(0.08f, 0.07f, 0.06f), Flash((Color){ 240, 130, 120, 255 }, hf)); // joues
    Ball(body, V3(0.13f, 1.98f, 0.24f), V3(0.08f, 0.07f, 0.06f), Flash((Color){ 240, 130, 120, 255 }, hf));
    for (int k = -1; k <= 1; k += 2)
    {
        Ball(body, V3(0.11f*k, 2.1f, 0.25f), V3(0.065f, 0.065f, 0.04f), WHITE);
        Ball(body, V3(0.11f*k, 2.1f, 0.285f), V3(0.03f, 0.03f, 0.015f), BLACK);
    }
    Ball(body, V3(0, 1.91f, 0.27f), V3(0.11f, 0.03f + 0.07f*mouthOpen, 0.04f), (Color){ 90, 20, 20, 255 });

    if (e->hasCap)
    {
        Ball(body, V3(0, 2.2f, 0), V3(0.31f, 0.16f, 0.31f), Flash(e->capCol, hf));
        Box3(body, V3(0, 2.2f, -0.3f), NOROT, V3(0.26f, 0.03f, 0.22f), Flash(e->capCol, hf));
    }
    if (e->type == E_BOSS)
    {
        Color gold = Flash((Color){ 255, 200, 40, 255 }, hf);
        Seg(body, V3(0, 2.22f, 0), V3(0, 2.34f, 0), 0.25f, gold);
        for (int k = 0; k < 6; k++)
        {
            float a = k*PI/3;
            Ball(body, V3(cosf(a)*0.22f, 2.4f, sinf(a)*0.22f), V3(0.05f, 0.1f, 0.05f), gold);
        }
        // Bavoir plein de ketchup
        Box3(body, V3(0, 1.5f, 0.56f*f), V3(0.35f, 0, 0), V3(0.5f, 0.4f, 0.02f), Flash(WHITE, hf));
        Ball(body, V3(0.08f, 1.45f, 0.6f*f), V3(0.09f, 0.07f, 0.02f), (Color){ 200, 20, 15, 255 });
    }

    // Bras : tendus vers l'avant (vers les burgers !)
    for (int k = -1; k <= 1; k += 2)
    {
        Vector3 sh = V3(0.5f*k*f, 1.62f, 0);
        Vector3 el, hand;
        if (e->state == ES_EAT && k == 1)
        {
            float chew = sinf(e->eatT*6.0f)*0.05f;
            el = V3(0.45f*f, 1.45f, 0.35f);
            hand = V3(0.18f, 1.85f + chew, 0.42f);
        }
        else if (e->state == ES_DEAD)
        {
            el = V3(0.75f*k*f, 1.4f, -0.1f);
            hand = V3(0.95f*k*f, 1.2f, -0.2f);
        }
        else
        {
            float a = e->attackAnim;
            float sway = walking ? -sw*k*0.12f : 0.0f;
            el = V3(0.6f*k*f, 1.42f, 0.32f*f + sway);
            hand = V3(0.38f*k*f, 1.5f + a*0.3f, 0.62f*f + sway + a*0.35f);
        }
        Limb(body, sh, el, 0.14f, shirt);
        Limb(body, el, hand, 0.11f, skin);
        Ball(body, hand, V3(0.13f, 0.13f, 0.13f), skin);
        if (e->state == ES_EAT && k == 1)
        {
            Matrix bm = MatrixMultiply(MatrixMultiply(MatrixScale(0.9f, 0.9f, 0.9f), MatrixRotateX(-1.0f)),
                                       MatrixMultiply(MatrixTranslate(hand.x - 0.05f, hand.y - 0.05f, hand.z + 0.1f), body));
            DrawBurger(bm);
        }
    }
}

static void DrawShadow(Vector3 p, float r)
{
    Seg(MatrixIdentity(), V3(p.x, 0.01f, p.z), V3(p.x, 0.012f, p.z), r, (Color){ 0, 0, 0, 90 });
}

//----------------------------------------------------------------------------------
// Joueur et arme
//----------------------------------------------------------------------------------
static Vector3 CamForward(void)
{
    float cp = cosf(P.pitch + W.recoilPitch);
    return V3(sinf(P.yaw + W.recoilYaw)*cp, sinf(P.pitch + W.recoilPitch), cosf(P.yaw + W.recoilYaw)*cp);
}

static Vector3 EyePos(void) { return V3(P.pos.x, P.pos.y + EYE_HEIGHT, P.pos.z); }

// Position du canon en espace vue (doit correspondre au modele de l'arme)
static Vector3 WeaponOffset(void)
{
    Vector3 hip = V3(0.17f, -0.19f, -0.45f);
    Vector3 ads = V3(0.0f, -0.085f, -0.33f);
    Vector3 o = Vector3Lerp(hip, ads, W.ads);
    float bobAmt = (1.0f - W.ads*0.85f);
    o.x += sinf(P.bob)*0.012f*bobAmt + W.sway.x;
    o.y += -fabsf(cosf(P.bob))*0.012f*bobAmt + W.sway.y;
    o.z += W.kick*0.06f;
    o.y -= W.sprintT*0.06f;
    o.x -= W.sprintT*0.04f;
    return o;
}

static Matrix WeaponMatrix(void)
{
    Vector3 o = WeaponOffset();
    float reloadTilt = 0.0f;
    if (W.reloading)
    {
        float t = W.reloadT/W.reloadDur;
        reloadTilt = sinf(fminf(t, 1.0f)*PI);
    }
    Vector3 rot = V3(W.kick*0.12f - reloadTilt*0.25f - W.sprintT*0.25f, W.sprintT*0.7f + reloadTilt*0.2f, reloadTilt*0.5f + W.sprintT*0.35f);
    return MatrixMultiply(MatrixRotateXYZ(rot), MatrixTranslate(o.x, o.y, o.z));
}

static Vector3 ViewToWorld(Vector3 v)
{
    Vector3 f = CamForward();
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);
    Vector3 eye = EyePos();
    return Vector3Add(eye, Vector3Add(Vector3Scale(r, v.x), Vector3Add(Vector3Scale(u, v.y), Vector3Scale(f, -v.z))));
}

static Vector3 MuzzleView(void) { return Vector3Transform(V3(0, 0, -0.68f), WeaponMatrix()); }

static void StartReload(void)
{
    if (W.reloading || W.ammo >= MAG_SIZE + 1 || W.reserve <= 0) return;
    if (W.ammo == MAG_SIZE) return;
    W.reloading = true;
    W.reloadT = 0;
    W.reloadStep = 0;
    W.reloadDur = (W.ammo == 0) ? RELOAD_EMPTY : RELOAD_TIME;
}

static void Shoot(void)
{
    W.ammo--;
    W.fireT = FIRE_DELAY;
    W.muzzleT = 0.05f;
    W.kick = 1.0f;
    W.recoilPitch += Randf(0.010f, 0.016f)*(1.0f - W.ads*0.35f);
    W.recoilYaw += Randf(-0.006f, 0.006f);
    W.bloom = fminf(W.bloom + 0.006f, 0.04f);
    W.sprintT = 0.0f;
    P.shake = fmaxf(P.shake, 0.04f);
    Play(SND_SHOT, Randf(0.92f, 1.08f), 0.55f);

    Vector3 eye = EyePos();
    Vector3 f = CamForward();
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);

    float hspeed = sqrtf(P.vel.x*P.vel.x + P.vel.z*P.vel.z);
    float spread = Lerp(0.022f, 0.002f, W.ads) + hspeed*0.003f + (P.grounded ? 0.0f : 0.04f) + W.bloom;
    float ang = Randf(0, 2*PI), mag = Randf(0, 1)*spread;
    Vector3 dir = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(r, cosf(ang)*mag), Vector3Scale(u, sinf(ang)*mag))));
    Ray ray = { eye, dir };

    float best = 250.0f;
    Vector3 normal = V3(0, 1, 0);
    int hitEnemy = -1;
    bool head = false;

    if (dir.y < -1e-4f)
    {
        float t = -eye.y/dir.y;
        if (t < best) { best = t; normal = V3(0, 1, 0); }
    }
    for (int i = 0; i < boxCount; i++)
    {
        RayCollision c = GetRayCollisionBox(ray, boxes[i].b);
        if (c.hit && c.distance < best) { best = c.distance; normal = c.normal; }
    }
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        float s = e->sz;
        Vector3 hc = V3(e->pos.x, 2.02f*s, e->pos.z);
        RayCollision c = GetRayCollisionSphere(ray, hc, 0.31f*s);
        if (c.hit && c.distance < best) { best = c.distance; hitEnemy = i; head = true; }
        Vector3 fw = V3(sinf(e->yaw), 0, cosf(e->yaw));
        Vector3 bc = Vector3Add(V3(e->pos.x, 1.15f*s, e->pos.z), Vector3Scale(fw, 0.08f*s*e->fat));
        c = GetRayCollisionSphere(ray, bc, 0.64f*s*e->fat);
        if (c.hit && c.distance < best) { best = c.distance; hitEnemy = i; head = false; }
        c = GetRayCollisionSphere(ray, V3(e->pos.x, 0.5f*s, e->pos.z), 0.45f*s*e->fat);
        if (c.hit && c.distance < best) { best = c.distance; hitEnemy = i; head = false; }
    }

    Vector3 hit = Vector3Add(eye, Vector3Scale(dir, best));
    AddTracer(ViewToWorld(MuzzleView()), hit);
    if (hitEnemy >= 0)
    {
        DamageEnemy(&enemies[hitEnemy], BULLET_DAMAGE*(head ? HEADSHOT_MULT : 1.0f), head, hit, dir);
    }
    else if (best < 249.0f)
    {
        BurstSparks(hit, normal);
    }
}

static void UpdateWeapon(float dt, bool wantFire, bool firePressed, bool sprinting)
{
    W.fireT -= dt;
    W.muzzleT -= dt;
    W.kick = fmaxf(0, W.kick - dt*12.0f);
    W.recoilPitch -= W.recoilPitch*fminf(1.0f, dt*6.0f);
    W.recoilYaw -= W.recoilYaw*fminf(1.0f, dt*6.0f);
    W.bloom = fmaxf(0, W.bloom - dt*0.08f);

    bool aiming = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !W.reloading && !sprinting;
    W.ads += ((aiming ? 1.0f : 0.0f) - W.ads)*fminf(1.0f, dt*14.0f);
    W.sprintT += ((sprinting ? 1.0f : 0.0f) - W.sprintT)*fminf(1.0f, dt*10.0f);

    Vector2 md = GetMouseDelta();
    W.sway.x += (-md.x*0.0004f - W.sway.x)*fminf(1.0f, dt*8.0f);
    W.sway.y += (md.y*0.0004f - W.sway.y)*fminf(1.0f, dt*8.0f);
    W.sway.x = Clamp(W.sway.x, -0.03f, 0.03f);
    W.sway.y = Clamp(W.sway.y, -0.03f, 0.03f);

    if (IsKeyPressed(KEY_R)) StartReload();
    if (IsKeyPressed(KEY_B))
    {
        W.semi = !W.semi;
        AddMsg(SKYBLUE, "Mode de tir : %s", W.semi ? "semi-auto" : "automatique");
        Play(SND_CLICK, 1.4f, 0.5f);
    }

    if (W.reloading)
    {
        W.reloadT += dt;
        float marks[3] = { 0.3f, 1.45f, 2.35f };
        int steps = (W.reloadDur > RELOAD_TIME) ? 3 : 2;
        if (W.reloadStep < steps && W.reloadT > marks[W.reloadStep])
        {
            Play(SND_CLICK, W.reloadStep == 2 ? 0.7f : 1.0f, 0.7f);
            W.reloadStep++;
        }
        if (W.reloadT >= W.reloadDur)
        {
            int chamber = (W.ammo > 0) ? 1 : 0;   // une balle reste chambree sur un rechargement tactique
            int need = MAG_SIZE + chamber - W.ammo;
            int take = (need < W.reserve) ? need : W.reserve;
            W.ammo += take;
            W.reserve -= take;
            W.reloading = false;
        }
        return;
    }

    if (!firePressed) W.shotThisPress = false;
    bool trigger = W.semi ? (wantFire && !W.shotThisPress) : wantFire;
    if (trigger && W.fireT <= 0 && !sprinting)
    {
        if (W.ammo > 0)
        {
            Shoot();
            W.shotThisPress = true;
        }
        else
        {
            if (firePressed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) Play(SND_EMPTY, 1.0f, 0.6f);
            W.fireT = 0.25f;
            StartReload();
        }
    }
}

static void UpdatePlayer(float dt, bool *sprinting)
{
    Vector2 md = GetMouseDelta();
    float sens = 0.0022f*(1.0f - W.ads*0.35f);
    P.yaw -= md.x*sens;
    P.pitch -= md.y*sens;
    P.pitch = Clamp(P.pitch, -1.5f, 1.5f);

    Vector3 fwd = V3(sinf(P.yaw), 0, cosf(P.yaw));
    Vector3 right = V3(-cosf(P.yaw), 0, sinf(P.yaw));
    float mf = 0, mr = 0;
    // ZQSD (AZERTY) et WASD (QWERTY) sont tous deux supportes
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_Z) || IsKeyDown(KEY_UP)) mf += 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) mf -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) mr += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT)) mr -= 1;

    *sprinting = IsKeyDown(KEY_LEFT_SHIFT) && mf > 0 && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
                 !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !W.reloading;
    float speed = *sprinting ? SPRINT_SPEED : WALK_SPEED;
    speed *= (1.0f - W.ads*0.4f);

    Vector3 wish = Vector3Add(Vector3Scale(fwd, mf), Vector3Scale(right, mr));
    if (Vector3Length(wish) > 0.01f) wish = Vector3Scale(Vector3Normalize(wish), speed);
    float accel = P.grounded ? 12.0f : 2.5f;
    P.vel.x += (wish.x - P.vel.x)*fminf(1.0f, dt*accel);
    P.vel.z += (wish.z - P.vel.z)*fminf(1.0f, dt*accel);

    if (P.grounded && IsKeyPressed(KEY_SPACE)) { P.vel.y = JUMP_SPEED; P.grounded = false; }

    P.pos.x += P.vel.x*dt;
    P.pos.z += P.vel.z*dt;
    ResolveCircle(&P.pos, PLAYER_RADIUS, P.pos.y, 1.8f);
    P.pos.x = Clamp(P.pos.x, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);
    P.pos.z = Clamp(P.pos.z, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);

    float ground = GroundHeightAt(P.pos, PLAYER_RADIUS, P.pos.y);
    P.vel.y -= GRAVITY*dt;
    P.pos.y += P.vel.y*dt;
    if (P.pos.y <= ground)
    {
        P.pos.y = ground;
        P.vel.y = 0;
        P.grounded = true;
    }
    else if (P.pos.y > ground + 0.05f) P.grounded = false;

    float hs = sqrtf(P.vel.x*P.vel.x + P.vel.z*P.vel.z);
    if (P.grounded) P.bob += dt*hs*1.6f;

    P.hurtT = fmaxf(0, P.hurtT - dt*1.5f);
    P.shake = fmaxf(0, P.shake - dt*1.5f);
    P.regenDelay -= dt;
    if (P.regenDelay <= 0 && P.hp < 100) P.hp = fminf(100, P.hp + dt*6.0f);

    // Ramassage des bonus
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        Pickup *k = &pickups[i];
        if (!k->active) continue;
        k->t += dt;
        if (k->t > 25.0f) { k->active = false; continue; }
        float dx = k->p.x - P.pos.x, dz = k->p.z - P.pos.z;
        if (dx*dx + dz*dz < 1.3f*1.3f && P.pos.y < 1.5f)
        {
            if (k->type == PK_AMMO)
            {
                if (W.reserve >= 300) continue;
                W.reserve = (W.reserve + 60 > 300) ? 300 : W.reserve + 60;
                AddMsg(GREEN, "%s", "+60 munitions 5.56");
            }
            else
            {
                if (P.hp >= 100) continue;
                P.hp = fminf(100, P.hp + 35);
                AddMsg(LIME, "%s", "+35 PV (une salade, beurk)");
            }
            Play(SND_PICK, 1.0f, 0.6f);
            k->active = false;
        }
    }
}

static void DrawWeapon(void)
{
    Matrix W0 = WeaponMatrix();
    const Color metal = { 38, 38, 36, 255 }, metal2 = { 55, 54, 50, 255 }, polymer = { 30, 30, 30, 255 };
    const Color glove = { 40, 38, 34, 255 }, sleeve = { 96, 100, 70, 255 };

    // Receptacle superieur / inferieur
    Box3(W0, V3(0, 0, 0.0f), NOROT, V3(0.05f, 0.06f, 0.28f), metal);
    Box3(W0, V3(0, -0.048f, 0.02f), NOROT, V3(0.045f, 0.045f, 0.2f), metal2);
    Box3(W0, V3(0.026f, 0.005f, -0.02f), NOROT, V3(0.004f, 0.03f, 0.07f), (Color){ 25, 25, 25, 255 }); // fenetre d'ejection
    Box3(W0, V3(0, 0.032f, 0.15f), NOROT, V3(0.04f, 0.012f, 0.025f), metal2); // levier d'armement
    // Poignee pistolet
    Box3(W0, V3(0, -0.11f, 0.085f), V3(-0.35f, 0, 0), V3(0.036f, 0.11f, 0.045f), polymer);
    // Pontet
    Box3(W0, V3(0, -0.085f, 0.025f), NOROT, V3(0.012f, 0.008f, 0.07f), metal);
    // Garde-main quad rail (signature du HK416)
    Box3(W0, V3(0, 0.0f, -0.29f), NOROT, V3(0.062f, 0.062f, 0.3f), metal2);
    for (int i = 0; i < 4; i++)
    {
        float a = i*PI*0.5f;
        Vector3 c = V3(cosf(a)*0.036f, sinf(a)*0.036f, -0.29f);
        Box3(W0, c, V3(0, 0, a), V3(0.008f, 0.02f, 0.29f), metal);
    }
    // Rail picatinny superieur
    Box3(W0, V3(0, 0.036f, -0.13f), NOROT, V3(0.024f, 0.01f, 0.6f), metal);
    for (int i = 0; i < 22; i++) Box3(W0, V3(0, 0.043f, 0.15f - i*0.026f), NOROT, V3(0.026f, 0.006f, 0.012f), metal2);
    // Bloc de gaz (piston), canon, cache-flamme
    Box3(W0, V3(0, 0.012f, -0.475f), NOROT, V3(0.034f, 0.05f, 0.04f), metal);
    Seg(W0, V3(0, 0, -0.44f), V3(0, 0, -0.62f), 0.011f, metal);
    Seg(W0, V3(0, 0, -0.6f), V3(0, 0, -0.68f), 0.016f, (Color){ 20, 20, 20, 255 });
    // Tube et crosse
    Seg(W0, V3(0, 0.005f, 0.14f), V3(0, 0.005f, 0.34f), 0.016f, metal);
    Box3(W0, V3(0, -0.02f, 0.3f), NOROT, V3(0.046f, 0.085f, 0.17f), polymer);
    Box3(W0, V3(0, -0.035f, 0.39f), NOROT, V3(0.05f, 0.12f, 0.02f), (Color){ 20, 20, 20, 255 });

    // Viseur point rouge : anneau ouvert pour pouvoir viser au travers
    const float zc = -0.04f, R = 0.02f;
    Box3(W0, V3(0, 0.055f, zc), NOROT, V3(0.026f, 0.03f, 0.04f), metal);
    for (int i = 0; i < 8; i++)
    {
        float a = i*PI/4;
        Box3(W0, V3(cosf(a)*R, 0.085f + sinf(a)*R, zc), V3(0, 0, a - PI/2), V3(0.0175f, 0.004f, 0.06f), polymer);
    }
    Box3(W0, V3(0.023f, 0.085f, zc), NOROT, V3(0.008f, 0.012f, 0.014f), polymer); // molette de reglage
    SetU1(locEmissive, 1.0f);
    Ball(W0, V3(0, 0.085f, zc - 0.03f), V3(0.0012f, 0.0012f, 0.0012f), (Color){ 255, 30, 30, 255 });
    SetU1(locEmissive, 0.0f);

    // Chargeur (anime pendant le rechargement)
    Vector3 magPos = V3(0, -0.14f, -0.065f);
    Vector3 handL = V3(-0.005f, -0.045f, -0.33f);
    bool magVisible = true;
    if (W.reloading)
    {
        float t = W.reloadT;
        if (t < 0.3f) magPos.y -= t/0.3f*0.12f;
        else if (t < 1.1f) { magPos.y -= 0.4f; magVisible = false; }
        else if (t < 1.45f) magPos.y -= (1.0f - (t - 1.1f)/0.35f)*0.15f;
        if (t > 0.1f && t < 1.6f) handL = Vector3Lerp(handL, V3(magPos.x - 0.01f, magPos.y - 0.06f, magPos.z), fminf(1.0f, (t - 0.1f)*5.0f));
        if (t >= 1.1f && t < 1.45f) handL = V3(magPos.x - 0.01f, magPos.y - 0.06f, magPos.z);
    }
    if (magVisible)
    {
        Matrix mm = MatrixMultiply(MatrixMultiply(MatrixRotateX(0.18f), MatrixTranslate(magPos.x, magPos.y, magPos.z)), W0);
        Box3(mm, V3(0, 0.03f, 0), NOROT, V3(0.034f, 0.11f, 0.066f), metal2);
        Box3(mm, V3(0, -0.04f, -0.008f), V3(0.2f, 0, 0), V3(0.034f, 0.07f, 0.066f), metal2);
        Box3(mm, V3(0, -0.08f, -0.018f), V3(0.2f, 0, 0), V3(0.04f, 0.014f, 0.072f), polymer);
    }

    // Mains et avant-bras
    Ball(W0, handL, V3(0.04f, 0.035f, 0.06f), glove);
    Limb(W0, Vector3Add(handL, V3(-0.02f, -0.03f, 0.03f)), V3(-0.13f, -0.26f, 0.05f), 0.042f, sleeve);
    Ball(W0, V3(0.0f, -0.1f, 0.095f), V3(0.035f, 0.05f, 0.045f), glove);
    Limb(W0, V3(0.02f, -0.125f, 0.12f), V3(0.12f, -0.25f, 0.38f), 0.045f, sleeve);

    // Flash du canon
    if (W.muzzleT > 0)
    {
        SetU1(locEmissive, 1.0f);
        float k = 0.8f + Randf(0, 0.6f);
        float rz = Randf(0, PI);
        Matrix fm = MatrixMultiply(MatrixRotateZ(rz), MatrixMultiply(MatrixTranslate(0, 0, -0.72f), W0));
        Ball(fm, V3(0, 0, 0), V3(0.035f*k, 0.035f*k, 0.06f*k), (Color){ 255, 240, 170, 255 });
        for (int i = 0; i < 4; i++)
            Box3(fm, V3(0, 0, -0.01f), V3(0, 0, i*PI/4), V3(0.12f*k, 0.012f, 0.012f), (Color){ 255, 180, 60, 255 });
        Ball(fm, V3(0, 0, -0.05f*k), V3(0.02f*k, 0.02f*k, 0.08f*k), (Color){ 255, 200, 90, 255 });
        SetU1(locEmissive, 0.0f);
    }
}

//----------------------------------------------------------------------------------
// Decor
//----------------------------------------------------------------------------------
static void DrawBoxStyled(const Box *bx)
{
    Vector3 c = Vector3Scale(Vector3Add(bx->b.min, bx->b.max), 0.5f);
    Vector3 s = Vector3Subtract(bx->b.max, bx->b.min);
    Matrix I = MatrixIdentity();
    switch (bx->style)
    {
        case BX_CRATE:
        {
            const Color dark = { 110, 72, 36, 255 }, light = { 168, 118, 64, 255 };
            float b = 0.09f;
            Box3(I, c, NOROT, s, dark);
            Box3(I, c, NOROT, V3(s.x + 0.004f, s.y - 2*b, s.z - 2*b), light);
            Box3(I, c, NOROT, V3(s.x - 2*b, s.y + 0.004f, s.z - 2*b), light);
            Box3(I, c, NOROT, V3(s.x - 2*b, s.y - 2*b, s.z + 0.004f), light);
        } break;
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
        {
            // Comptoir rouge et blanc + toit + enseigne burger geante
            Box3(I, c, NOROT, s, (Color){ 200, 35, 35, 255 });
            for (int i = 0; i < 5; i++)
                Box3(I, V3(-1.76f + i*0.88f, 0.55f, c.z), NOROT, V3(0.44f, 1.104f, s.z + 0.004f), (Color){ 245, 245, 240, 255 });
            Box3(I, V3(c.x, s.y + 0.04f, c.z), NOROT, V3(s.x + 0.2f, 0.08f, s.z + 0.2f), (Color){ 220, 220, 225, 255 });
            for (int k = 0; k < 4; k++)
            {
                float px = (k & 1) ? 2.1f : -2.1f, pz = (k & 2) ? 0.7f : -0.7f;
                Seg(I, V3(px, 1.1f, pz), V3(px, 3.0f, pz), 0.05f, (Color){ 200, 200, 205, 255 });
            }
            for (int i = 0; i < 6; i++)
                Box3(I, V3(-2.3f + i*0.92f + 0.46f, 3.05f, 0), V3(0, 0, 0), V3(0.92f, 0.12f, 2.2f),
                     (i % 2) ? (Color){ 245, 245, 240, 255 } : (Color){ 200, 35, 35, 255 });
            Seg(I, V3(0, 3.1f, 0), V3(0, 4.2f, 0), 0.08f, (Color){ 120, 120, 125, 255 });
            Matrix sign = MatrixMultiply(MatrixMultiply(MatrixScale(7, 7, 7), MatrixRotateY(G.time*0.8f)), MatrixTranslate(0, 4.1f, 0));
            DrawBurger(sign);
        } break;
    }
}

static void DrawStandBurgers(void)
{
    for (int i = 0; i < G.burgers; i++)
    {
        int row = i/6, col = i % 6;
        float x = -1.75f + col*0.7f;
        float z = (row == 0) ? 0.35f : -0.35f;
        DrawBurger(MatrixTranslate(x, 1.18f, z));
    }
}

static void DrawPickups(void)
{
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        Pickup *k = &pickups[i];
        if (!k->active) continue;
        if (k->t > 20.0f && fmodf(k->t, 0.3f) < 0.15f) continue;   // clignote avant de disparaitre
        Matrix m = MatrixMultiply(MatrixRotateY(k->t*2.0f), MatrixTranslate(k->p.x, 0.45f + sinf(k->t*3.0f)*0.1f, k->p.z));
        DrawShadow(k->p, 0.35f);
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
                Ball(m, V3(cosf(a)*0.13f, 0.08f, sinf(a)*0.13f), V3(0.12f, 0.06f, 0.1f), (j % 3) ? (Color){ 90, 190, 60, 255 } : (Color){ 220, 50, 40, 255 });
            }
        }
    }
}

static void DrawWorld(void)
{
    Matrix I = MatrixIdentity();
    SetU1(locChecker, 1.0f);
    Box3(I, V3(0, -0.5f, 0), NOROT, V3(240, 1, 240), (Color){ 140, 138, 134, 255 });
    SetU1(locChecker, 0.0f);

    for (int i = 0; i < boxCount; i++) DrawBoxStyled(&boxes[i]);
    DrawStandBurgers();

    // Lampadaires decoratifs
    for (int k = 0; k < 4; k++)
    {
        float x = (k & 1) ? 26.0f : -26.0f, z = (k & 2) ? 26.0f : -26.0f;
        Seg(I, V3(x, 0, z), V3(x, 6, z), 0.12f, (Color){ 60, 62, 66, 255 });
        Box3(I, V3(x, 6.05f, z), NOROT, V3(0.7f, 0.2f, 0.4f), (Color){ 60, 62, 66, 255 });
    }

    DrawPickups();

    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        if (e->state != ES_DEAD) DrawShadow(e->pos, EnemyRadius(e)*1.1f);
        DrawEnemy(e);
    }
}

static void DrawEffects(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        float k = p->life/p->maxLife;
        Color c = p->c;
        c.a = (unsigned char)(c.a*fminf(1.0f, k*2.0f));
        DrawCube(p->p, p->size, p->size, p->size, c);
    }
    for (int i = 0; i < MAX_TRACERS; i++)
    {
        if (tracers[i].life <= 0) continue;
        Vector3 a = tracers[i].a, b = tracers[i].b;
        Vector3 d = Vector3Subtract(b, a);
        // Trainee courte qui "voyage" le long de la trajectoire
        float t = 1.0f - tracers[i].life/0.06f;
        Vector3 s = Vector3Add(a, Vector3Scale(d, fmaxf(0, t - 0.25f)));
        Vector3 e = Vector3Add(a, Vector3Scale(d, fminf(1, t + 0.15f)));
        DrawCylinderEx(s, e, 0.012f, 0.012f, 4, (Color){ 255, 230, 150, 200 });
    }
}

static void UpdateEffects(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->v.y -= p->grav*dt;
        p->p = Vector3Add(p->p, Vector3Scale(p->v, dt));
        if (p->p.y < 0.02f) { p->p.y = 0.02f; p->v = Vector3Scale(p->v, 0.3f); p->v.y = 0; }
    }
    for (int i = 0; i < MAX_TRACERS; i++) tracers[i].life -= dt;
    for (int i = 0; i < MAX_MSGS; i++) msgs[i].t -= dt;
}

//----------------------------------------------------------------------------------
// Vagues
//----------------------------------------------------------------------------------
static int WaveSize(int n) { return 3 + n*3; }

static void StartWave(int n)
{
    G.wave = n;
    G.toSpawn = WaveSize(n);
    G.bossPending = (n % 5 == 0);
    G.spawnT = 1.0f;
    G.inIntermission = false;
    G.bannerT = 3.0f;
    Play(SND_HORN, 1.0f, 0.6f);
}

static EnemyType PickType(int n)
{
    int r = rand() % 100;
    if (n >= 2 && r < 12 + n*2) return E_FAST;
    if (n >= 3 && r > 92 - n*2) return E_TANK;
    return E_NORMAL;
}

static void UpdateWaves(float dt)
{
    if (G.inIntermission)
    {
        G.interT -= dt;
        if (G.interT <= 0) StartWave(G.wave + 1);
        return;
    }
    if (G.toSpawn > 0 || G.bossPending)
    {
        G.spawnT -= dt;
        if (G.spawnT <= 0 && G.alive < 40)
        {
            if (G.bossPending && G.toSpawn <= WaveSize(G.wave)/2) { SpawnEnemy(E_BOSS); G.bossPending = false; }
            else if (G.toSpawn > 0) { SpawnEnemy(PickType(G.wave)); G.toSpawn--; }
            G.spawnT = fmaxf(0.35f, 2.2f - G.wave*0.15f)*Randf(0.6f, 1.3f);
        }
    }
    else if (G.alive == 0)
    {
        G.inIntermission = true;
        G.interT = 8.0f;
        G.score += 250*G.wave;
        G.burgers = (G.burgers + 2 > MAX_BURGERS) ? MAX_BURGERS : G.burgers + 2;
        W.reserve = (W.reserve + 90 > 300) ? 300 : W.reserve + 90;
        P.hp = fminf(100, P.hp + 25);
        Play(SND_PICK, 0.8f, 0.6f);
    }
}

static void ResetGame(void)
{
    memset(enemies, 0, sizeof(enemies));
    memset(parts, 0, sizeof(parts));
    memset(tracers, 0, sizeof(tracers));
    memset(pickups, 0, sizeof(pickups));
    memset(msgs, 0, sizeof(msgs));
    memset(&P, 0, sizeof(P));
    memset(&W, 0, sizeof(W));
    P.pos = V3(0, 0, 9);
    P.yaw = PI;
    P.pitch = -0.08f;
    P.hp = 100;
    P.grounded = true;
    W.ammo = MAG_SIZE;
    W.reserve = 180;
    int best = G.best;
    memset(&G, 0, sizeof(G));
    G.best = best;
    G.burgers = START_BURGERS;
    G.state = ST_PLAY;
    StartWave(1);
}

//----------------------------------------------------------------------------------
// HUD
//----------------------------------------------------------------------------------
static void TextCenter(const char *t, int cx, int y, int size, Color c)
{
    int w = MeasureText(t, size);
    DrawText(t, cx - w/2 + 2, y + 2, size, (Color){ 0, 0, 0, (unsigned char)(c.a*0.6f) });
    DrawText(t, cx - w/2, y, size, c);
}

static void TextShadow(const char *t, int x, int y, int size, Color c)
{
    DrawText(t, x + 2, y + 2, size, (Color){ 0, 0, 0, (unsigned char)(c.a*0.6f) });
    DrawText(t, x, y, size, c);
}

static void DrawBurgerIcon(int x, int y, float s, bool full)
{
    Color bun = full ? (Color){ 230, 160, 60, 255 } : (Color){ 90, 90, 90, 160 };
    Color meat = full ? (Color){ 100, 55, 25, 255 } : (Color){ 60, 60, 60, 160 };
    Color let = full ? (Color){ 100, 200, 70, 255 } : (Color){ 70, 70, 70, 160 };
    DrawCircleSector((Vector2){ (float)x, y + 8*s }, 11*s, 180, 360, 16, bun);
    DrawRectangle((int)(x - 12*s), (int)(y + 8*s), (int)(24*s), (int)(3*s), let);
    DrawRectangle((int)(x - 11*s), (int)(y + 11*s), (int)(22*s), (int)(5*s), meat);
    DrawRectangleRounded((Rectangle){ x - 11*s, y + 16*s, 22*s, 5*s }, 0.6f, 4, bun);
}

static void DrawRadar(int cx, int cy, int R)
{
    DrawCircle(cx, cy, (float)R, (Color){ 0, 0, 0, 120 });
    DrawCircleLines(cx, cy, (float)R, (Color){ 255, 255, 255, 120 });
    DrawLine(cx - R, cy, cx + R, cy, (Color){ 255, 255, 255, 40 });
    DrawLine(cx, cy - R, cx, cy + R, (Color){ 255, 255, 255, 40 });
    float range = 32.0f, k = R/range;
    Vector2 f = { sinf(P.yaw), cosf(P.yaw) }, r = { -cosf(P.yaw), sinf(P.yaw) };
    #define RADAR_PT(wx, wz, out) do { float dx = (wx) - P.pos.x, dz = (wz) - P.pos.z; \
        float fr = dx*f.x + dz*f.y, rr = dx*r.x + dz*r.y; out = (Vector2){ cx + rr*k, cy - fr*k }; } while (0)
    Vector2 sp;
    RADAR_PT(STAND_POS.x, STAND_POS.z, sp);
    if (Vector2Distance(sp, (Vector2){ (float)cx, (float)cy }) < R) DrawRectangle((int)sp.x - 5, (int)sp.y - 3, 10, 6, (Color){ 255, 200, 40, 255 });
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        RADAR_PT(e->pos.x, e->pos.z, sp);
        if (Vector2Distance(sp, (Vector2){ (float)cx, (float)cy }) > R - 3) continue;
        DrawCircleV(sp, e->type == E_BOSS ? 6.0f : 3.0f, e->state == ES_EAT ? ORANGE : RED);
    }
    #undef RADAR_PT
    DrawTriangle((Vector2){ (float)cx, cy - 7.0f }, (Vector2){ cx - 5.0f, cy + 5.0f }, (Vector2){ cx + 5.0f, cy + 5.0f }, WHITE);
}

static void DrawHUD(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int cx = sw/2, cy = sh/2;
    char buf[128];

    // Vignette de degats
    if (P.hurtT > 0 || P.hp < 35)
    {
        float a = fmaxf(P.hurtT*0.6f, P.hp < 35 ? (0.25f + 0.1f*sinf(G.time*5.0f))*(1.0f - P.hp/35.0f) : 0.0f);
        Color red = { 200, 0, 0, (unsigned char)(Clamp(a, 0, 1)*255) }, clear = { 200, 0, 0, 0 };
        int b = sh/4;
        DrawRectangleGradientV(0, 0, sw, b, red, clear);
        DrawRectangleGradientV(0, sh - b, sw, b, clear, red);
        DrawRectangleGradientH(0, 0, b, sh, red, clear);
        DrawRectangleGradientH(sw - b, 0, b, sh, clear, red);
    }

    // Reticule dynamique
    if (W.ads < 0.6f && W.sprintT < 0.5f)
    {
        float hs = sqrtf(P.vel.x*P.vel.x + P.vel.z*P.vel.z);
        float spread = 0.022f + hs*0.003f + (P.grounded ? 0 : 0.04f) + W.bloom;
        int gap = (int)(6 + spread*350), len = 9;
        Color c = Fade(WHITE, 1.0f - W.ads*1.6f);
        DrawRectangle(cx - gap - len, cy - 1, len, 2, c);
        DrawRectangle(cx + gap, cy - 1, len, 2, c);
        DrawRectangle(cx - 1, cy - gap - len, 2, len, c);
        DrawRectangle(cx - 1, cy + gap, 2, len, c);
        DrawRectangle(cx - 1, cy - 1, 2, 2, c);
    }
    // Hitmarker
    if (G.hitT > 0)
    {
        Color c = G.hitKill ? RED : (G.hitHead ? ORANGE : WHITE);
        c.a = (unsigned char)(255*fminf(1, G.hitT/0.1f));
        int a = 6, b = 14;
        DrawLineEx((Vector2){ (float)cx - b, (float)cy - b }, (Vector2){ (float)cx - a, (float)cy - a }, 2.5f, c);
        DrawLineEx((Vector2){ (float)cx + b, (float)cy - b }, (Vector2){ (float)cx + a, (float)cy - a }, 2.5f, c);
        DrawLineEx((Vector2){ (float)cx - b, (float)cy + b }, (Vector2){ (float)cx - a, (float)cy + a }, 2.5f, c);
        DrawLineEx((Vector2){ (float)cx + b, (float)cy + b }, (Vector2){ (float)cx + a, (float)cy + a }, 2.5f, c);
    }

    // Vie
    int bx = 30, by = sh - 60;
    DrawRectangle(bx - 4, by - 26, 268, 56, (Color){ 0, 0, 0, 110 });
    TextShadow("SANTE", bx, by - 22, 16, RAYWHITE);
    DrawRectangle(bx, by, 260, 20, (Color){ 40, 40, 40, 200 });
    Color hc = P.hp > 50 ? (Color){ 90, 220, 90, 255 } : (P.hp > 25 ? (Color){ 240, 190, 40, 255 } : (Color){ 230, 50, 40, 255 });
    DrawRectangle(bx, by, (int)(260*P.hp/100.0f), 20, hc);
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(P.hp));
    TextShadow(buf, bx + 220, by - 22, 18, RAYWHITE);

    // Munitions
    int ax = sw - 260, ay = sh - 86;
    DrawRectangle(ax - 10, ay - 6, 250, 82, (Color){ 0, 0, 0, 110 });
    snprintf(buf, sizeof(buf), "%d", W.ammo);
    TextShadow(buf, ax, ay, 56, W.ammo <= 8 ? (Color){ 255, 90, 70, 255 } : RAYWHITE);
    snprintf(buf, sizeof(buf), "/ %d", W.reserve);
    TextShadow(buf, ax + MeasureText(W.ammo >= 10 ? "00" : "0", 56) + 12, ay + 26, 26, LIGHTGRAY);
    TextShadow("HK416", ax + 150, ay + 4, 20, (Color){ 230, 200, 120, 255 });
    TextShadow(W.semi ? "SEMI" : "AUTO", ax + 150, ay + 28, 16, LIGHTGRAY);
    for (int i = 0; i < MAG_SIZE; i++)
    {
        Color c = (i < W.ammo) ? (Color){ 230, 200, 120, 255 } : (Color){ 80, 80, 80, 200 };
        DrawRectangle(ax + i*7, ay + 62, 5, 9, c);
    }
    if (W.reloading)
    {
        TextCenter("RECHARGEMENT...", cx, cy + 50, 20, RAYWHITE);
        DrawRectangle(cx - 80, cy + 76, 160, 6, (Color){ 0, 0, 0, 150 });
        DrawRectangle(cx - 80, cy + 76, (int)(160*W.reloadT/W.reloadDur), 6, (Color){ 230, 200, 120, 255 });
    }
    else if (W.ammo == 0 && W.reserve == 0) TextCenter("PLUS DE MUNITIONS !", cx, cy + 50, 22, RED);
    else if (W.ammo <= 5) TextCenter("[R] RECHARGER", cx, cy + 50, 20, (Color){ 255, 200, 80, 255 });

    // Burgers restants
    int n = G.burgers;
    int bw = MAX_BURGERS*30;
    DrawRectangle(cx - bw/2 - 12, 8, bw + 24, 62, (Color){ 0, 0, 0, 110 });
    TextCenter("BURGERS A PROTEGER", cx, 12, 16, n <= 3 ? (Color){ 255, 80, 60, 255 } : RAYWHITE);
    for (int i = 0; i < MAX_BURGERS; i++) DrawBurgerIcon(cx - bw/2 + 15 + i*30, 34, 1.0f, i < n);

    // Vague / score
    DrawRadar(95, 105, 75);
    snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
    TextShadow(buf, 20, 195, 30, (Color){ 255, 210, 80, 255 });
    snprintf(buf, sizeof(buf), "Ennemis : %d", G.alive + G.toSpawn + (G.bossPending ? 1 : 0));
    TextShadow(buf, 20, 228, 18, RAYWHITE);
    snprintf(buf, sizeof(buf), "SCORE  %d", G.score);
    TextShadow(buf, sw - 20 - MeasureText(buf, 28), 16, 28, RAYWHITE);
    snprintf(buf, sizeof(buf), "Eliminations %d  |  Headshots %d", G.kills, G.headshots);
    TextShadow(buf, sw - 20 - MeasureText(buf, 16), 50, 16, LIGHTGRAY);

    // Fil d'actualite
    for (int i = 0; i < MAX_MSGS; i++)
    {
        if (msgs[i].t <= 0) continue;
        Color c = msgs[i].c;
        c.a = (unsigned char)(255*fminf(1.0f, msgs[i].t));
        TextShadow(msgs[i].text, sw - 20 - MeasureText(msgs[i].text, 18), 80 + i*24, 18, c);
    }

    // Annonces de vague
    if (G.bannerT > 0)
    {
        float a = fminf(1.0f, G.bannerT);
        snprintf(buf, sizeof(buf), "VAGUE %d", G.wave);
        TextCenter(buf, cx, sh/4, 64, Fade((Color){ 255, 210, 80, 255 }, a));
        TextCenter(G.wave % 5 == 0 ? "ALERTE : LE ROI DU GRAS ARRIVE !" : "Les gloutons ont faim...", cx, sh/4 + 70, 24, Fade(RAYWHITE, a));
    }
    if (G.inIntermission)
    {
        snprintf(buf, sizeof(buf), "VAGUE %d TERMINEE", G.wave);
        TextCenter(buf, cx, sh/4, 52, (Color){ 120, 255, 120, 255 });
        TextCenter("+2 burgers  +90 munitions  +25 PV", cx, sh/4 + 60, 22, RAYWHITE);
        snprintf(buf, sizeof(buf), "Prochaine vague dans %d...", (int)ceilf(G.interT));
        TextCenter(buf, cx, sh/4 + 92, 22, LIGHTGRAY);
    }
}

//----------------------------------------------------------------------------------
// Rendu 3D complet
//----------------------------------------------------------------------------------
static void SetupWorldUniforms(Camera3D *c, float fogDensity, Vector3 flashPos, float flashPower)
{
    SetU3(locViewPos, c->position);
    SetU3(locLightDir, sunDir);
    Vector4 fog = ColorVec(SKY_HORIZON);
    SetShaderValue(shader, locFogColor, &fog, SHADER_UNIFORM_VEC4);
    SetU1(locFogDensity, fogDensity);
    SetU3(locFlashPos, flashPos);
    SetU1(locFlashPower, flashPower);
    SetU1(locEmissive, 0.0f);
    SetU1(locChecker, 0.0f);
    SetU1(locArena, ARENA);
}

static void RenderScene(bool withWeapon)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangleGradientV(0, 0, sw, sh/2 + 40, SKY_TOP, SKY_HORIZON);
    DrawRectangle(0, sh/2 + 40, sw, sh/2, SKY_HORIZON);

    Vector3 flashW = withWeapon ? ViewToWorld(MuzzleView()) : V3(0, -100, 0);
    float flashP = (withWeapon && W.muzzleT > 0) ? 6.0f : 0.0f;

    BeginMode3D(cam);
        SetupWorldUniforms(&cam, 0.018f, flashW, flashP);
        DrawWorld();
        DrawEffects();
    EndMode3D();

    if (!withWeapon) return;

    // Arme dans sa propre passe (espace vue), depth buffer vide
    glClear(GL_DEPTH_BUFFER_BIT);
    Camera3D vc = { 0 };
    vc.position = V3(0, 0, 0);
    vc.target = V3(0, 0, -1);
    vc.up = V3(0, 1, 0);
    vc.fovy = Lerp(55.0f, 40.0f, W.ads);
    vc.projection = CAMERA_PERSPECTIVE;

    // Direction du soleil exprimee en espace vue
    Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    Vector3 ld = { view.m0*sunDir.x + view.m4*sunDir.y + view.m8*sunDir.z,
                   view.m1*sunDir.x + view.m5*sunDir.y + view.m9*sunDir.z,
                   view.m2*sunDir.x + view.m6*sunDir.y + view.m10*sunDir.z };
    BeginMode3D(vc);
        SetU3(locViewPos, vc.position);
        SetU3(locLightDir, Vector3Normalize(ld));
        SetU1(locFogDensity, 0.0f);
        SetU3(locFlashPos, MuzzleView());
        SetU1(locFlashPower, W.muzzleT > 0 ? 0.25f : 0.0f);
        DrawWeapon();
    EndMode3D();
}

static void UpdateCamera3D(float dt)
{
    Vector3 eye = EyePos();
    if (P.grounded)
    {
        float hs = sqrtf(P.vel.x*P.vel.x + P.vel.z*P.vel.z);
        eye.y += sinf(P.bob*2.0f)*0.03f*fminf(hs/WALK_SPEED, 1.4f)*(1.0f - W.ads*0.8f);
    }
    Vector3 f = CamForward();
    Vector3 shake = V3(Randf(-1, 1)*P.shake*0.05f, Randf(-1, 1)*P.shake*0.05f, Randf(-1, 1)*P.shake*0.05f);
    cam.position = eye;
    cam.target = Vector3Add(Vector3Add(eye, f), shake);
    cam.up = V3(0, 1, 0);
    float want = Lerp(FOV_HIP, FOV_ADS, W.ads);
    want = Lerp(want, FOV_SPRINT, W.sprintT);
    cam.fovy += (want - cam.fovy)*fminf(1.0f, dt*14.0f);
    cam.projection = CAMERA_PERSPECTIVE;
}

//----------------------------------------------------------------------------------
// Programme principal
//----------------------------------------------------------------------------------
int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "BURGER RUSH - Operation HK416");
    SetExitKey(KEY_NULL);
    SetTargetFPS(144);
    InitAudioDevice();

    shader = LoadShaderFromMemory(VS_CODE, FS_CODE);
    locViewPos = GetShaderLocation(shader, "viewPos");
    locLightDir = GetShaderLocation(shader, "lightDir");
    locFogColor = GetShaderLocation(shader, "fogColor");
    locFogDensity = GetShaderLocation(shader, "fogDensity");
    locFlashPos = GetShaderLocation(shader, "flashPos");
    locFlashPower = GetShaderLocation(shader, "flashPower");
    locEmissive = GetShaderLocation(shader, "emissive");
    locChecker = GetShaderLocation(shader, "checker");
    locArena = GetShaderLocation(shader, "arena");
    sunDir = Vector3Normalize(V3(-0.45f, -1.0f, -0.3f));

    mCube = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    mSphere = LoadModelFromMesh(GenMeshSphere(1, 12, 16));
    mCyl = LoadModelFromMesh(GenMeshCylinder(1, 1, 14));
    mCube.materials[0].shader = shader;
    mSphere.materials[0].shader = shader;
    mCyl.materials[0].shader = shader;

    // Sans peripherique audio, le jeu tourne simplement en silence
    bool audio = IsAudioDeviceReady();
    if (audio)
    {
        for (int i = 0; i < SND_COUNT; i++) snd[i] = GenSound((SndId)i);
        audio = IsSoundValid(snd[SND_SHOT]);
        if (audio) for (int i = 0; i < 8; i++) shotAlias[i] = LoadSoundAlias(snd[SND_SHOT]);
    }

    BuildMap();
    G.state = ST_TITLE;
    G.burgers = START_BURGERS;
    cam.fovy = FOV_HIP;
    bool quit = false;

    while (!WindowShouldClose() && !quit)
    {
        float dt = fminf(GetFrameTime(), 0.05f);
        G.time += dt;

        switch (G.state)
        {
            case ST_TITLE:
            {
                G.titleT += dt;
                float a = G.titleT*0.15f;
                cam.position = V3(sinf(a)*16.0f, 7.0f, cosf(a)*16.0f);
                cam.target = V3(0, 2.0f, 0);
                cam.up = V3(0, 1, 0);
                cam.fovy = 60.0f;
                cam.projection = CAMERA_PERSPECTIVE;
                if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    ResetGame();
                    cam.fovy = FOV_HIP;
                    DisableCursor();
                }
                if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            } break;

            case ST_PLAY:
            {
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) { G.state = ST_PAUSE; EnableCursor(); break; }
                bool sprinting = false;
                UpdatePlayer(dt, &sprinting);
                bool fireDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
                UpdateWeapon(dt, fireDown, fireDown, sprinting);
                UpdateEnemies(dt);
                UpdateWaves(dt);
                UpdateEffects(dt);
                UpdateCamera3D(dt);
                G.hitT -= dt;
                G.bannerT -= dt;
                if (G.state == ST_OVER)
                {
                    if (G.score > G.best) G.best = G.score;
                    EnableCursor();
                }
            } break;

            case ST_PAUSE:
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { G.state = ST_PLAY; DisableCursor(); }
                if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A)) { G.state = ST_TITLE; G.titleT = 0; }
                break;

            case ST_OVER:
                UpdateEffects(dt);
                for (int i = 0; i < MAX_ENEMIES; i++)
                    if (enemies[i].active && enemies[i].state == ES_WALK) enemies[i].phase += dt*3.0f;
                if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER)) { ResetGame(); DisableCursor(); }
                if (IsKeyPressed(KEY_ESCAPE)) { G.state = ST_TITLE; G.titleT = 0; }
                break;
        }

        BeginDrawing();
            ClearBackground(SKY_HORIZON);
            int sw = GetScreenWidth(), sh = GetScreenHeight();
            int cx = sw/2;
            char buf[128];

            if (G.state == ST_TITLE)
            {
                RenderScene(false);
                DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 110 });
                TextCenter("BURGER RUSH", cx, sh/5, 90, (Color){ 255, 200, 60, 255 });
                TextCenter("OPERATION HK416", cx, sh/5 + 95, 36, RAYWHITE);
                TextCenter("Des hordes de gloutons veulent devorer votre stand de burgers.", cx, sh/5 + 160, 22, LIGHTGRAY);
                TextCenter("Defendez-le avec votre HK416. Ne les laissez pas tout manger !", cx, sh/5 + 188, 22, LIGHTGRAY);
                const char *ctrl[] = {
                    "ZQSD / WASD : se deplacer      SOURIS : viser",
                    "CLIC GAUCHE : tirer      CLIC DROIT : viseur point rouge",
                    "R : recharger      B : mode de tir      MAJ : sprint      ESPACE : sauter",
                    "ECHAP : pause",
                };
                for (int i = 0; i < 4; i++) TextCenter(ctrl[i], cx, sh/5 + 250 + i*28, 20, RAYWHITE);
                if (fmodf(G.time, 1.0f) < 0.65f) TextCenter("Appuyez sur ENTREE pour commencer", cx, sh - 110, 30, (Color){ 255, 220, 120, 255 });
                if (G.best > 0) { snprintf(buf, sizeof(buf), "Meilleur score : %d", G.best); TextCenter(buf, cx, sh - 60, 22, LIGHTGRAY); }
            }
            else
            {
                RenderScene(G.state != ST_OVER);
                if (G.state == ST_PLAY || G.state == ST_PAUSE) DrawHUD();
                if (G.state == ST_PAUSE)
                {
                    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 150 });
                    TextCenter("PAUSE", cx, sh/3, 70, RAYWHITE);
                    TextCenter("ECHAP / clic : reprendre      Q : menu principal", cx, sh/3 + 90, 24, LIGHTGRAY);
                }
                if (G.state == ST_OVER)
                {
                    DrawRectangle(0, 0, sw, sh, (Color){ 60, 0, 0, 150 });
                    TextCenter("GAME OVER", cx, sh/5, 90, (Color){ 255, 70, 50, 255 });
                    TextCenter(G.overReason, cx, sh/5 + 100, 28, RAYWHITE);
                    snprintf(buf, sizeof(buf), "Vague atteinte : %d      Score : %d", G.wave, G.score);
                    TextCenter(buf, cx, sh/5 + 160, 30, (Color){ 255, 210, 80, 255 });
                    snprintf(buf, sizeof(buf), "Eliminations : %d      Headshots : %d      Meilleur score : %d", G.kills, G.headshots, G.best);
                    TextCenter(buf, cx, sh/5 + 200, 22, LIGHTGRAY);
                    TextCenter("R / ENTREE : rejouer      ECHAP : menu", cx, sh/5 + 270, 24, RAYWHITE);
                }
            }
        EndDrawing();
    }

    if (audio)
    {
        for (int i = 0; i < 8; i++) UnloadSoundAlias(shotAlias[i]);
        for (int i = 0; i < SND_COUNT; i++) UnloadSound(snd[i]);
    }
    // Le shader est partage : on le detache des materiaux avant de liberer les modeles
    Shader def = { rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
    mCube.materials[0].shader = mSphere.materials[0].shader = mCyl.materials[0].shader = def;
    UnloadModel(mCube);
    UnloadModel(mSphere);
    UnloadModel(mCyl);
    UnloadShader(shader);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
