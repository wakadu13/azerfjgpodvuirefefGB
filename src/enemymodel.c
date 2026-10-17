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
 *
 * Repere du corps ("body space") utilise par toutes les coordonnees de ce
 * fichier : origine entre les pieds au sol, y vers le haut, +z vers l'avant
 * (le regard), +x vers la GAUCHE du personnage. Unites en metres pour un
 * mercenaire de taille 1 (~1.9 m de haut, sommet du crane vers y = 1.9).
 * La matrice du mercenaire met ensuite le tout a l'echelle (sz), l'oriente
 * (yaw) et le place dans le monde (pos).
 *
 * Le modele est entierement procedural : il est assemble a partir de
 * primitives (Ball = ellipsoide, Seg = cylindre, Limb = cylindre + rotules,
 * Box3 = pave) dessinees chaque frame avec une matrice parent.
 *
 * Rappel raylib : MatrixMultiply(A, B) applique d'abord A puis B.
 */
#include "enemy.h"
#include "weapon.h"
#include "render.h"
#include "util.h"

#include "raymath.h"

#include <math.h>

typedef struct { Color base, a, b, c; } Cloth;   // tissu + 3 nuances (usure, boue, delave)

// Couleurs du treillis, une entree par valeur de e->camo.
// Les nuances a, b, c sont passees au shader de camouflage (RenderSetCamo).
static const Cloth UNIFORMS[CAMO_COUNT] = {
    { { 88, 96, 68, 255 }, { 78, 86, 60, 255 }, { 62, 66, 48, 255 }, { 102, 108, 80, 255 } },   // olive
    { { 80, 86, 68, 255 }, { 70, 76, 60, 255 }, { 56, 58, 48, 255 }, { 94, 98, 80, 255 } },     // olive gris
    { { 92, 92, 66, 255 }, { 80, 80, 58, 255 }, { 64, 60, 44, 255 }, { 106, 104, 78, 255 } },   // olive brun
    { { 70, 78, 56, 255 }, { 62, 70, 50, 255 }, { 48, 52, 38, 255 }, { 84, 90, 66, 255 } },     // olive fonce
};
// Le chef a un treillis plus sombre, toujours le meme
static const Cloth UNIFORM_BOSS = { { 58, 62, 50, 255 }, { 48, 52, 42, 255 }, { 36, 36, 30, 255 }, { 72, 76, 60, 255 } };
// Teintes de peau (e->skin), du plus clair au plus fonce
static const Color SKINS[SKIN_COUNT] = { { 238, 196, 170, 255 }, { 222, 172, 140, 255 }, { 182, 128, 96, 255 }, { 120, 84, 60, 255 } };
// Couleurs de cheveux (choisies via (skin + camo) % 4 pour varier sans champ supplementaire)
static const Color HAIRS[4] = { { 86, 62, 42, 255 }, { 50, 38, 28, 255 }, { 132, 102, 70, 255 }, { 30, 26, 22, 255 } };
static const Color PAINTS[PAINT_COUNT] = { { 112, 126, 80, 255 }, { 96, 112, 70, 255 }, { 120, 128, 94, 255 } };   // vert du visage

// Couleurs fixes des details
static const Color PAINT_BLACK = { 24, 26, 22, 255 };    // peinture noire du visage
static const Color PAINT_DARK = { 54, 70, 40, 255 };     // vert fonce
static const Color EYE_WHITE = { 238, 212, 206, 255 };   // yeux injectes
static const Color ARMBAND = { 238, 112, 38, 255 };      // brassard / ecusson orange
static const Color BLOOD = { 168, 24, 20, 255 };         // taches de sang sur le brassard
static const Color EPAULET_RED = { 190, 36, 30, 255 };   // epaulettes et foulard
static const Color BERET_RED = { 112, 24, 32, 255 };     // beret bordeaux
static const Color STEEL = { 158, 162, 168, 255 };       // chaines, boucle de ceinture, cadenas
static const Color BOOTS = { 28, 26, 24, 255 };          // rangers
static const Color BRASS = { 230, 180, 50, 255 };        // laiton : insigne et cadenas du chef

// Volumes du torse (repere du corps, avant = +z) : taille, poitrine,
// epaules. Les chaines s'y posent, la carrure f elargit x et z.
// c = centre de l'ellipsoide, r = ses trois demi-axes (rayons en x, y, z).
typedef struct { Vector3 c, r; } Ellipsoid;

// Remplit les 3 ellipsoides du torse pour une carrure f.
// La hauteur (y) ne depend pas de f : un mercenaire gros est plus large, pas plus grand.
static void TorsoShape(float f, Ellipsoid out[3])
{
    out[0] = (Ellipsoid){ V3(0, 1.1f, 0), V3(0.19f*f, 0.17f, 0.13f*f) };             // ventre / taille
    out[1] = (Ellipsoid){ V3(0, 1.33f, 0.012f*f), V3(0.215f*f, 0.2f, 0.14f*f) };     // poitrine (un peu bombee vers l'avant)
    out[2] = (Ellipsoid){ V3(0, 1.48f, 0), V3(0.25f*f, 0.09f, 0.13f*f) };            // epaules (large et plat)
}

