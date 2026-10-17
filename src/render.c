/*
 * render.c - Shader (lambert + brouillard + eclair du canon + taches sur le
 * tissu) et primitives.
 */
#include "render.h"
#include "config.h"
#include "util.h"

#include "rlgl.h"

#include <math.h>
#include <stdio.h>

// glClear vient directement de la lib OpenGL (deja liee par raylib) : on
// s'en sert pour vider le depth buffer avant de dessiner l'arme, afin
// qu'elle ne rentre jamais dans les murs.
#if defined(_WIN32)
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif
extern void GL_APIENTRY glClear(unsigned int mask);
#define GL_DEPTH_BUFFER_BIT 0x00000100

const Color SKY_TOP = { 92, 148, 214, 255 };
const Color SKY_HORIZON = { 196, 214, 232, 255 };

static Model mCube, mSphere, mCyl;
static Shader shader;
static Vector3 sunDir;
static int locViewPos, locLightDir, locFogColor, locFogDensity, locFlashPos, locFlashPower;
static int locEmissive, locChecker, locArena;
static int locCamo, locCamoSpace, locCamoA, locCamoB, locCamoC;

// Les shaders sont ecrits une fois, avec un en-tete different selon l'API :
// OpenGL 3.3 (PC / Mac) ou OpenGL ES 2.0 (Android / iOS).
static const char *VS_HEADER_GL33 = "#version 330\n#define ATTR in\n#define VARY out\n";
static const char *VS_HEADER_ES2 = "#version 100\n#define ATTR attribute\n#define VARY varying\n";
static const char *FS_HEADER_GL33 = "#version 330\n#define VARY in\nout vec4 fragOut;\n#define FRAG_OUT fragOut\n";
static const char *FS_HEADER_ES2 =
    "#version 100\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "#define VARY varying\n#define FRAG_OUT gl_FragColor\n";

static const char *VS_BODY =
    "ATTR vec3 vertexPosition;\n"
    "ATTR vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "uniform mat4 camoSpace;\n"
    "VARY vec3 fragPosition;\n"
    "VARY vec3 fragNormal;\n"
    "VARY vec3 camoPos;\n"
    "void main() {\n"
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));\n"
    "    camoPos = vec3(camoSpace*vec4(fragPosition, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

// Treillis : les taches (usure, boue) sont un bruit 3D calcule dans le repere du
// corps (camoSpace), il suit donc le personnage sans "glisser".
static const char *FS_BODY =
    "VARY vec3 fragPosition;\n"
    "VARY vec3 fragNormal;\n"
    "VARY vec3 camoPos;\n"
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
    "uniform float camo;\n"
    "uniform vec3 camoA;\n"
    "uniform vec3 camoB;\n"
    "uniform vec3 camoC;\n"
    "float hash3(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719)))*43758.5453); }\n"
    "float custom_noise3(vec3 p) {\n"
    "    vec3 i = floor(p);\n"
    "    vec3 f = fract(p);\n"
    "    f = f*f*(3.0 - 2.0*f);\n"
    "    float a = mix(hash3(i), hash3(i + vec3(1.0, 0.0, 0.0)), f.x);\n"
    "    float b = mix(hash3(i + vec3(0.0, 1.0, 0.0)), hash3(i + vec3(1.0, 1.0, 0.0)), f.x);\n"
    "    float c = mix(hash3(i + vec3(0.0, 0.0, 1.0)), hash3(i + vec3(1.0, 0.0, 1.0)), f.x);\n"
    "    float d = mix(hash3(i + vec3(0.0, 1.0, 1.0)), hash3(i + vec3(1.0, 1.0, 1.0)), f.x);\n"
    "    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);\n"
    "}\n"
    "float camoField(vec3 p) { return custom_noise3(p*2.6)*0.65 + custom_noise3(p*5.5 + 3.7)*0.35; }\n"
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
    "    if (camo > 0.5) {\n"
    "        float n = camoField(camoPos);\n"
    "        float m = camoField(camoPos.zxy + vec3(7.3, 1.9, 4.1));\n"
    "        if (n > 0.6) base = camoA;\n"
    "        else if (n < 0.38) base = camoB;\n"
    "        if (m > 0.66) base = camoC;\n"
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
    "    FRAG_OUT = vec4(col, colDiffuse.a);\n"
    "}\n";

static void SetU1(int loc, float v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetU3(int loc, Vector3 v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_VEC3); }

