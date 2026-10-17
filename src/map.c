/*
 * map.c - Construction, rendu et collisions de l'arene.
 *
 * Repere : Y vers le haut, sol en y = 0, l'arene va de -ARENA a +ARENA sur
 * X et Z, le stand des poussins est au centre (0, 0).
 */
#include "map.h"
#include "render.h"
#include "util.h"

#include "raymath.h"

#include <math.h>

Box boxes[MAX_BOXES];
int boxCount;

//----------------------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------------------
// Ajoute une boite centree en (x, z) au sol, de taille sx * sy * sz
// (sy = hauteur). Ignoree silencieusement si le tableau est plein.
static void AddBox(float x, float z, float sx, float sy, float sz, BoxStyle style)
{
    if (boxCount >= MAX_BOXES) return;
    boxes[boxCount++] = (Box){ { V3(x - sx/2, 0, z - sz/2), V3(x + sx/2, sy, z + sz/2) }, style };
}

// Cree toute la carte : comptoir, enceinte et obstacles. Les positions sont
// symetriques par rapport au centre pour qu'aucun cote ne soit avantage.
void MapBuild(void)
{
    boxCount = 0;

    // Comptoir du stand des poussins (toujours en premier)
    AddBox(0, 0, 4.4f, 1.1f, 1.6f, BX_COUNTER);

    // Murs exterieurs avec une porte au milieu de chaque cote
    // gate = largeur de la porte, th = epaisseur du mur, h = hauteur.
    // Chaque cote est fait de 2 segments : de gate/2 jusqu'a ARENA + th
    // (le "+ th" ferme les coins). segC = position du centre d'un segment.
    const float gate = 5.0f, th = 1.0f, h = 4.0f;
    float segLen = ARENA - gate/2 + th;
    float segC = gate/2 + segLen/2;
    for (int s = -1; s <= 1; s += 2)   // s = -1 puis +1 : les deux moities de chaque cote
    {
        AddBox(s*segC, -ARENA - th/2, segLen, h, th, BX_WALL);   // mur nord (z negatif)
        AddBox(s*segC, ARENA + th/2, segLen, h, th, BX_WALL);    // mur sud (z positif)
        AddBox(-ARENA - th/2, s*segC, th, h, segLen, BX_WALL);   // mur ouest (x negatif)
        AddBox(ARENA + th/2, s*segC, th, h, segLen, BX_WALL);    // mur est (x positif)
    }

    // Camp militaire (placement symetrique ; les allees qui relient les 4
    // portes au stand, le long des axes X et Z, restent degagees).
    // Arguments : x, z, largeur (X), hauteur, profondeur (Z), style.
    // Grandes tentes : couvert complet (3,2 m de haut).
    AddBox(18, -19, 7.0f, 3.2f, 5.0f, BX_TENT);
    AddBox(-18, 19, 7.0f, 3.2f, 5.0f, BX_TENT);
    AddBox(-18, -19, 5.0f, 3.2f, 7.0f, BX_TENT);
    AddBox(18, 19, 6.0f, 3.0f, 5.0f, BX_TENT_GREEN);
    AddBox(22, -7, 5.0f, 3.0f, 4.0f, BX_TENT);
    AddBox(-22, 7, 5.0f, 3.0f, 4.0f, BX_TENT);
    // Murets de sacs de sable : 1 m, on tire par-dessus
    AddBox(-7.5f, 7.5f, 3.0f, 1.0f, 0.7f, BX_SANDBAGS);
    AddBox(7.5f, -7.5f, 3.0f, 1.0f, 0.7f, BX_SANDBAGS);
    AddBox(14, -13, 0.7f, 1.0f, 3.5f, BX_SANDBAGS);
    AddBox(-14, 13, 0.7f, 1.0f, 3.5f, BX_SANDBAGS);
    AddBox(-13, -14, 3.5f, 1.0f, 0.7f, BX_SANDBAGS);
    AddBox(13, 14, 3.5f, 1.0f, 0.7f, BX_SANDBAGS);
    // Caisses militaires
    AddBox(7, 7, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(8.4f, 6.2f, 1.2f, 2.0f, 1.2f, BX_CRATE);
    AddBox(-7, -7, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(-8.5f, -6.3f, 1.2f, 2.0f, 1.2f, BX_CRATE);
    AddBox(4, -15, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(-4, 15, 1.6f, 1.0f, 1.6f, BX_CRATE);
    AddBox(25, 6, 1.4f, 1.0f, 1.4f, BX_CRATE);
    AddBox(-25, -6, 1.4f, 1.0f, 1.4f, BX_CRATE);
}

//----------------------------------------------------------------------------------
// Collisions
//----------------------------------------------------------------------------------
// Collision cercle / rectangle vue de dessus (plan XZ) :
//  1. on cherche le point du rectangle le plus proche du centre (Clamp) ;
//  2. si ce point est a moins de "radius", on pousse le centre dans la
//     direction opposee juste assez pour que la distance vaille radius.
// Les boites sont traitees une par une (suffisant pour des obstacles espaces).
void ResolveCircle(Vector3 *p, float radius, float feetY, float height)
{
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        // Pas de collision si on est au-dessus de la boite (marge de 5 cm pour
        // pouvoir marcher dessus) ou si la boite est entierement au-dessus de nous.
        if (feetY >= b.max.y - 0.05f || feetY + height <= b.min.y) continue;
        // (cx, cz) : point de la boite le plus proche du centre du cercle
        float cx = Clamp(p->x, b.min.x, b.max.x);
        float cz = Clamp(p->z, b.min.z, b.max.z);
        float dx = p->x - cx, dz = p->z - cz;
        float d2 = dx*dx + dz*dz;   // distance au carre (evite une racine si pas de contact)
        if (d2 >= radius*radius) continue;
        if (d2 > 1e-8f)
        {
            // (dx, dz)/d = direction unitaire de sortie ; on recule de (radius - d)
            float d = sqrtf(d2);
            p->x += dx/d*(radius - d);
            p->z += dz/d*(radius - d);
        }
        else
        {
            // Centre a l'interieur : sortie par la face la plus proche
            // l, r, n, f = distances aux faces gauche, droite, proche (near), loin (far)
            float l = p->x - b.min.x, r = b.max.x - p->x, n = p->z - b.min.z, f = b.max.z - p->z;
            float m = fminf(fminf(l, r), fminf(n, f));
            if (m == l) p->x = b.min.x - radius;
            else if (m == r) p->x = b.max.x + radius;
            else if (m == n) p->z = b.min.z - radius;
            else p->z = b.max.z + radius;
        }
    }
}

// Renvoie la hauteur du "sol" sous les pieds : 0, ou le dessus de la plus
// haute caisse que l'on chevauche et dont le dessus n'est pas plus de 30 cm
// au-dessus des pieds (permet de monter sur une caisse en sautant, sans etre
// teleporte sur une caisse haute en la frolant). On utilise 70 % du rayon
// pour ne pas "tenir" sur le bord d'une caisse avec un seul orteil.
float GroundHeightAt(Vector3 p, float radius, float feetY)
{
    float g = 0.0f;
    float r = radius*0.7f;
    for (int i = 0; i < boxCount; i++)
    {
        BoundingBox b = boxes[i].b;
        // Le carre de cote 2r autour de p ne chevauche pas la boite : on passe
        if (p.x + r < b.min.x || p.x - r > b.max.x || p.z + r < b.min.z || p.z - r > b.max.z) continue;
        if (feetY >= b.max.y - 0.3f && b.max.y > g) g = b.max.y;
    }
    return g;
}

// Projete p sur le rectangle du comptoir (boxes[0]) vu de dessus : c'est la
// que les mercenaires se dirigent pour voler un poussin.
Vector3 CounterPoint(Vector3 p)
{
    BoundingBox b = boxes[0].b;
    return V3(Clamp(p.x, b.min.x, b.max.x), 0, Clamp(p.z, b.min.z, b.max.z));
}

// Lance un rayon et garde l'impact le plus proche parmi le sol et les boites.
bool MapRaycast(Ray ray, float *dist, Vector3 *normal)
{
    bool hit = false;
    // Sol (plan y = 0) : seulement si le rayon descend. On resout
    // position.y + t*direction.y = 0, donc t = -position.y / direction.y.
    if (ray.direction.y < -1e-4f)
    {
        float t = -ray.position.y/ray.direction.y;
        if (t < *dist) { *dist = t; *normal = V3(0, 1, 0); hit = true; }
    }
    // Boites : raylib fournit le test rayon / AABB (distance et normale de la face touchee)
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
// Caisse militaire (vert olive) : c = centre, s = taille.
static void DrawCrate(Vector3 c, Vector3 s)
{
    // Boite sombre + trois boites claires legerement en retrait = cadre sur chaque face
    // Chaque boite claire depasse de 2 mm (0.004 / 2) sur un axe et est plus
    // petite de 2*b sur les deux autres : sur les faces de cet axe, on voit un
    // panneau clair entoure d'un cadre sombre de largeur b.
    const Color dark = { 52, 60, 40, 255 }, light = { 82, 92, 62, 255 };
    const float b = 0.09f;   // largeur du cadre (9 cm)
    Matrix I = MatrixIdentity();
    Box3(I, c, NOROT, s, dark);
    Box3(I, c, NOROT, V3(s.x + 0.004f, s.y - 2*b, s.z - 2*b), light);   // panneaux des faces X
    Box3(I, c, NOROT, V3(s.x - 2*b, s.y + 0.004f, s.z - 2*b), light);   // panneaux dessus / dessous
    Box3(I, c, NOROT, V3(s.x - 2*b, s.y - 2*b, s.z + 0.004f), light);   // panneaux des faces Z
}

// Sacs de sable empiles en quinconce sur le volume s (centre c) : rangees
// de 25 cm, sacs de ~55 cm decales d'un demi-sac une rangee sur deux.
static void DrawSandbags(Vector3 c, Vector3 s)
{
    Matrix I = MatrixIdentity();
    bool alongX = s.x >= s.z;                  // sens de la longueur du muret
    float len = alongX ? s.x : s.z, depth = alongX ? s.z : s.x;
    int rows = (int)roundf(s.y/0.25f);
    for (int r = 0; r < rows; r++)
    {
        float y = 0.125f + r*0.25f;
        int n = (int)ceilf(len/0.55f);
        float off = (r % 2) ? 0.275f : 0.0f;
        for (int i = 0; i < n; i++)
        {
            float t = -len/2 + 0.275f + i*0.55f + off;
            if (t > len/2 - 0.2f) continue;
            // Teinte un peu differente selon le sac (toile plus ou moins delavee)
            int k = (r*7 + i*3) % 4;
            Color bag = { (unsigned char)(150 + k*8), (unsigned char)(132 + k*7), (unsigned char)(96 + k*5), 255 };
            Vector3 p = alongX ? V3(c.x + t, y, c.z) : V3(c.x, y, c.z + t);
            Vector3 rad = alongX ? V3(0.3f, 0.14f, depth*0.55f) : V3(depth*0.55f, 0.14f, 0.3f);
            Ball(I, p, rad, bag);
        }
    }
}

// Grande tente militaire (voir photos du camp) : toile, toit a deux pans,
// porte sombre du cote du centre de l'arene, dalle de beton, sacs de sable
// au pied et, pour les tentes beiges, armature exterieure en tubes noirs.
static void DrawTent(Vector3 c, Vector3 s, bool green)
{
    Matrix I = MatrixIdentity();
    const Color canvas = green ? (Color){ 74, 84, 60, 255 } : (Color){ 196, 168, 120, 255 };
    const Color frame = { 26, 26, 28, 255 };
    bool alongX = s.x >= s.z;                      // le faitage suit la plus grande dimension
    float len = alongX ? s.x : s.z, wid = alongX ? s.z : s.x;
    float wallH = s.y*0.6f, rise = s.y - wallH;    // murs droits puis toit
    float w = wid/2 + 0.12f;                        // demi-largeur du toit (leger debord)

    // Dalle de beton et murs de toile
    Box3(I, V3(c.x, 0.02f, c.z), NOROT, V3(s.x + 1.2f, 0.04f, s.z + 1.2f), (Color){ 140, 140, 136, 255 });
    Box3(I, V3(c.x, wallH/2, c.z), NOROT, V3(s.x, wallH, s.z), canvas);

    // Toit + pignons : un prisme "losange" (cube tourne de 45 degres autour du
    // faitage) ecrase en hauteur ; sa moitie basse est cachee dans les murs.
    Matrix roof = MatrixMultiply(MatrixScale(1, rise/w, 1), MatrixTranslate(c.x, wallH, c.z));
    if (alongX) Box3(roof, V3(0, 0, 0), V3(PI/4, 0, 0), V3(len + 0.2f, w*1.4142f, w*1.4142f), Shade(canvas, 0.95f));
    else        Box3(roof, V3(0, 0, 0), V3(0, 0, PI/4), V3(w*1.4142f, w*1.4142f, len + 0.2f), Shade(canvas, 0.95f));

    // Porte (rabat ouvert, interieur sombre) sur la face qui regarde le centre
    float side = alongX ? (c.z > 0 ? -1.0f : 1.0f) : (c.x > 0 ? -1.0f : 1.0f);
    if (alongX) Box3(I, V3(c.x, 0.95f, c.z + side*(wid/2 + 0.01f)), NOROT, V3(1.6f, 1.9f, 0.04f), (Color){ 30, 26, 22, 255 });
    else        Box3(I, V3(c.x + side*(wid/2 + 0.01f), 0.95f, c.z), NOROT, V3(0.04f, 1.9f, 1.6f), (Color){ 30, 26, 22, 255 });

    // Armature exterieure : un arceau tous les ~1,8 m (pieds plantes a 40 cm
    // des murs, montee jusqu'au bord du toit, puis jusqu'au faitage)
    if (!green)
    {
        int arches = (int)(len/1.8f) + 1;
        for (int i = 0; i < arches; i++)
        {
            float t = -len/2 + 0.15f + i*(len - 0.3f)/(arches - 1);
            for (int k = -1; k <= 1; k += 2)
            {
                Vector3 foot, eave, ridge;
                if (alongX)
                {
                    foot = V3(c.x + t, 0, c.z + k*(wid/2 + 0.4f));
                    eave = V3(c.x + t, wallH + 0.05f, c.z + k*(wid/2 + 0.12f));
                    ridge = V3(c.x + t, s.y + 0.08f, c.z);
                }
                else
                {
                    foot = V3(c.x + k*(wid/2 + 0.4f), 0, c.z + t);
                    eave = V3(c.x + k*(wid/2 + 0.12f), wallH + 0.05f, c.z + t);
                    ridge = V3(c.x, s.y + 0.08f, c.z + t);
                }
                Seg(I, foot, eave, 0.05f, frame);
                Seg(I, eave, ridge, 0.05f, frame);
            }
        }
    }
    // Sacs de sable aux quatre coins pour lester la toile
    for (int k = 0; k < 4; k++)
    {
        float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
        Ball(I, V3(c.x + sx*(s.x/2 + 0.25f), 0.1f, c.z + sz*(s.z/2 + 0.25f)), V3(0.3f, 0.12f, 0.22f), (Color){ 158, 138, 100, 255 });
    }
}

// Panneau "SORTIE" bleu a cote d'une porte : poteau + fleche (corps + pointe).
// (x, z) : pied du poteau ; dir = +1/-1 sens de la fleche ; alongX : fleche dans l'axe X.
static void DrawExitSign(float x, float z, float dir, bool alongX)
{
    Matrix I = MatrixIdentity();
    const Color blue = { 20, 80, 190, 255 }, white = { 240, 240, 240, 255 };
    Seg(I, V3(x, 0, z), V3(x, 1.5f, z), 0.03f, blue);
    Vector3 size = alongX ? V3(1.1f, 0.32f, 0.04f) : V3(0.04f, 0.32f, 1.1f);
    Vector3 border = alongX ? V3(1.16f, 0.38f, 0.03f) : V3(0.03f, 0.38f, 1.16f);
    Box3(I, V3(x, 1.6f, z), NOROT, border, white);
    Box3(I, V3(x, 1.6f, z), NOROT, size, blue);
    // Pointe de la fleche : petit carre tourne de 45 degres au bout du panneau
    if (alongX) Box3(I, V3(x + dir*0.6f, 1.6f, z), V3(0, 0, PI/4), V3(0.24f, 0.24f, 0.045f), blue);
    else        Box3(I, V3(x, 1.6f, z + dir*0.6f), V3(PI/4, 0, 0), V3(0.045f, 0.24f, 0.24f), blue);
    // Ecriture suggeree par une bande blanche (le texte serait illisible a cette taille)
    Box3(I, V3(x, 1.6f, z), NOROT, alongX ? V3(0.7f, 0.06f, 0.05f) : V3(0.05f, 0.06f, 0.7f), white);
}

// Cypres derriere les murs : tronc + deux fuseaux vert sombre
static void DrawCypress(float x, float z, float h)
{
    Matrix I = MatrixIdentity();
    Seg(I, V3(x, 0, z), V3(x, 1.0f, z), 0.18f, (Color){ 60, 44, 30, 255 });
    Ball(I, V3(x, h*0.45f, z), V3(0.9f, h*0.42f, 0.9f), (Color){ 28, 48, 30, 255 });
    Ball(I, V3(x, h*0.75f, z), V3(0.55f, h*0.3f, 0.55f), (Color){ 34, 56, 34, 255 });
}

// Stand des poussins : comptoir raye, auvent sur 4 poteaux, et enseigne en
// forme de poussin geant qui tourne (time = temps ecoule, pour l'animation).
static void DrawStand(Vector3 c, Vector3 s, float time)
{
    Matrix I = MatrixIdentity();
    const Color red = { 200, 35, 35, 255 }, white = { 245, 245, 240, 255 };

    // Comptoir raye rouge et blanc
    // 5 bandes blanches de 44 cm espacees de 88 cm, legerement plus grandes
    // que le comptoir pour ressortir par-dessus le rouge ; puis un plateau gris.
    Box3(I, c, NOROT, s, red);
    for (int i = 0; i < 5; i++)
        Box3(I, V3(-1.76f + i*0.88f, 0.55f, c.z), NOROT, V3(0.44f, 1.104f, s.z + 0.004f), white);
    Box3(I, V3(c.x, s.y + 0.04f, c.z), NOROT, V3(s.x + 0.2f, 0.08f, s.z + 0.2f), (Color){ 220, 220, 225, 255 });

    // Poteaux et toit
    // Les bits de k choisissent le coin : bit 0 -> x = +/-2.1, bit 1 -> z = +/-0.7
    for (int k = 0; k < 4; k++)
    {
        float px = (k & 1) ? 2.1f : -2.1f, pz = (k & 2) ? 0.7f : -0.7f;
        Seg(I, V3(px, 1.1f, pz), V3(px, 3.0f, pz), 0.05f, (Color){ 200, 200, 205, 255 });
    }
    // Toit en 6 bandes alternees rouge / blanc
    for (int i = 0; i < 6; i++)
        Box3(I, V3(-2.3f + i*0.92f + 0.46f, 3.05f, 0), NOROT, V3(0.92f, 0.12f, 2.2f), (i % 2) ? white : red);

    // Poussin geant tournant en enseigne
    // Matrice : echelle x7, rotation autour de Y (0.8 rad/s), puis montee a 4.1 m
    Seg(I, V3(0, 3.1f, 0), V3(0, 4.2f, 0), 0.08f, (Color){ 120, 120, 125, 255 });
    DrawChick(MatrixMultiply(MatrixMultiply(MatrixScale(7, 7, 7), MatrixRotateY(time*0.8f)), MatrixTranslate(0, 4.1f, 0)), time, 0.25f);
}

// Dessine une boite selon son style. c = centre, s = taille (calcules depuis min / max).
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
        case BX_SANDBAGS:
            DrawSandbags(c, s);
            break;
        case BX_WALL:
            // Gabions : cages de grillage remplies de pierres (motif du shader)
            RenderSetSurface(SURF_GABION);
            Box3(I, c, NOROT, s, (Color){ 160, 160, 156, 255 });
            RenderSetSurface(SURF_PLAIN);
            break;
        case BX_TENT:
        case BX_TENT_GREEN:
            DrawTent(c, s, bx->style == BX_TENT_GREEN);
            break;
        case BX_COUNTER:
            DrawStand(c, s, time);
            break;
    }
}

// Les poussins sautillent sur le comptoir, chacun a son rythme
// Disposition : 2 rangees de 6 (i/6 = rangee, i%6 = colonne), espaces de 70 cm.
// Le decalage i*1.37 desynchronise les animations des poussins.
static void DrawStandChicks(int chicks, float time)
{
    for (int i = 0; i < chicks; i++)
    {
        int row = i/6, col = i % 6;
        float t = time + i*1.37f;
        float x = -1.75f + col*0.7f + 0.06f*sinf(t*0.9f);   // petit balancement lateral
        float z = (row == 0) ? 0.35f : -0.35f;
        // Petits sauts (5 cm max) seulement pendant certaines periodes : le
        // facteur (sinf(t*0.7) > 0.3) vaut 0 ou 1 et coupe les sauts le reste du temps.
        float hop = fmaxf(0.0f, sinf(t*5.0f + i))*0.05f*(sinf(t*0.7f) > 0.3f);
        // Rangee avant regarde vers +Z, rangee arriere vers -Z (PI), plus une
        // rotation de la tete qui oscille (+/- 0.8 rad).
        float yaw = (row == 0 ? 0.0f : PI) + 0.8f*sinf(t*0.45f + i*2.1f);
        // 1.18 m : dessus du comptoir ; les ailes battent pendant un saut
        DrawChick(MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(x, 1.18f + hop, z)), t, hop > 0.01f ? 0.4f : 0.0f);
    }
}