// Point de la surface du torse dans la direction d depuis c (le plus
// exterieur des trois volumes), avec la normale a cet endroit.
//
// Principe (intersection rayon / ellipsoide) : on divise tout par les rayons
// r pour transformer l'ellipsoide en sphere unite. Le rayon c + t*d devient
// o + t*v, et on resout |o + t*v|^2 = 1, soit
//     (v.v) t^2 + 2 (o.v) t + (o.o - 1) = 0   ->   a t^2 + 2b t + cc = 0
// Comme c est a l'interieur du torse, la plus grande racine
// t = (-b + sqrt(b^2 - a*cc)) / a est le point de sortie. On garde le t le
// plus grand des trois volumes : la surface visible de leur union.
// La normale d'un ellipsoide en p (relatif au centre) est le gradient
// (p.x/r.x^2, p.y/r.y^2, p.z/r.z^2), normalise.
// Si aucun volume n'est touche, on renvoie c et la normale vaut d.
static Vector3 SurfaceAlong(const Ellipsoid *shape, Vector3 c, Vector3 d, Vector3 *normal)
{
    float best = 0.0f;
    *normal = d;
    for (int i = 0; i < 3; i++)
    {
        Vector3 r = shape[i].r;
        // Origine et direction du rayon dans l'espace ou l'ellipsoide est une sphere unite
        Vector3 o = V3((c.x - shape[i].c.x)/r.x, (c.y - shape[i].c.y)/r.y, (c.z - shape[i].c.z)/r.z);
        Vector3 v = V3(d.x/r.x, d.y/r.y, d.z/r.z);
        float a = Vector3DotProduct(v, v), b = Vector3DotProduct(o, v), cc = Vector3DotProduct(o, o) - 1.0f;
        float disc = b*b - a*cc;   // discriminant (reduit) : < 0 = le rayon rate ce volume
        if (disc < 0) continue;
        float t = (-b + sqrtf(disc))/a;
        if (t <= best) continue;   // un autre volume deborde deja plus loin
        best = t;
        // Point touche, relatif au centre de l'ellipsoide, puis normale par le gradient
        Vector3 p = Vector3Subtract(Vector3Add(c, Vector3Scale(d, t)), shape[i].c);
        *normal = Vector3Normalize(V3(p.x/(r.x*r.x), p.y/(r.y*r.y), p.z/(r.z*r.z)));
    }
    return Vector3Add(c, Vector3Scale(d, best));
}

//----------------------------------------------------------------------------------
// Chaines
//----------------------------------------------------------------------------------
// Chaque maillon est un tore (un "donut") a section ronde dont l'ame est une
// ellipse : LINK_SEG points le long de l'ellipse, LINK_SIDES points autour
// du fil a chacun de ces points.
#define LINK_SEG     10    // segments autour d'un maillon
#define LINK_SIDES   6     // segments autour du fil
#define LINK_VERTS   (LINK_SEG*LINK_SIDES)   // sommets par maillon (et nombre de quads)
#define CHAIN_STEPS  20    // carrures en cache : 1.0 a 1.95 par pas de 0.05
#define PATH_SAMPLES 200   // points d'echantillonnage du trajet de la chaine (pour mesurer sa longueur)

typedef enum { CHAIN_LIGHT, CHAIN_HEAVY, CHAIN_KINDS } ChainKind;   // autour du cou, + en bandouliere (colosse, chef)

// Cache des meshes de chaines : un modele par type de chaine et par carrure arrondie.
// La carrure change quand un mercenaire vole un poussin, d'ou plusieurs versions.
static Model chainModels[CHAIN_KINDS][CHAIN_STEPS];
static bool chainReady[CHAIN_KINDS][CHAIN_STEPS];   // true quand chainModels[k][i] est genere

// Une boucle de chaine. Collier : fait le tour du cou et pend sur la
// poitrine. Bandouliere : cercle incline autour du torse (directions u, v).
// c = centre de la boucle (bandouliere seulement).
typedef struct { bool necklace; Vector3 c, u, v; } ChainLoop;

// Point de la boucle a l'angle a (0 .. 2*PI) pose sur la surface du torse,
// avec la normale du torse a cet endroit.
static Vector3 LoopPoint(const Ellipsoid *shape, const ChainLoop *l, float a, Vector3 *normal)
{
    if (l->necklace)
    {
        // a = 0 : devant (+z). On lance un rayon horizontal depuis l'axe du cou ;
        // devant, le point de depart descend (jusqu'a 0.4 m) : la chaine pend en
        // V sur la poitrine. front^3 garde le dos et les cotes presque a plat.
        float front = fmaxf(0.0f, cosf(a));
        Vector3 from = V3(0, 1.555f - 0.4f*front*front*front, 0);
        return SurfaceAlong(shape, from, V3(sinf(a), 0, cosf(a)), normal);
    }
    // Bandouliere : direction dans le plan (u, v), un cercle autour de c
    Vector3 d = Vector3Add(Vector3Scale(l->u, cosf(a)), Vector3Scale(l->v, sinf(a)));
    return SurfaceAlong(shape, l->c, d, normal);
}