void RenderInit(void)
{
    bool es = (rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30);
    static char vs[4096], fs[8192];
    snprintf(vs, sizeof(vs), "%s%s", es ? VS_HEADER_ES2 : VS_HEADER_GL33, VS_BODY);
    snprintf(fs, sizeof(fs), "%s%s", es ? FS_HEADER_ES2 : FS_HEADER_GL33, FS_BODY);
    shader = LoadShaderFromMemory(vs, fs);
    locViewPos = GetShaderLocation(shader, "viewPos");
    locLightDir = GetShaderLocation(shader, "lightDir");
    locFogColor = GetShaderLocation(shader, "fogColor");
    locFogDensity = GetShaderLocation(shader, "fogDensity");
    locFlashPos = GetShaderLocation(shader, "flashPos");
    locFlashPower = GetShaderLocation(shader, "flashPower");
    locEmissive = GetShaderLocation(shader, "emissive");
    locChecker = GetShaderLocation(shader, "checker");
    locArena = GetShaderLocation(shader, "arena");
    locCamo = GetShaderLocation(shader, "camo");
    locCamoSpace = GetShaderLocation(shader, "camoSpace");
    locCamoA = GetShaderLocation(shader, "camoA");
    locCamoB = GetShaderLocation(shader, "camoB");
    locCamoC = GetShaderLocation(shader, "camoC");
    sunDir = Vector3Normalize(V3(-0.45f, -1.0f, -0.3f));

    mCube = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    mSphere = LoadModelFromMesh(GenMeshSphere(1, 12, 16));
    mCyl = LoadModelFromMesh(GenMeshCylinder(1, 1, 14));
    mCube.materials[0].shader = shader;
    mSphere.materials[0].shader = shader;
    mCyl.materials[0].shader = shader;
}

void RenderClose(void)
{
    // Le shader est partage : on le detache des materiaux avant de liberer les modeles
    Shader def = { rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
    mCube.materials[0].shader = mSphere.materials[0].shader = mCyl.materials[0].shader = def;
    UnloadModel(mCube);
    UnloadModel(mSphere);
    UnloadModel(mCyl);
    UnloadShader(shader);
}

Model RenderLoadModel(Mesh mesh)
{
    UploadMesh(&mesh, false);
    Model m = LoadModelFromMesh(mesh);
    m.materials[0].shader = shader;
    return m;
}

void RenderUnloadModel(Model m)
{
    m.materials[0].shader = (Shader){ rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
    UnloadModel(m);
}

Vector3 RenderSunDir(void) { return sunDir; }

void RenderSetFrame(Vector3 viewPos, Vector3 lightDir, float fogDensity, Vector3 flashPos, float flashPower)
{
    SetU3(locViewPos, viewPos);
    SetU3(locLightDir, lightDir);
    Vector4 fog = ColorVec(SKY_HORIZON);
    SetShaderValue(shader, locFogColor, &fog, SHADER_UNIFORM_VEC4);
    SetU1(locFogDensity, fogDensity);
    SetU3(locFlashPos, flashPos);
    SetU1(locFlashPower, flashPower);
    SetU1(locEmissive, 0.0f);
    SetU1(locChecker, 0.0f);
    SetU1(locArena, ARENA);
    SetU1(locCamo, 0.0f);
}

void RenderSetEmissive(bool on) { SetU1(locEmissive, on ? 1.0f : 0.0f); }
void RenderSetChecker(bool on) { SetU1(locChecker, on ? 1.0f : 0.0f); }

static Vector3 ColorRGB(Color c) { return V3(c.r/255.0f, c.g/255.0f, c.b/255.0f); }

void RenderSetCamo(bool on, Matrix body, Color a, Color b, Color c)
{
    SetU1(locCamo, on ? 1.0f : 0.0f);
    if (!on) return;
    SetShaderValueMatrix(shader, locCamoSpace, MatrixInvert(body));
    SetU3(locCamoA, ColorRGB(a));
    SetU3(locCamoB, ColorRGB(b));
    SetU3(locCamoC, ColorRGB(c));
}

void RenderClearDepth(void)
{
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);
}

void DrawSky(int width, int height)
{
    DrawRectangleGradientV(0, 0, width, height/2 + 40, SKY_TOP, SKY_HORIZON);
    DrawRectangle(0, height/2 + 40, width, height/2, SKY_HORIZON);
}

//----------------------------------------------------------------------------------
// Primitives
//----------------------------------------------------------------------------------
void DrawPart(Model *m, Matrix world, Color c)
{
    m->transform = world;
    DrawModel(*m, (Vector3){ 0 }, 1.0f, c);
}

Matrix TRS(Vector3 t, Vector3 r, Vector3 s)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateXYZ(r)), MatrixTranslate(t.x, t.y, t.z));
}

