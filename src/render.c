/*
 * render.c - Shader (lambert + brouillard + eclair du canon + taches sur le
 * tissu) et primitives.
 *
 * Principe : un seul shader "maison" est utilise pour tout le monde 3D. Les
 * effets (sol carrele, couleur emissive, camouflage) sont actives par des
 * uniformes (variables envoyees au GPU) plutot que par plusieurs shaders,
 * ce qui reste simple et compatible avec les telephones (OpenGL ES 2).
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
// GL_APIENTRY : convention d'appel des fonctions OpenGL (stdcall sous Windows).
#if defined(_WIN32)
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif
extern void GL_APIENTRY glClear(unsigned int mask);
#define GL_DEPTH_BUFFER_BIT 0x00000100   // valeur officielle de la constante OpenGL

// Ciel de nuit : bleu nuit presque noir en haut, bleu ardoise a l'horizon
// (c'est aussi la couleur du brouillard, pour que le decor se fonde dans la nuit)
const Color SKY_TOP = { 5, 8, 20, 255 };
const Color SKY_HORIZON = { 22, 30, 50, 255 };

// Lampes qui eclairent la nuit : une sous le toit du stand (au-dessus des
// poussins) et une sur chacun des 4 lampadaires des coins (voir map.c).
#define LAMP_COUNT 5
static const Vector3 LAMPS[LAMP_COUNT] = {
    { 0, 2.85f, 0 }, { 26, 7.8f, 26 }, { -26, 7.8f, 26 }, { 26, 7.8f, -26 }, { -26, 7.8f, -26 }
};

// Les trois meshes de base (cube 1x1x1, sphere de rayon 1, cylindre rayon 1 hauteur 1)
static Model mCube, mSphere, mCyl;
static Shader shader;
static Vector3 sunDir;
// "loc" = emplacement (location) d'un uniforme dans le shader, obtenu une fois
// a l'initialisation puis utilise pour lui envoyer des valeurs.
static int locViewPos, locLightDir, locFogColor, locFogDensity, locFlashPos, locFlashPower;
static int locEmissive, locChecker, locArena;
static int locCamo, locCamoSpace, locCamoA, locCamoB, locCamoC;
static int locLampPos, locLamps;

// Les shaders sont ecrits une fois, avec un en-tete different selon l'API :
// OpenGL 3.3 (PC / Mac) ou OpenGL ES 2.0 (Android / iOS).
// Les macros ATTR / VARY / FRAG_OUT masquent les differences de syntaxe :
// GLSL 330 utilise "in" / "out" et une sortie declaree, GLSL ES 100 utilise
// "attribute" / "varying" et gl_FragColor. En ES 2, il faut aussi choisir
// une precision des flottants (highp si le GPU la supporte, sinon mediump).
static const char *VS_HEADER_GL33 = "#version 330\n#define ATTR in\n#define VARY out\n";
static const char *VS_HEADER_ES2 = "#version 100\n#define ATTR attribute\n#define VARY varying\n";
static const char *FS_HEADER_GL33 = "#version 330\n#define VARY in\nout vec4 fragOut;\n#define FRAG_OUT fragOut\n";
static const char *FS_HEADER_ES2 =
    "#version 100\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "#define VARY varying\n#define FRAG_OUT gl_FragColor\n";

// Vertex shader : execute une fois par sommet.
//  - vertexPosition / vertexNormal : donnees du mesh (repere local du modele)
//  - mvp : matrice Modele * Vue * Projection fournie par raylib (-> position ecran)
//  - matModel : matrice du modele seul (-> position dans le monde)
//  - matNormal : inverse transposee de matModel, pour tourner correctement les
//    normales meme quand le modele est etire de facon non uniforme
//  - camoSpace : passe du monde au repere du corps (pour le camouflage)
// Les "VARY" sont interpolees entre les sommets et transmises au fragment shader.
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
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));\n"   // w = 1 : un point subit la translation
    "    camoPos = vec3(camoSpace*vec4(fragPosition, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));\n"   // w = 0 : une direction ne se translate pas
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

// Treillis : les taches (usure, boue) sont un bruit 3D calcule dans le repere du
// corps (camoSpace), il suit donc le personnage sans "glisser".
//
// Fragment shader : execute pour chaque pixel dessine. Etapes :
//  1. couleur de base = colDiffuse (teinte passee a DrawModel) ;
//  2. si checker : sol carrele dans l'arene, herbe tachetee au-dela ;
//  3. si camo : remplace la couleur par une des nuances selon un bruit 3D ;
//  4. eclairage (sauf si emissive) : ambiant + ciel + soleil (Lambert) + eclair du canon ;
//  5. brouillard exponentiel au carre selon la distance a la camera.
//
// Fonctions de bruit :
//  - hash3 : pseudo-aleatoire classique "fract(sin(dot(...)) * 43758.5453)",
//    donne une valeur entre 0 et 1 differente pour chaque point entier ;
//  - custom_noise3 : "value noise" 3D : on prend les 8 coins entiers du cube qui
//    contient p, et on melange leurs valeurs (mix = interpolation lineaire)
//    avec un poids lisse f*f*(3-2f) (smoothstep) pour eviter les aretes ;
//  - camoField : somme de 2 octaves de bruit (frequences 2.6 et 5.5) pour des
//    taches de tailles variees ; resultat ~ entre 0 et 1.
// Sol (checker) :
//  - hors arene (|x| ou |z| > arena) : herbe verte, luminosite aleatoire par
//    carre de 50 cm (floor(p*2)) ;
//  - dans l'arene : damier de dalles de 2 m (p*0.5), une dalle sur deux un peu
//    plus sombre (0.86), et joints (grout) assombris pres des bords de dalle
//    (quand on est a plus de 0.47 du centre, en fraction de dalle).
// Camouflage : n et m sont deux bruits independants (coordonnees permutees et
//    decalees) ; n fort -> camoA, n faible -> camoB, m fort -> camoC (par-dessus).
// Eclairage :
//  - diff = Lambert : max(N . L, 0), L = -lightDir (vers le soleil) ;
//  - sky = lumiere du ciel (hemisphere) : 1 pour une face tournee vers le
//    haut, 0 vers le bas ; ambiance totale = 0.30 + 0.25*sky ;
//  - eclair du canon : lumiere ponctuelle orangee en flashPos, Lambert divise
//    par une attenuation 1 + 0.35*d^2 (decroit avec le carre de la distance).
// Brouillard : fog = 1 - exp(-(d*density)^2), 0 pres de la camera, tend vers 1
//    au loin ; la couleur finale est melangee vers fogColor de cette quantite.
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
    "uniform float emissive;\n"   // booleens passes en float (> 0.5 = vrai), plus portable en GLSL ES
    "uniform float checker;\n"
    "uniform float arena;\n"
    "uniform float camo;\n"
    "uniform vec3 lampPos[5];\n"   // lampes de la nuit (positions monde)
    "uniform float lamps;\n"       // 1 = lampes allumees, 0 = eteintes (passe de l'arme)
    "uniform vec3 camoA;\n"
    "uniform vec3 camoB;\n"
    "uniform vec3 camoC;\n"
    "float hash3(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719)))*43758.5453); }\n"
    "float custom_noise3(vec3 p) {\n"   // pas "noise3" : nom reserve en GLSL (refuse par certains pilotes)
    "    vec3 i = floor(p);\n"            // coin entier du cube
    "    vec3 f = fract(p);\n"            // position dans le cube (0..1)
    "    f = f*f*(3.0 - 2.0*f);\n"        // lissage smoothstep
    "    float a = mix(hash3(i), hash3(i + vec3(1.0, 0.0, 0.0)), f.x);\n"   // 4 interpolations sur x...
    "    float b = mix(hash3(i + vec3(0.0, 1.0, 0.0)), hash3(i + vec3(1.0, 1.0, 0.0)), f.x);\n"
    "    float c = mix(hash3(i + vec3(0.0, 0.0, 1.0)), hash3(i + vec3(1.0, 0.0, 1.0)), f.x);\n"
    "    float d = mix(hash3(i + vec3(0.0, 1.0, 1.0)), hash3(i + vec3(1.0, 1.0, 1.0)), f.x);\n"
    "    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);\n"               // ... puis sur y, puis sur z
    "}\n"
    // Cellules de Voronoi : chaque cellule a un point place au hasard ; on
    // renvoie (distance au 2e point - distance au 1er point, hasard de la
    // cellule). Petit ecart = bord entre deux pierres -> joints irreguliers.
    "vec2 hash2(vec2 c) { return fract(sin(vec2(dot(c, vec2(127.1, 311.7)), dot(c, vec2(269.5, 183.3))))*43758.5453); }\n"
    "vec2 stones(vec2 x) {\n"
    "    vec2 n = floor(x);\n"
    "    float d1 = 8.0, d2 = 8.0, id = 0.0;\n"
    "    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {\n"
    "        vec2 g = n + vec2(float(i), float(j));\n"
    "        vec2 h = hash2(g);\n"
    "        float d = length(g + h - x);\n"
    "        if (d < d1) { d2 = d1; d1 = d; id = h.x; } else if (d < d2) d2 = d;\n"
    "    }\n"
    "    return vec2(d2 - d1, id);\n"
    "}\n"
    "float camoField(vec3 p) { return custom_noise3(p*2.6)*0.65 + custom_noise3(p*5.5 + 3.7)*0.35; }\n"
    "void main() {\n"
    "    vec3 base = colDiffuse.rgb;\n"
    // Sol (checker = 1) : gravier sombre dans l'arene (cailloux de ~9 cm de
    // teintes variees, quelques-uns plus clairs, zones plus ou moins
    // tassees), herbe au-dela des murs.
    "    if (checker > 0.5 && checker < 1.5) {\n"
    "        vec2 p = fragPosition.xz;\n"   // le sol est le plan horizontal XZ
    "        if (abs(p.x) > arena + 1.0 || abs(p.y) > arena + 1.0) {\n"
    "            float g = fract(sin(dot(floor(p*2.0), vec2(12.9898, 78.233)))*43758.5453);\n"
    "            base = vec3(0.26, 0.40, 0.18)*(0.85 + 0.25*g);\n"   // vert herbe, +/- clair
    "        } else {\n"
    // Au-dela de 22 m : teinte moyenne (pas de scintillement, et le calcul
    // des cailloux, le plus couteux du shader, n'est fait que pres du joueur)
    "            float detail = clamp(1.0 - length(viewPos - fragPosition)/22.0, 0.0, 1.0);\n"
    "            float tone = 0.21;\n"
    "            if (detail > 0.0) {\n"
    "                vec2 st = stones(p*10.0);\n"                             // ~10 cailloux par metre
    "                float h = fract(st.y*7.13);\n"
    "                float t = (0.15 + 0.2*st.y + (h > 0.9 ? 0.3 : 0.0))*mix(0.45, 1.0, smoothstep(0.02, 0.12, st.x));\n"
    "                tone = mix(0.21, t, detail);\n"
    "            }\n"
    "            base = vec3(tone, tone, tone*1.04);\n"
    "            base *= 0.8 + 0.4*camoField(vec3(p*0.12, 0.0));\n"          // zones plus ou moins sombres
    "        }\n"
    "    }\n"
    // Mur en gabions (checker = 2) : cages de grillage remplies de pierres
    // grises. Pierres en quinconce (rangees decalees d'une demi-pierre),
    // joints sombres entre elles, fils du grillage tous les 50 cm.
    "    if (checker > 1.5) {\n"
    "        vec3 wp = fragPosition;\n"
    "        vec2 st = stones(vec2(wp.x + wp.z, wp.y)*vec2(3.6, 4.4));\n"
    "        float tone = 0.38 + 0.32*st.y;\n"
    "        base = vec3(tone, tone*0.99, tone*0.95)*mix(0.25, 1.0, smoothstep(0.03, 0.16, st.x));\n"
    "        vec2 w = abs(fract(vec2(wp.x + wp.z, wp.y)*2.0) - 0.5);\n"
    "        if (max(w.x, w.y) > 0.475) base = vec3(0.55, 0.56, 0.58);\n"    // fil de fer galvanise
    "    }\n"
    "    if (camo > 0.5) {\n"
    "        float n = camoField(camoPos);\n"
    "        float m = camoField(camoPos.zxy + vec3(7.3, 1.9, 4.1));\n"
    "        if (n > 0.6) base = camoA;\n"
    "        else if (n < 0.38) base = camoB;\n"
    "        if (m > 0.66) base = camoC;\n"
    "    }\n"
    "    vec3 n = normalize(fragNormal);\n"   // re-normaliser : l'interpolation raccourcit le vecteur
    "    vec3 col;\n"
    "    if (emissive > 0.5) col = base;\n"
    "    else {\n"
    "        float diff = max(dot(n, -lightDir), 0.0);\n"
    "        float sky = 0.5 + 0.5*n.y;\n"
    // Nuit : ambiance faible + clair de lune bleute
    "        col = base*(0.14 + 0.14*sky*vec3(0.55, 0.65, 1.0) + 0.42*diff*vec3(0.65, 0.75, 1.0));\n"
    // Lampes : lumiere jaune orangee qui decroit avec la distance au carre
    // (la premiere, celle du stand, est un peu moins forte)
    "        if (lamps > 0.5) {\n"
    "            for (int i = 0; i < 5; i++) {\n"
    "                vec3 ld = lampPos[i] - fragPosition;\n"
    "                float d = length(ld);\n"
    "                float power = (i == 0) ? 2.4 : 7.0;\n"
    "                float la = max(dot(n, ld/max(d, 0.001)), 0.0)*power/(1.0 + d*d*0.12);\n"
    "                col += base*vec3(1.0, 0.82, 0.55)*la;\n"
    "            }\n"
    "        }\n"
    "        vec3 fl = flashPos - fragPosition;\n"
    "        float fd = length(fl);\n"
    "        float fa = flashPower*max(dot(n, fl/max(fd, 0.001)), 0.0)/(1.0 + fd*fd*0.35);\n"
    "        col += base*vec3(1.0, 0.75, 0.4)*fa;\n"   // lumiere orangee du tir
    "    }\n"
    "    float dist = length(viewPos - fragPosition);\n"
    "    float fog = 1.0 - exp(-pow(dist*fogDensity, 2.0));\n"
    "    col = mix(col, fogColor.rgb, clamp(fog, 0.0, 1.0));\n"
    "    FRAG_OUT = vec4(col, colDiffuse.a);\n"
    "}\n";

// Raccourcis pour envoyer un float ou un vec3 au shader
static void SetU1(int loc, float v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetU3(int loc, Vector3 v) { SetShaderValue(shader, loc, &v, SHADER_UNIFORM_VEC3); }

// Assemble et compile le shader selon l'API graphique detectee, recupere les
// emplacements des uniformes, puis cree les 3 meshes de base qui utiliseront
// tous ce shader. Doit etre appele apres InitWindow (contexte OpenGL requis).
void RenderInit(void)
{
    bool es = (rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30);
    // static : tampons assez grands pour l'en-tete + le corps, pas sur la pile
    static char vs[4096], fs[8192];
    snprintf(vs, sizeof(vs), "%s%s", es ? VS_HEADER_ES2 : VS_HEADER_GL33, VS_BODY);
    snprintf(fs, sizeof(fs), "%s%s", es ? FS_HEADER_ES2 : FS_HEADER_GL33, FS_BODY);
    shader = LoadShaderFromMemory(vs, fs);
    // Les uniformes standard (mvp, matModel, colDiffuse...) sont trouves par
    // raylib tout seul ; on cherche ici seulement nos uniformes personnalises.
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
    locLampPos = GetShaderLocation(shader, "lampPos");
    locLamps = GetShaderLocation(shader, "lamps");
    locCamoSpace = GetShaderLocation(shader, "camoSpace");
    locCamoA = GetShaderLocation(shader, "camoA");
    locCamoB = GetShaderLocation(shader, "camoB");
    locCamoC = GetShaderLocation(shader, "camoC");
    // Soleil haut dans le ciel, legerement de biais (direction de propagation : vers le bas)
    sunDir = Vector3Normalize(V3(-0.45f, -1.0f, -0.3f));

    // Meshes de base : cube de cote 1 centre, sphere de rayon 1 (12 anneaux,
    // 16 tranches), cylindre de rayon 1 et hauteur 1 partant de y = 0 (14 cotes).
    // Peu de polygones : style "low poly" et rapide sur mobile.
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
    // (sinon UnloadModel tenterait de le liberer plusieurs fois).
    Shader def = { rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
    mCube.materials[0].shader = mSphere.materials[0].shader = mCyl.materials[0].shader = def;
    UnloadModel(mCube);
    UnloadModel(mSphere);
    UnloadModel(mCyl);
    UnloadShader(shader);
}

// Envoie un mesh genere sur le GPU (UploadMesh, false = non modifiable) et
// l'enveloppe dans un Model qui utilise notre shader.
Model RenderLoadModel(Mesh mesh)
{
    UploadMesh(&mesh, false);
    Model m = LoadModelFromMesh(mesh);
    m.materials[0].shader = shader;
    return m;
}

// Meme precaution que RenderClose : remettre le shader par defaut avant de
// liberer, pour ne pas detruire le shader partage.
void RenderUnloadModel(Model m)
{
    m.materials[0].shader = (Shader){ rlGetShaderIdDefault(), rlGetShaderLocsDefault() };
    UnloadModel(m);
}

Vector3 RenderSunDir(void) { return sunDir; }

// A appeler au debut de chaque passe 3D (une par vue en ecran partage).
// Remet aussi a zero les options (emissive, checker, camo) pour repartir d'un
// etat connu.
//  - viewPos : position de la camera (pour le calcul du brouillard)
//  - lightDir : direction du soleil
//  - fogDensity : 0 = pas de brouillard ; plus grand = brouillard plus proche
//  - flashPos / flashPower : position et intensite de l'eclair du canon (0 = eteint)
void RenderSetFrame(Vector3 viewPos, Vector3 lightDir, float fogDensity, Vector3 flashPos, float flashPower)
{
    SetU3(locViewPos, viewPos);
    SetU3(locLightDir, lightDir);
    Vector4 fog = ColorVec(SKY_HORIZON);   // brouillard de la couleur de l'horizon : fondu naturel avec le ciel
    SetShaderValue(shader, locFogColor, &fog, SHADER_UNIFORM_VEC4);
    SetU1(locFogDensity, fogDensity);
    SetU3(locFlashPos, flashPos);
    SetU1(locFlashPower, flashPower);
    SetU1(locEmissive, 0.0f);
    SetU1(locChecker, 0.0f);
    SetU1(locArena, ARENA);
    SetU1(locCamo, 0.0f);
    SetShaderValueV(shader, locLampPos, LAMPS, SHADER_UNIFORM_VEC3, LAMP_COUNT);
    SetU1(locLamps, 1.0f);
}

// Lampes de la nuit allumees ou non. La passe de l'arme les eteint : elle est
// dessinee dans un repere vue ou les positions des lampes n'auraient pas de sens.
void RenderSetLamps(bool on) { SetU1(locLamps, on ? 1.0f : 0.0f); }

// Interrupteurs d'effets : ils restent actifs pour tous les dessins suivants
// jusqu'a ce qu'on les desactive.
void RenderSetEmissive(bool on) { SetU1(locEmissive, on ? 1.0f : 0.0f); }
void RenderSetSurface(Surface s) { SetU1(locChecker, (float)s); }

// Couleur 0..255 -> vecteur RVB 0..1 (sans alpha)
static Vector3 ColorRGB(Color c) { return V3(c.r/255.0f, c.g/255.0f, c.b/255.0f); }

// Active le camouflage. "body" est la matrice monde du corps : on envoie son
// inverse au shader (camoSpace) pour qu'il ramene chaque pixel dans le repere
// du corps ; le motif est ainsi "colle" au personnage quand il bouge.
void RenderSetCamo(bool on, Matrix body, Color a, Color b, Color c)
{
    SetU1(locCamo, on ? 1.0f : 0.0f);
    if (!on) return;
    SetShaderValueMatrix(shader, locCamoSpace, MatrixInvert(body));
    SetU3(locCamoA, ColorRGB(a));
    SetU3(locCamoB, ColorRGB(b));
    SetU3(locCamoC, ColorRGB(c));
}

// raylib accumule les dessins dans un "batch" envoye plus tard au GPU : il
// faut d'abord le vider (rlDrawRenderBatchActive) pour que ce qui a ete
// demande avant soit bien dessine AVANT l'effacement du depth buffer.
void RenderClearDepth(void)
{
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);
}

// Ciel de nuit : degrade vertical du haut de l'ecran jusqu'un peu sous le
// milieu (+40 px, a peu pres la ligne d'horizon), couleur unie en dessous
// (cachee par le sol en general), puis etoiles et lune.
// Les etoiles sont des directions fixes dans le monde, projetees a l'ecran
// avec la camera : elles tournent avec le regard comme un vrai ciel.
void DrawSky(Camera3D cam, int width, int height)
{
    DrawRectangleGradientV(0, 0, width, height/2 + 40, SKY_TOP, SKY_HORIZON);
    DrawRectangle(0, height/2 + 40, width, height/2, SKY_HORIZON);

    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    unsigned int seed = 12345u;
    for (int i = 0; i < 220; i++)
    {
        // Petit generateur pseudo-aleatoire a graine fixe : toujours les memes etoiles
        seed = seed*1103515245u + 12345u; float az = (seed >> 8)*(2*PI/16777216.0f);
        seed = seed*1103515245u + 12345u; float el = 0.08f + (seed >> 8)*(1.4f/16777216.0f);
        seed = seed*1103515245u + 12345u; float b = 0.35f + (seed >> 8)*(0.65f/16777216.0f);
        Vector3 d = V3(cosf(el)*sinf(az), sinf(el), cosf(el)*cosf(az));
        if (Vector3DotProduct(d, fwd) < 0.2f) continue;   // derriere la camera
        Vector2 sp = GetWorldToScreenEx(Vector3Add(cam.position, Vector3Scale(d, 100.0f)), cam, width, height);
        DrawCircleV(sp, b > 0.9f ? 2.0f : 1.3f, Fade((Color){ 230, 235, 255, 255 }, b));
    }

    // Lune : a l'oppose de la direction de sa lumiere (sunDir pointe vers le sol)
    Vector3 moon = Vector3Negate(sunDir);
    if (Vector3DotProduct(moon, fwd) > 0.2f)
    {
        Vector2 sp = GetWorldToScreenEx(Vector3Add(cam.position, Vector3Scale(moon, 100.0f)), cam, width, height);
        float r = height*0.035f;
        DrawCircleV(sp, r*2.2f, Fade((Color){ 140, 160, 220, 255 }, 0.12f));   // halo
        DrawCircleV(sp, r, (Color){ 235, 238, 225, 255 });
        DrawCircleV((Vector2){ sp.x + r*0.3f, sp.y - r*0.2f }, r*0.22f, (Color){ 205, 208, 196, 255 });   // crateres
        DrawCircleV((Vector2){ sp.x - r*0.35f, sp.y + r*0.3f }, r*0.15f, (Color){ 210, 212, 200, 255 });
    }
}

//----------------------------------------------------------------------------------
// Primitives
//----------------------------------------------------------------------------------
// On remplace la transformation du modele par la matrice voulue, puis on le
// dessine a l'origine avec une echelle de 1 : toute la position vient de "world".
void DrawPart(Model *m, Matrix world, Color c)
{
    m->transform = world;
    DrawModel(*m, (Vector3){ 0 }, 1.0f, c);
}

// Matrice Translation * Rotation * Scale : on met d'abord a l'echelle, puis on
// tourne (angles d'Euler en radians), puis on deplace (ordre de raymath :
// le premier argument de MatrixMultiply est applique en premier).
Matrix TRS(Vector3 t, Vector3 r, Vector3 s)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateXYZ(r)), MatrixTranslate(t.x, t.y, t.z));
}

// Cube : matrice locale TRS puis repere parent
void Box3(Matrix parent, Vector3 t, Vector3 r, Vector3 s, Color c) { DrawPart(&mCube, MatrixMultiply(TRS(t, r, s), parent), c); }

// Sphere de rayon 1 etiree en ellipsoide de rayons "radii", sans rotation
void Ball(Matrix parent, Vector3 t, Vector3 radii, Color c) { DrawPart(&mSphere, MatrixMultiply(TRS(t, NOROT, radii), parent), c); }

// Ellipsoide dont on donne directement les axes : la matrice "frame" a pour
// colonnes les axes x, y, z et la position t (c'est une matrice de
// changement de repere). On etire la sphere, on la place dans ce repere, puis
// dans le repere parent. x et y doivent etre unitaires et perpendiculaires.
void BallAxes(Matrix parent, Vector3 t, Vector3 x, Vector3 y, Vector3 radii, Color c)
{
    Vector3 z = Vector3CrossProduct(x, y);
    Matrix frame = { x.x, y.x, z.x, t.x,
                     x.y, y.y, z.y, t.y,
                     x.z, y.z, z.z, t.z,
                     0, 0, 0, 1 };
    DrawPart(&mSphere, MatrixMultiply(MatrixMultiply(MatrixScale(radii.x, radii.y, radii.z), frame), parent), c);
}

// Cylindre de rayon "radius" reliant les points a et b (repere parent).
// Le cylindre de base va de y = 0 a y = 1 : on l'etire a la longueur voulue,
// on le tourne de l'axe Y vers la direction a->b (quaternion), puis on le
// place en a.
void Seg(Matrix parent, Vector3 a, Vector3 b, float radius, Color c)
{
    // Le cylindre est symetrique : on l'oriente toujours vers le haut pour
    // eviter le cas degenere d'une rotation de 180 degres.
    if (b.y < a.y) { Vector3 tmp = a; a = b; b = tmp; }
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-5f) return;   // segment de longueur nulle : rien a dessiner (et evite une division par 0)
    Quaternion q = QuaternionFromVector3ToVector3(V3(0, 1, 0), Vector3Scale(d, 1.0f/len));
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(radius, len, radius), QuaternionToMatrix(q)), MatrixTranslate(a.x, a.y, a.z));
    DrawPart(&mCyl, MatrixMultiply(m, parent), c);
}

// Membre (bras, jambe...) : un cylindre avec une sphere a chaque bout, ce qui
// donne des articulations arrondies sans trous quand le membre se plie.
void Limb(Matrix parent, Vector3 a, Vector3 b, float radius, Color c)
{
    Seg(parent, a, b, radius, c);
    Ball(parent, a, V3(radius, radius, radius), c);
    Ball(parent, b, V3(radius, radius, radius), c);
}

// Ombre "pastille" : un disque noir transparent tres plat (cylindre de 2 mm
// d'epaisseur) pose juste au-dessus du sol (y = 1 cm pour eviter le
// scintillement avec le sol, le "z-fighting").
void DrawBlobShadow(Vector3 p, float radius)
{
    Seg(MatrixIdentity(), V3(p.x, 0.01f, p.z), V3(p.x, 0.012f, p.z), radius, (Color){ 0, 0, 0, 90 });
}

// Poussin construit avec des spheres et des cubes, dans le repere "parent"
// (origine au sol entre les pattes, +Z vers l'avant). Animations :
//  - picorage : la tete descend et avance par moments ;
//  - ailes : rotation autour de Z qui oscille vite quand flap > 0.
void DrawChick(Matrix parent, float t, float flap)
{
    const Color yellow = { 255, 214, 50, 255 }, wing = { 240, 186, 30, 255 }, orange = { 255, 135, 25, 255 };
    // Picore de temps en temps
    // sin^8 (en ne gardant que la partie positive) donne des "pics" courts et
    // separes par des pauses : la tete ne s'abaisse que brievement.
    float peck = powf(fmaxf(0.0f, sinf(t*1.7f)), 8.0f);
    Vector3 head = V3(0, 0.19f - 0.05f*peck, 0.06f + 0.04f*peck);

    Ball(parent, V3(0, 0.09f, 0), V3(0.1f, 0.085f, 0.11f), yellow);   // corps
    Ball(parent, V3(0, 0.12f, -0.1f), V3(0.04f, 0.03f, 0.04f), yellow);   // queue
    Ball(parent, head, V3(0.07f, 0.068f, 0.07f), yellow);   // tete
    Ball(parent, Vector3Add(head, V3(0, 0.07f, -0.01f)), V3(0.012f, 0.03f, 0.012f), yellow);   // houppette
    Box3(parent, Vector3Add(head, V3(0, -0.005f, 0.075f)), V3(0, PI/4, 0), V3(0.035f, 0.02f, 0.035f), orange);   // bec
    // Boucle symetrique : k = -1 pour le cote gauche, k = +1 pour le cote droit
    for (int k = -1; k <= 1; k += 2)
    {
        Ball(parent, Vector3Add(head, V3(0.04f*k, 0.015f, 0.055f)), V3(0.013f, 0.016f, 0.01f), BLACK);   // oeil
        // Ailes : battent quand on l'attrape
        // Angle de base 0.15 rad, plus une oscillation rapide (38 rad/s, ~6 battements/s)
        // proportionnelle a flap.
        float a = 0.15f + flap*(0.5f + 0.5f*sinf(t*38.0f))*1.1f;
        // Charniere de l'aile : rotation autour de Z puis deplacement sur le flanc
        Matrix wm = MatrixMultiply(MatrixMultiply(MatrixRotateZ(k*a), MatrixTranslate(0.09f*k, 0.13f, 0)), parent);
        Ball(wm, V3(0, -0.035f, -0.01f), V3(0.025f, 0.05f, 0.07f), wing);
        Seg(parent, V3(0.035f*k, 0.0f, 0.02f), V3(0.035f*k, 0.04f, 0.0f), 0.008f, orange);   // pattes
        Box3(parent, V3(0.035f*k, 0.005f, 0.035f), NOROT, V3(0.03f, 0.01f, 0.05f), orange);   // pied
    }
}