// Ajoute un maillon (tore aplati) : t = sens de la chaine, w = second axe du maillon.
// center : centre du maillon ; a, b : demi-axes de l'ellipse de l'ame (a le long de t,
// b le long de w) ; r : rayon du fil. Ecrit directement dans les tableaux du mesh,
// qui doivent avoir ete alloues assez grands (voir ChainModel).
static void AddLink(Mesh *m, Vector3 center, Vector3 t, Vector3 w, float a, float b, float r)
{
    Vector3 q = Vector3Normalize(Vector3CrossProduct(t, w));   // perpendiculaire au plan du maillon
    int base = m->vertexCount;   // indice du premier sommet de ce maillon
    for (int i = 0; i < LINK_SEG; i++)
    {
        float phi = 2*PI*i/LINK_SEG;   // position le long de l'ellipse
        // Point de l'ellipse de l'ame
        Vector3 ring = Vector3Add(Vector3Scale(t, a*cosf(phi)), Vector3Scale(w, b*sinf(phi)));
        // Normale de l'ellipse dans son plan (gradient : cos/a, sin/b), vers l'exterieur
        Vector3 rn = Vector3Normalize(Vector3Add(Vector3Scale(t, cosf(phi)/a), Vector3Scale(w, sinf(phi)/b)));
        for (int j = 0; j < LINK_SIDES; j++)
        {
            // Petit cercle du fil autour de l'ame, dans le plan (rn, q)
            float psi = 2*PI*j/LINK_SIDES;
            Vector3 n = Vector3Add(Vector3Scale(rn, cosf(psi)), Vector3Scale(q, sinf(psi)));
            Vector3 p = Vector3Add(Vector3Add(center, ring), Vector3Scale(n, r));
            // Sommet (i, j) a l'indice base + i*LINK_SIDES + j ; n est aussi la normale de lumiere
            int k = m->vertexCount++;
            m->vertices[k*3] = p.x; m->vertices[k*3 + 1] = p.y; m->vertices[k*3 + 2] = p.z;
            m->normals[k*3] = n.x; m->normals[k*3 + 1] = n.y; m->normals[k*3 + 2] = n.z;
        }
    }
    // Sens des triangles : on verifie le premier contre sa normale (faces avant = sens trigo)
    // (selon l'orientation de t et w, la grille peut etre "a l'envers" ; flip inverse alors
    // l'ordre des sommets pour que les faces avant regardent vers l'exterieur)
    Vector3 p0 = V3(m->vertices[base*3], m->vertices[base*3 + 1], m->vertices[base*3 + 2]);
    Vector3 p1 = V3(m->vertices[(base + LINK_SIDES)*3], m->vertices[(base + LINK_SIDES)*3 + 1], m->vertices[(base + LINK_SIDES)*3 + 2]);
    Vector3 p2 = V3(m->vertices[(base + LINK_SIDES + 1)*3], m->vertices[(base + LINK_SIDES + 1)*3 + 1], m->vertices[(base + LINK_SIDES + 1)*3 + 2]);
    Vector3 n0 = V3(m->normals[base*3], m->normals[base*3 + 1], m->normals[base*3 + 2]);
    bool flip = Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)), n0) < 0;
    // Chaque case (i, j) de la grille est un quad i0-i1-i2-i3 coupe en deux triangles.
    // Les modulos referment la grille dans les deux sens (c'est un tore).
    for (int i = 0; i < LINK_SEG; i++)
        for (int j = 0; j < LINK_SIDES; j++)
        {
            int i0 = base + i*LINK_SIDES + j, i1 = base + ((i + 1) % LINK_SEG)*LINK_SIDES + j;
            int i2 = base + ((i + 1) % LINK_SEG)*LINK_SIDES + (j + 1) % LINK_SIDES, i3 = base + i*LINK_SIDES + (j + 1) % LINK_SIDES;
            // Indices 16 bits (format de raylib) : le mesh d'une chaine doit rester sous 65536 sommets
            unsigned short *tri = &m->indices[m->triangleCount*3];
            tri[0] = (unsigned short)i0; tri[1] = (unsigned short)(flip ? i2 : i1); tri[2] = (unsigned short)(flip ? i1 : i2);
            tri[3] = (unsigned short)i0; tri[4] = (unsigned short)(flip ? i3 : i2); tri[5] = (unsigned short)(flip ? i2 : i3);
            m->triangleCount += 2;
        }
}

// Maillons le long d'une boucle, un a plat, un debout, en alternance
// scale : grosseur des maillons. build = false : passe de comptage seulement
// (on additionne les sommets/triangles necessaires sans rien ecrire).
static void AddLoop(Mesh *m, const Ellipsoid *shape, const ChainLoop *l, float scale, bool build)
{
    // a, b : demi-axes du maillon ; r : rayon du fil.
    // pitch = longueur interieure d'un maillon = ecart entre deux maillons qui s'emboitent.
    float a = 0.035f*scale, b = 0.021f*scale, r = 0.0085f*scale, pitch = 2*(a - r);
    // Echantillonne le trajet et calcule la longueur cumulee len[i] depuis le debut
    // (abscisse curviligne) : elle permet d'espacer les maillons regulierement,
    // meme si les points de LoopPoint ne sont pas regulierement espaces.
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

    int k = 0;   // segment courant du trajet (ne fait qu'avancer : s est croissant)
    for (int n = 0; n < count; n++)
    {
        float s = len[PATH_SAMPLES]*n/count;   // distance le long de la chaine du maillon n
        // Cherche le segment [k, k+1] qui contient s, puis la fraction f dans ce segment
        while (k < PATH_SAMPLES - 1 && len[k + 1] < s) k++;
        float f = (s - len[k])/fmaxf(len[k + 1] - len[k], 1e-6f);
        Vector3 p = Vector3Lerp(pts[k], pts[k + 1], f);                     // position
        Vector3 t = Vector3Normalize(Vector3Subtract(pts[k + 1], pts[k]));  // tangente (sens de la chaine)
        Vector3 nm = Vector3Normalize(Vector3Lerp(nrm[k], nrm[k + 1], f));  // normale du torse
        // Gram-Schmidt : on retire de nm sa composante le long de t pour avoir un repere orthogonal
        nm = Vector3Normalize(Vector3Subtract(nm, Vector3Scale(t, Vector3DotProduct(nm, t))));
        // Un maillon sur deux est debout (plan perpendiculaire au torse), l'autre a plat
        bool standing = n & 1;
        Vector3 w = standing ? nm : Vector3CrossProduct(t, nm);
        // On decolle le maillon de la surface : de tout son demi-axe s'il est debout,
        // de l'epaisseur du fil s'il est a plat (+3 mm pour ne pas s'enfoncer dans le tissu)
        Vector3 center = Vector3Add(p, Vector3Scale(nm, (standing ? b + r : r) + 0.003f));
        AddLink(m, center, t, w, a, b, r);
    }
}

// Renvoie le modele de chaine pour un type et une carrure f, en le generant
// (puis en le gardant en cache) la premiere fois.
static Model *ChainModel(ChainKind kind, float f)
{
    // Carrure arrondie au pas de 0.05 le plus proche, bornee a [0, CHAIN_STEPS-1]
    int step = (int)Clamp(roundf((f - 1.0f)/0.05f), 0, CHAIN_STEPS - 1);
    Model *model = &chainModels[kind][step];
    if (chainReady[kind][step]) return model;

    float fq = 1.0f + step*0.05f;   // carrure arrondie utilisee pour generer le mesh
    Ellipsoid shape[3];
    TorsoShape(fq, shape);
    ChainLoop neck = { true, { 0 }, { 0 }, { 0 } };
    // Bandouliere : cercle centre sur la poitrine, dans le plan engendre par
    // u (incline de 0.85 rad ~ 49 deg dans le plan x/y : de l'epaule vers la hanche opposee)
    // et v = +z (passe devant et derriere le torse)
    ChainLoop bandolier = { false, V3(0, 1.3f, 0.01f*fq), V3(cosf(0.85f), sinf(0.85f), 0), V3(0, 0, 1) };

    // Deux passes : compter, puis remplir
    // (on connait ainsi la taille exacte des tableaux avant de les allouer)
    Mesh mesh = { 0 };
    for (int pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            // Fin du comptage : allocation, puis on remet les compteurs a zero
            // (AddLink les fait remonter en ecrivant)
            int verts = mesh.vertexCount, tris = mesh.triangleCount;
            mesh = (Mesh){ 0 };
            mesh.vertices = (float *)MemAlloc(verts*3*sizeof(float));
            mesh.normals = (float *)MemAlloc(verts*3*sizeof(float));
            mesh.indices = (unsigned short *)MemAlloc(tris*3*sizeof(unsigned short));
        }
        AddLoop(&mesh, shape, &neck, 1.0f, pass == 1);
        if (kind == CHAIN_HEAVY) AddLoop(&mesh, shape, &bandolier, 1.3f, pass == 1);   // maillons 30 % plus gros
    }

    *model = RenderLoadModel(mesh);   // envoie le mesh a la carte graphique
    chainReady[kind][step] = true;
    return model;
}

