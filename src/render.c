/*
 * render.c - Shader (lambert + brouillard + eclair du canon) et primitives.
 */
#include "render.h"
#include "config.h"
#include "util.h"

#include "rlgl.h"

#include <math.h>

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

void RenderInit(void)
{
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
}

void RenderSetEmissive(bool on) { SetU1(locEmissive, on ? 1.0f : 0.0f); }
void RenderSetChecker(bool on) { SetU1(locChecker, on ? 1.0f : 0.0f); }

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
static void Part(Model *m, Matrix world, Color c)
{
    m->transform = world;
    DrawModel(*m, (Vector3){ 0 }, 1.0f, c);
}

Matrix TRS(Vector3 t, Vector3 r, Vector3 s)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateXYZ(r)), MatrixTranslate(t.x, t.y, t.z));
}

void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c) { Part(&mCube, MatrixMultiply(TRS(t, r, s), parent), c); }

void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c) { Part(&mSphere, MatrixMultiply(TRS(t, NOROT, radii), parent), c); }

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
    Part(&mCyl, MatrixMultiply(m, parent), c);
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

void DrawBurger(Matrix parent)
{
    const Color bun = { 214, 150, 62, 255 };
    Seg(parent, V3(0, 0, 0), V3(0, 0.05f, 0), 0.15f, Shade(bun, 0.9f));                                // pain du bas
    Seg(parent, V3(0, 0.05f, 0), V3(0, 0.095f, 0), 0.16f, (Color){ 92, 50, 26, 255 });                   // steak
    Box3(parent, V3(0, 0.1f, 0), V3(0, PI/4, 0), V3(0.27f, 0.012f, 0.27f), (Color){ 250, 200, 40, 255 }); // cheddar
    Seg(parent, V3(0, 0.1f, 0), V3(0, 0.115f, 0), 0.168f, (Color){ 90, 175, 60, 255 });                  // salade
    Seg(parent, V3(0, 0.115f, 0), V3(0, 0.13f, 0), 0.14f, (Color){ 210, 50, 40, 255 });                  // tomate
    Ball(parent, V3(0, 0.13f, 0), V3(0.155f, 0.09f, 0.155f), bun);                                       // pain du haut
    for (int i = 0; i < 6; i++)                                                                          // sesame
    {
        float a = i*1.1f;
        float r = 0.05f + 0.04f*(i % 2);
        Ball(parent, V3(cosf(a)*r, 0.13f + 0.085f - r*0.35f, sinf(a)*r), V3(0.012f, 0.007f, 0.018f), (Color){ 250, 240, 210, 255 });
    }
}