void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c) { DrawPart(&mCube, MatrixMultiply(TRS(t, r, s), parent), c); }

void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c) { DrawPart(&mSphere, MatrixMultiply(TRS(t, NOROT, radii), parent), c); }

void BallAxes(Matrix parent, Vector3 t, Vector3 x, Vector3 y, Vector3 radii, Color c)
{
    Vector3 z = Vector3CrossProduct(x, y);
    Matrix frame = { x.x, y.x, z.x, t.x,
                     x.y, y.y, z.y, t.y,
                     x.z, y.z, z.z, t.z,
                     0, 0, 0, 1 };
    DrawPart(&mSphere, MatrixMultiply(MatrixMultiply(MatrixScale(radii.x, radii.y, radii.z), frame), parent), c);
}

void Seg(Matrix parent, Vector3 a, Vector3 b, float radius, Color c)
{
    // Le cylindre est symetrique : on l'oriente toujours vers le haut pour
    // eviter le cas degenere d'une rotation de 180 degres.
    if (b.y < a.y) { Vector3 tmp = a; a = b; b = tmp; }
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-5f) return;
    Quaternion q = QuaternionFromVector3ToVector3(V3(0, 1, 0), Vector3Scale(d, 1.0f/len));
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(radius, len, radius), QuaternionToMatrix(q)), MatrixTranslate(a.x, a.y, a.z));
    DrawPart(&mCyl, MatrixMultiply(m, parent), c);
}

void Limb(Matrix parent, Vector3 a, Vector3 b, float radius, Color c)
{
    Seg(parent, a, b, radius, c);
    Ball(parent, a, V3(radius, radius, radius), c);
    Ball(parent, b, V3(radius, radius, radius), c);
}

void DrawBlobShadow(Vector3 p, float radius)
{
    Seg(MatrixIdentity(), V3(p.x, 0.01f, p.z), V3(p.x, 0.012f, p.z), radius, (Color){ 0, 0, 0, 90 });
}

void DrawChick(Matrix parent, float t, float flap)
{
    const Color yellow = { 255, 214, 50, 255 }, wing = { 240, 186, 30, 255 }, orange = { 255, 135, 25, 255 };
    // Picore de temps en temps
    float peck = powf(fmaxf(0.0f, sinf(t*1.7f)), 8.0f);
    Vector3 head = V3(0, 0.19f - 0.05f*peck, 0.06f + 0.04f*peck);

    Ball(parent, V3(0, 0.09f, 0), V3(0.1f, 0.085f, 0.11f), yellow);
    Ball(parent, V3(0, 0.12f, -0.1f), V3(0.04f, 0.03f, 0.04f), yellow);   // queue
    Ball(parent, head, V3(0.07f, 0.068f, 0.07f), yellow);
    Ball(parent, Vector3Add(head, V3(0, 0.07f, -0.01f)), V3(0.012f, 0.03f, 0.012f), yellow);   // houppette
    Box3(parent, Vector3Add(head, V3(0, -0.005f, 0.075f)), V3(0, PI/4, 0), V3(0.035f, 0.02f, 0.035f), orange);   // bec
    for (int k = -1; k <= 1; k += 2)
    {
        Ball(parent, Vector3Add(head, V3(0.04f*k, 0.015f, 0.055f)), V3(0.013f, 0.016f, 0.01f), BLACK);
        // Ailes : battent quand on l'attrape
        float a = 0.15f + flap*(0.5f + 0.5f*sinf(t*38.0f))*1.1f;
        Matrix wm = MatrixMultiply(MatrixMultiply(MatrixRotateZ(k*a), MatrixTranslate(0.09f*k, 0.13f, 0)), parent);
        Ball(wm, V3(0, -0.035f, -0.01f), V3(0.025f, 0.05f, 0.07f), wing);
        Seg(parent, V3(0.035f*k, 0.0f, 0.02f), V3(0.035f*k, 0.04f, 0.0f), 0.008f, orange);   // pattes
        Box3(parent, V3(0.035f*k, 0.005f, 0.035f), NOROT, V3(0.03f, 0.01f, 0.05f), orange);
    }
}