// Libere tous les meshes de chaines generes (fermeture du jeu)
void EnemyModelsUnload(void)
{
    for (int k = 0; k < CHAIN_KINDS; k++)
        for (int i = 0; i < CHAIN_STEPS; i++)
            if (chainReady[k][i]) { RenderUnloadModel(chainModels[k][i]); chainReady[k][i] = false; }
}

//----------------------------------------------------------------------------------
// Tete : visage peint
//----------------------------------------------------------------------------------
// Crane = ellipsoide de centre HEAD_C et de demi-axes HEAD_R (repere du corps)
static const Vector3 HEAD_C = { 0, 1.775f, 0.012f };
static const Vector3 HEAD_R = { 0.105f, 0.13f, 0.115f };

// Point du visage dans la direction (lacet, tangage) et sa normale
// yaw : angle horizontal (0 = face, > 0 vers la gauche du personnage, +x)
// pitch : angle vertical (> 0 vers le haut). Permet de placer yeux, nez,
// bouche et traits de peinture exactement sur la surface du crane.
static Vector3 OnHead(float yaw, float pitch, Vector3 *normal)
{
    // Direction unitaire (coordonnees spheriques)
    Vector3 d = V3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
    // Distance t telle que p = t*d soit sur l'ellipsoide : |p / R| = 1  ->  t = 1 / |d / R|
    float t = 1.0f/sqrtf(powf(d.x/HEAD_R.x, 2) + powf(d.y/HEAD_R.y, 2) + powf(d.z/HEAD_R.z, 2));
    Vector3 p = Vector3Scale(d, t);
    // Normale = gradient de l'ellipsoide (comme dans SurfaceAlong)
    *normal = Vector3Normalize(V3(p.x/(HEAD_R.x*HEAD_R.x), p.y/(HEAD_R.y*HEAD_R.y), p.z/(HEAD_R.z*HEAD_R.z)));
    return Vector3Add(HEAD_C, p);
}

// Trait de peinture pose sur le visage ; angle = inclinaison (PI/2 = vertical)
// len, wid : demi-longueur et demi-largeur du trait. Le trait est un ellipsoide
// tres aplati, oriente dans le plan tangent au crane et legerement enfonce dedans.
static void Stroke(Matrix body, float yaw, float pitch, float angle, float len, float wid, Color c)
{
    Vector3 n;
    Vector3 p = OnHead(yaw, pitch, &n);
    // Repere tangent local : east = horizontal le long du visage, north = vers le haut
    Vector3 east = Vector3Normalize(Vector3CrossProduct(V3(0, 1, 0), n));
    Vector3 north = Vector3CrossProduct(n, east);
    // Axe long du trait, tourne de `angle` dans le plan tangent
    Vector3 x = Vector3Add(Vector3Scale(east, cosf(angle)), Vector3Scale(north, sinf(angle)));
    // Enfonce de 4 mm pour que seule une fine pellicule depasse (epaisseur 0.011)
    BallAxes(body, Vector3Subtract(p, Vector3Scale(n, 0.004f)), x, Vector3CrossProduct(n, x), V3(len, wid, 0.011f), c);
}