// Dessine toute la carte. time = temps (animations), chicks = poussins restants au stand.
void MapDraw(float time, int chicks)
{
    Matrix I = MatrixIdentity();

    // Sol : un grand cube dont le shader dessine le gravier et l'herbe
    // 240 x 240 m, dessus en y = 0 (centre a -0.5, epaisseur 1 m)
    RenderSetSurface(SURF_GROUND);
    Box3(I, V3(0, -0.5f, 0), NOROT, V3(240, 1, 240), (Color){ 140, 138, 134, 255 });
    RenderSetSurface(SURF_PLAIN);

    for (int i = 0; i < boxCount; i++) DrawBox(&boxes[i], time);
    DrawStandChicks(chicks, time);

    // Grands lampadaires (mats fins de 8 m), un dans chaque coin de l'arene,
    // sans collision. La lumiere qu'ils donnent est calculee par le shader
    // (LAMPS dans render.c) ; l'ampoule est dessinee "emissive" (elle brille).
    for (int k = 0; k < 4; k++)
    {
        float x = (k & 1) ? 26.0f : -26.0f, z = (k & 2) ? 26.0f : -26.0f;
        Seg(I, V3(x, 0, z), V3(x, 8, z), 0.09f, (Color){ 96, 98, 102, 255 });
        Box3(I, V3(x, 8.05f, z), NOROT, V3(0.6f, 0.16f, 0.36f), (Color){ 70, 72, 76, 255 });
        RenderSetEmissive(true);
        Box3(I, V3(x, 7.95f, z), NOROT, V3(0.42f, 0.05f, 0.24f), (Color){ 255, 236, 190, 255 });
        RenderSetEmissive(false);
    }

    // Panneaux SORTIE a cote des quatre portes (cote droit en entrant)
    DrawExitSign(3.6f, -28.6f, 1, true);
    DrawExitSign(-3.6f, 28.6f, -1, true);
    DrawExitSign(28.6f, 3.6f, 1, false);
    DrawExitSign(-28.6f, -3.6f, -1, false);

    // Cypres derriere les murs (tous les 7 m, sauf devant les portes)
    for (int i = -4; i <= 4; i++)
    {
        if (i == 0) continue;
        float t = i*7.0f, h = 7.0f + 2.0f*sinf(i*1.7f);
        DrawCypress(t, -ARENA - 4.5f, h);
        DrawCypress(t, ARENA + 4.5f, h + 0.5f);
        DrawCypress(-ARENA - 4.5f, t, h - 0.5f);
        DrawCypress(ARENA + 4.5f, t, h);
    }
    // Ampoule du stand, sous le toit, juste au-dessus des poussins
    RenderSetEmissive(true);
    Ball(I, V3(0, 2.9f, 0), V3(0.12f, 0.1f, 0.12f), (Color){ 255, 230, 160, 255 });
    RenderSetEmissive(false);
}