// Peinture de guerre du visage. style = e->paint (0 .. PAINT_COUNT-1), hf = flash de degats.
// Les boucles k = -1 puis +1 dessinent les deux cotes symetriques du visage.
static void DrawFacePaint(Matrix body, int style, float hf)
{
    Color black = Flash(PAINT_BLACK, hf), dark = Flash(PAINT_DARK, hf);
    // Commun a tous les styles
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
            // Deux coulures asymetriques sur le front
            Stroke(body, 0.12f, 0.52f, PI/2 - 0.15f, 0.06f, 0.009f, black);
            Stroke(body, -0.14f, 0.48f, PI/2 + 0.2f, 0.05f, 0.008f, black);
            break;
        case 1:   // taches vert fonce et noires
            for (int k = -1; k <= 1; k += 2)
            {
                Stroke(body, 0.55f*k, -0.25f, 0.6f*k, 0.05f, 0.028f, dark);       // joues
                Stroke(body, 0.5f*k, -0.45f, PI/2 - 0.2f*k, 0.05f, 0.016f, black); // machoire
            }
            Stroke(body, 0.1f, 0.45f, 0.2f, 0.07f, 0.024f, dark);   // tache sur le front
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

// Tete complete : cou, machoire, crane, oreilles, cheveux (ou beret du chef),
// peinture, nez, yeux et bouche. skin et hf : couleur de peau deja "flashee"
// et intensite du flash de degats.
static void DrawHead(Matrix body, const Enemy *e, Color skin, float hf)
{
    float shout = 0.0f;   // bouche : grande ouverte quand il frappe
    if (e->attackAnim > 0) shout = 1.0f;
    else if (e->state == ES_GRAB) shout = 0.4f + 0.4f*sinf(e->grabT*9.0f);   // ricane en fouillant le comptoir

    Color paint = Flash(PAINTS[e->paint % PAINT_COUNT], hf);   // le visage entier est peint en vert
    Color hair = Flash(HAIRS[(e->skin + e->camo) % 4], hf);

    Seg(body, V3(0, 1.5f, -0.005f), V3(0, 1.68f, 0.01f), 0.052f, paint);       // cou (peint aussi)
    Ball(body, V3(0, 1.69f, 0.025f), V3(0.085f, 0.06f, 0.085f), paint);        // machoire
    Ball(body, HEAD_C, HEAD_R, paint);                                          // crane
    for (int k = -1; k <= 1; k += 2) Ball(body, V3(0.103f*k, 1.765f, -0.005f), V3(0.018f, 0.04f, 0.028f), skin);   // oreilles

    // Cotes et nuque rases, un peu plus long sur le dessus
    // (ellipsoide a peine plus grand que le crane, assombri = cheveux tres courts)
    Ball(body, V3(0, 1.79f, -0.02f), V3(0.108f, 0.118f, 0.1f), Shade(hair, 0.75f));
    if (e->type == E_BOSS)
    {
        // Le chef porte son beret
        Color beret = Flash(BERET_RED, hf);
        Seg(body, V3(0, 1.85f, 0.005f), V3(0, 1.87f, 0.005f), 0.105f, Shade(beret, 0.6f));   // bandeau en cuir
        // Galette du beret : penchee sur le cote (rotation Z) et decalee vers -x (sa droite)
        Ball(MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.3f), MatrixTranslate(-0.025f, 1.9f, 0.0f)), body),
             V3(0, 0, 0), V3(0.12f, 0.04f, 0.12f), beret);
        Ball(body, V3(0.06f, 1.885f, 0.1f), V3(0.016f, 0.018f, 0.006f), Flash(BRASS, hf));   // insigne
    }
    else Ball(body, V3(0, 1.86f, 0.008f), V3(0.092f, 0.06f, 0.1f), hair);   // dessus du crane plus fourni

    DrawFacePaint(body, e->paint % PAINT_COUNT, hf);

    Vector3 n;
    // Nez : un peu sous le centre du visage, decolle de 12 mm de la surface
    Vector3 nose = OnHead(0, -0.08f, &n);
    Ball(body, Vector3Add(nose, Vector3Scale(n, 0.012f)), V3(0.018f, 0.03f, 0.022f), Shade(paint, 1.05f));
    for (int k = -1; k <= 1; k += 2)
    {
        // Yeux grands ouverts, injectes de sang : blanc de l'oeil un peu enfonce, pupille devant
        Vector3 eye = OnHead(0.36f*k, 0.09f, &n);
        Ball(body, Vector3Subtract(eye, Vector3Scale(n, 0.002f)), V3(0.024f, 0.017f, 0.012f), Flash(EYE_WHITE, hf));
        Ball(body, Vector3Add(eye, Vector3Scale(n, 0.009f)), V3(0.009f, 0.01f, 0.005f), (Color){ 40, 28, 20, 255 });   // pupille
    }
    // Bouche : un trait horizontal dont la hauteur grandit avec shout (bouche ouverte = sombre)
    Color lips = shout > 0.6f ? (Color){ 60, 18, 18, 255 } : Flash((Color){ 150, 96, 90, 255 }, hf);
    Stroke(body, 0, -0.42f, 0, 0.035f, 0.007f + 0.02f*shout, lips);
}

//----------------------------------------------------------------------------------
// Corps
//----------------------------------------------------------------------------------
// Positions des articulations d'un bras (repere du corps, avant inclinaison)
typedef struct { Vector3 shoulder, elbow, hand; } ArmPose;

// Le mercenaire se penche sur le comptoir pour attraper un poussin
// Renvoie l'angle d'inclinaison du buste vers l'avant (radians).
static float GrabLean(const Enemy *e)
{
    if (e->state != ES_GRAB) return e->type == E_FAST ? 0.12f : 0.06f;   // demarche penchee
    // Plus il est grand, plus il doit se pencher pour atteindre le comptoir
    float lean = Clamp(0.35f + (e->sz - 1.0f)*0.4f, 0.25f, 0.9f);
    // Pendant les 0.3 s qui precedent GRAB_HOLD, le facteur passe de 1 a 0 :
    return lean*(1.0f - Clamp((e->grabT - (GRAB_HOLD - 0.3f))/0.3f, 0.0f, 1.0f));   // se redresse avec le poussin
}

// Calcule la pose d'un bras. k = -1 : bras droit (cote -x), k = +1 : bras gauche (+x).
// walkSwing : balancier de la marche (-1 .. 1) ; lean : inclinaison du buste (GrabLean).
static ArmPose PoseArm(const Enemy *e, int k, float walkSwing, float lean)
{
    float f = e->fat, s = e->sz;
    ArmPose a;
    a.shoulder = V3(0.235f*k*f, 1.47f, 0);   // les epaules s'ecartent avec la carrure
    if (e->state == ES_DEAD)
    {
        // Mort : bras en croix, legerement vers l'arriere
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
            // tz : la main avance a ~0.5 m devant lui en grattant (petit va-et-vient sinus)
            // ty : hauteur de main dans le repere du corps telle qu'apres la rotation
            //      de `lean` autour du pivot y = 1 (voir leanM dans DrawEnemy) et la mise a
            //      l'echelle s, la main arrive a 1.2 m dans le monde :
            //      1 + (ty - 1)*cos(lean) - tz*sin(lean) = 1.2/s  ->  formule ci-dessous
            float tz = 0.5f + sinf(e->grabT*7.0f + k)*0.04f;
            float ty = 1.0f + (1.2f/s - 1.0f + tz*sinf(lean))/cosf(lean);
            a.elbow = V3(0.27f*k*f, (1.47f + ty)*0.5f, 0.22f);   // coude a mi-hauteur entre epaule et main
            a.hand = V3(0.12f*k*f, ty, tz);
        }
        return a;
    }
    float sway = walkSwing*k;   // les deux bras balancent en opposition
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
    // (interpolation entre la pose de marche et la pose d'attaque selon attackAnim ;
    // le bras droit, k = -1, suit un peu moins)
    float lunge = e->attackAnim*(k == 1 ? 1.0f : 0.8f);
    a.elbow = Vector3Lerp(a.elbow, V3(0.2f*k*f, 1.5f, 0.3f), lunge);
    a.hand = Vector3Lerp(a.hand, V3(0.1f*k*f, 1.55f, 0.62f), lunge);
    return a;
}

// Ecusson plat colle au torse (tissu, nom, insignes)
// (x, y) : position sur la face avant ; le pave est pose sur la surface et
// incline autour de x pour suivre la pente du torse a cet endroit.
static void Patch(Matrix body, const Ellipsoid *shape, float x, float y, Vector3 size, Color c)
{
    Vector3 n;
    Vector3 p = SurfaceAlong(shape, V3(x, y, 0), V3(0, 0, 1), &n);   // rayon vers l'avant depuis l'interieur
    Box3(body, Vector3Add(p, Vector3Scale(n, 0.002f)), V3(-atan2f(n.y, n.z), 0, 0), size, c);
}

// Dessine un mercenaire complet. Construit d'abord ses matrices :
//   root : mise a l'echelle, chute (mort) ou vol (harpon), orientation, position
//   body : root + inclinaison du buste (lean) et petit rebond de marche (bob)
// Les jambes et les bottes utilisent root (elles restent droites), tout le
// haut du corps utilise body.
static void DrawEnemy(const Enemy *e)
{
    float s = e->sz, f = e->fat, sf = sqrtf(f);   // sf : les membres grossissent moins vite que le tronc

    // Mort : bascule en arriere puis s'enfonce dans le sol
    float tilt = 0, sink = 0;
    if (e->state == ES_DEAD)
    {
        // k va de 0 a 1 en 0.7 s ; 1 - (1-k)^2 = chute rapide qui ralentit en fin de course
        float k = fminf(e->deadT/0.7f, 1.0f);
        tilt = (1.0f - (1.0f - k)*(1.0f - k))*PI*0.5f;   // jusqu'a 90 deg : allonge sur le dos
        if (e->deadT > 1.8f) sink = (e->deadT - 1.8f)*0.6f*s;   // apres 1.8 s, il s'enfonce dans le sol
    }
    // Ordre des transformations : echelle -> bascule (pivot aux pieds) -> lacet -> position
    Matrix root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateX(-tilt)),
                                 MatrixRotateY(e->yaw)), MatrixTranslate(e->pos.x, -sink, e->pos.z));
    if (e->launched)
    {
        // Projete : tourne autour de son ventre en vol, puis couche au sol
        // On descend le corps de 1 m (le ventre a l'origine), on applique la rotation
        // spin, puis on remonte le ventre a la bonne hauteur (centerH) au-dessus de pos.
        // Au sol, centerH = epaisseur du corps couche (proportionnelle a la carrure).
        float centerH = e->airborne ? 1.0f*s : 0.16f*s*f;
        sink = (!e->airborne && e->deadT > 1.8f) ? (e->deadT - 1.8f)*0.6f*s : 0.0f;
        root = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(0, -1.0f*s, 0)),
                              MatrixRotateX(-e->spin)), MatrixRotateY(e->yaw)),
                              MatrixTranslate(e->pos.x, e->pos.y + centerH - sink, e->pos.z));
    }

    float hf = e->hitFlash;   // flash blanc quand il est touche
    const Cloth *cl = (e->type == E_BOSS) ? &UNIFORM_BOSS : &UNIFORMS[e->camo % CAMO_COUNT];
    Color shirt = Flash(cl->base, hf), pants = Flash(Shade(cl->base, 0.8f), hf);   // pantalon un peu plus sombre
    Color skin = Flash(SKINS[e->skin % SKIN_COUNT], hf);

    // Animation de marche : sw = balancier des bras/jambes, bob = petit rebond a chaque pas
    // (|sin| fait deux bosses par cycle : un rebond par pied)
    bool walking = e->state == ES_WALK;
    float sw = walking ? sinf(e->phase) : 0.0f;
    float bob = walking ? fabsf(sinf(e->phase))*0.035f : 0.0f;
    float lean = (e->state == ES_DEAD) ? 0.0f : GrabLean(e);
    // Inclinaison autour des hanches : on ramene le pivot (y = 1 m) a l'origine,
    // on tourne autour de x, puis on remet en place (plus le rebond)
    Matrix leanM = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateX(lean)), MatrixTranslate(0, 1.0f + bob, 0));
    Matrix body = MatrixMultiply(leanM, root);
    ArmPose arms[2] = { PoseArm(e, -1, sw, lean), PoseArm(e, 1, sw, lean) };   // [0] = droit, [1] = gauche

    // Pantalon de treillis (taches d'usure et de boue calculees par le shader)
    // RenderSetCamo active le motif de camouflage pour les primitives qui suivent
    // (la matrice body sert de repere au motif pour qu'il suive le personnage)
    Vector3 ankle[2];
    RenderSetCamo(true, body, Flash(Shade(cl->a, 0.8f), hf), Flash(Shade(cl->b, 0.8f), hf), Flash(Shade(cl->c, 0.8f), hf));
    for (int k = -1; k <= 1; k += 2)
    {
        // swing : le pied avance (+z) ou recule ; quand il recule, il se souleve (lift)
        float swing = sw*k*0.24f, lift = fmaxf(0, -swing);
        Vector3 hip = V3(0.1f*k*f, 0.95f, 0);
        Vector3 foot = V3(0.12f*k*f, 0.1f + lift*0.18f, swing);
        // Genou a mi-chemin, legerement avance (+0.05 en z) pour que la jambe plie vers l'avant
        Vector3 knee = V3((hip.x + foot.x)*0.5f, 0.52f + lift*0.1f, (hip.z + foot.z)*0.5f + 0.05f);
        ankle[(k + 1)/2] = foot;   // k = -1 -> indice 0, k = +1 -> indice 1 (reutilise pour les rangers)
        Limb(root, hip, knee, 0.085f*sf, pants);                                  // cuisse
        Seg(root, knee, Vector3Add(foot, V3(0, 0.12f, 0)), 0.075f*sf, pants);     // tibia (s'arrete dans la botte)
        Vector3 pocket = Vector3Add(Vector3Lerp(hip, knee, 0.45f), V3(0.08f*k*sf, 0, 0.01f));   // poche cargo
        Box3(root, pocket, NOROT, V3(0.03f, 0.13f, 0.11f), Shade(pants, 0.88f));
    }
    Ball(body, V3(0, 0.98f, 0), V3(0.185f*f, 0.12f, 0.125f*f), pants);   // bassin

    // Veste de treillis : torse et manches retroussees au-dessus du coude
    Ellipsoid shape[3];
    TorsoShape(f, shape);
    RenderSetCamo(true, body, Flash(cl->a, hf), Flash(cl->b, hf), Flash(cl->c, hf));
    for (int i = 0; i < 3; i++) Ball(body, shape[i].c, shape[i].r, shirt);   // memes volumes que pour les chaines
    for (int i = 0; i < 2; i++)
    {
        // La manche s'arrete a 82 % de l'epaule au coude, puis un revers roule plus epais
        Vector3 cuff = Vector3Lerp(arms[i].shoulder, arms[i].elbow, 0.82f);
        Limb(body, arms[i].shoulder, cuff, 0.062f, shirt);
        Seg(body, cuff, Vector3Lerp(arms[i].shoulder, arms[i].elbow, 0.95f), 0.07f, Shade(shirt, 0.9f));   // revers roule
    }
    RenderSetCamo(false, body, BLANK, BLANK, BLANK);   // fin du camouflage : la suite est en couleurs unies

    // Col : t-shirt noir dessous, pointes du col
    Seg(body, V3(0, 1.53f, 0.0f), V3(0, 1.575f, 0.005f), 0.062f, Flash((Color){ 28, 28, 30, 255 }, hf));
    for (int k = -1; k <= 1; k += 2)
        Box3(body, V3(0.045f*k, 1.56f, 0.05f), V3(0.3f, 0, 0.6f*k), V3(0.06f, 0.01f, 0.045f), shirt);

    // Poches de poitrine, bande nominative, ecusson orange, insigne rond
    // (rabats des 2 poches, bande nominative a droite (-x), ecusson orange a gauche (+x))
    for (int k = -1; k <= 1; k += 2) Patch(body, shape, 0.1f*k*f, 1.4f, V3(0.09f, 0.035f, 0.012f), Shade(shirt, 0.85f));
    Patch(body, shape, -0.1f*f, 1.452f, V3(0.09f, 0.022f, 0.01f), Flash((Color){ 52, 56, 42, 255 }, hf));
    Patch(body, shape, 0.1f*f, 1.355f, V3(0.04f, 0.03f, 0.01f), Flash(ARMBAND, hf));
    {
        // Insigne rond, pose sur la surface du torse a gauche
        Vector3 n;
        Vector3 p = SurfaceAlong(shape, V3(0.16f*f, 1.3f, 0), V3(0, 0, 1), &n);
        Ball(body, Vector3Add(p, Vector3Scale(n, 0.004f)), V3(0.024f, 0.024f, 0.008f), Flash((Color){ 205, 195, 155, 255 }, hf));
    }

    // Epaulettes rouges (inclinees pour suivre la pente des epaules)
    Color red = Flash(EPAULET_RED, hf);
    for (int k = -1; k <= 1; k += 2) Box3(body, V3(0.15f*k*f, 1.562f, 0), V3(0, 0, -0.27f*k), V3(0.1f, 0.02f, 0.07f), red);
    if (e->gear == GEAR_BERET)   // beret plie, glisse sous l'epaulette gauche
    {
        Color beret = Flash(BERET_RED, hf);
        Ball(MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.5f), MatrixTranslate(0.19f*f, 1.57f, 0.02f)), body),
             V3(0, 0, 0), V3(0.06f, 0.03f, 0.075f), beret);
        // Bord replie du beret qui pend sous l'epaulette
        Box3(body, V3(0.215f*f, 1.53f, 0.03f), V3(0, 0, -0.9f), V3(0.012f, 0.06f, 0.06f), Shade(beret, 0.5f));
    }
    if (e->gear == GEAR_SCARF)   // foulard rouge et noir autour du cou
    {
        Seg(body, V3(0, 1.54f, 0.0f), V3(0, 1.6f, 0.005f), 0.075f, red);                                       // tour de cou
        Seg(body, V3(0, 1.563f, 0.0f), V3(0, 1.573f, 0.003f), 0.077f, Flash((Color){ 26, 22, 22, 255 }, hf));  // liseret noir
        Ball(body, V3(0.03f, 1.53f, 0.075f), V3(0.035f, 0.03f, 0.025f), red);                                  // noeud
        Box3(body, V3(0.045f, 1.47f, 0.13f), V3(-0.25f, 0, 0.3f), V3(0.035f, 0.1f, 0.01f), red);               // pan qui pend
    }

    // Ceinturon
    // Cylindre de rayon 1 ecrase par une matrice d'echelle pour epouser la taille (ovale)
    Matrix belt = MatrixMultiply(MatrixScale(0.19f*f, 1, 0.132f*f), body);
    Seg(belt, V3(0, 1.02f, 0), V3(0, 1.06f, 0), 1.0f, Flash((Color){ 24, 24, 24, 255 }, hf));
    Box3(body, V3(0, 1.04f, 0.132f*f), NOROT, V3(0.05f, 0.035f, 0.012f), Flash(STEEL, hf));   // boucle

    // Rangers
    Color boots = Flash(BOOTS, hf);
    for (int i = 0; i < 2; i++)
    {
        Vector3 a = ankle[i];
        Seg(root, a, Vector3Add(a, V3(0, 0.14f, 0)), 0.072f*sf, boots);                   // tige de la botte
        Box3(root, V3(a.x, 0.05f, a.z + 0.04f), NOROT, V3(0.11f*sf, 0.1f, 0.26f), boots);  // pied (pose au sol)
    }

    // Avant-bras nus, mains, montre au poignet gauche, brassard orange tache de sang
    for (int i = 0; i < 2; i++)
    {
        const ArmPose *a = &arms[i];
        Seg(body, Vector3Lerp(a->shoulder, a->elbow, 0.9f), a->elbow, 0.046f, skin);   // peau visible sous le revers
        Ball(body, a->elbow, V3(0.046f, 0.046f, 0.046f), skin);                          // coude
        Seg(body, a->elbow, a->hand, 0.042f, skin);                                      // avant-bras
        Ball(body, a->hand, V3(0.045f, 0.055f, 0.04f), skin);                            // main
    }
    {
        const ArmPose *a = &arms[1];   // bras gauche
        // Montre : bracelet noir vers 80 % du trajet coude -> main
        Seg(body, Vector3Lerp(a->elbow, a->hand, 0.76f), Vector3Lerp(a->elbow, a->hand, 0.84f), 0.05f, Flash((Color){ 20, 20, 22, 255 }, hf));
        // Brassard sur le haut du bras (par-dessus la manche), entre 30 % et 62 % epaule -> coude
        Vector3 b0 = Vector3Lerp(a->shoulder, a->elbow, 0.3f), b1 = Vector3Lerp(a->shoulder, a->elbow, 0.62f);
        Seg(body, b0, b1, 0.071f, Flash(ARMBAND, hf));
        Vector3 out = V3(0.066f, 0, 0);   // decalage vers l'exterieur du bras (+x = gauche)
        Color blood = Flash(BLOOD, hf);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.25f), V3(0.064f, 0.01f, 0.02f)), V3(0.012f, 0.022f, 0.014f), blood);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.7f), V3(0.064f, -0.01f, -0.015f)), V3(0.012f, 0.016f, 0.02f), blood);
        Ball(body, Vector3Add(Vector3Lerp(b0, b1, 0.5f), out), V3(0.01f, 0.018f, 0.014f), Flash((Color){ 60, 30, 20, 255 }, hf));   // insigne
    }

    // Chaine autour du cou (et en bandouliere pour le colosse et le chef), cadenas
    // Le mesh vient du cache (un seul appel de dessin pour toute la chaine)
    bool heavy = (e->type == E_TANK || e->type == E_BOSS);
    DrawPart(ChainModel(heavy ? CHAIN_HEAVY : CHAIN_LIGHT, f), body, Flash(STEEL, hf));
    if (heavy)
    {
        // Cadenas accroche en bas du collier (laiton pour le chef) : corps (pave) puis anse
        Vector3 n;
        Vector3 c = SurfaceAlong(shape, V3(0, 1.17f, 0), V3(0, 0, 1), &n);   // bas du collier
        Color lockC = Flash(e->type == E_BOSS ? BRASS : STEEL, hf);
        Box3(body, Vector3Add(c, Vector3Scale(n, 0.03f)), NOROT, V3(0.05f, 0.045f, 0.025f), lockC);
        Seg(body, Vector3Add(c, V3(0, 0.02f, 0.03f)), Vector3Add(c, V3(0, 0.055f, 0.03f)), 0.012f, Flash(STEEL, hf));   // anse
    }

    if (EnemyHoldsChick(e))
    {
        // Poussin dans la main gauche ; l'echelle 1/s annule celle du mercenaire
        Vector3 h = arms[1].hand;
        float sc = 1.1f/s;   // meme taille pour tous les mercenaires
        DrawChick(MatrixMultiply(MatrixMultiply(MatrixScale(sc, sc, sc), MatrixTranslate(h.x, h.y + 0.03f, h.z + 0.02f)), body),
                  e->grabT, 1.0f);
    }

    DrawHead(body, e, skin, hf);

    // Fleche de harpon plantee dans le ventre (taille independante du mercenaire)
    // (placee dans le dos, z < 0 : elle l'a traverse de part en part)
    if (e->launched)
        DrawSpear(MatrixMultiply(MatrixMultiply(MatrixScale(1.0f/s, 1.0f/s, 1.0f/s), MatrixTranslate(0, 1.2f, -0.28f*f)), body));
}

// Dessine tous les mercenaires actifs avec leur ombre au sol.
// Pas d'ombre pour un corps couche au sol (il est deja contre le sol).
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

// Bulles de paroles : pour chaque mercenaire qui parle, on projette un point
// au-dessus de sa tete a l'ecran et on y dessine une bulle blanche avec la
// phrase. La taille du texte diminue avec la distance (comme un objet 3D).
void EnemiesDrawSpeech(Camera3D cam, int w, int h)
{
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        const Enemy *e = &enemies[i];
        if (!e->active || e->talkT <= 0 || e->state == ES_DEAD) continue;
        Vector3 top = V3(e->pos.x, 2.08f*e->sz, e->pos.z);   // juste au-dessus des cheveux
        Vector3 to = Vector3Subtract(top, cam.position);
        float dist = Vector3Length(to);
        if (dist > 28.0f || Vector3DotProduct(to, fwd) < 0.3f*dist) continue;   // trop loin ou hors champ

        Vector2 sp = GetWorldToScreenEx(top, cam, w, h);
        int size = (int)Clamp(h*0.2f/dist, 10.0f, 30.0f);
        // Apparition et disparition en fondu (0.25 s)
        float a = Clamp(fminf(e->talkT, 2.6f - e->talkT)/0.25f, 0.0f, 1.0f);
        int tw = MeasureText(ENEMY_TAUNT, size);
        Rectangle box = { sp.x - tw/2.0f - size*0.5f, sp.y - size*2.2f, tw + size*1.0f, size*1.5f };
        DrawRectangleRounded(box, 0.5f, 8, Fade(RAYWHITE, 0.92f*a));
        DrawTriangle((Vector2){ sp.x - size*0.35f, box.y + box.height }, (Vector2){ sp.x, sp.y - size*0.2f },
                     (Vector2){ sp.x + size*0.35f, box.y + box.height }, Fade(RAYWHITE, 0.92f*a));   // pointe vers la bouche
        DrawText(ENEMY_TAUNT, (int)(box.x + size*0.5f), (int)(box.y + size*0.25f), size, Fade((Color){ 140, 20, 20, 255 }, a));
    }
}
