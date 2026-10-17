/*
 * weapon.c - Logique et rendu des armes : HK416 et harpon.
 *
 * Le modele de l'arme est defini dans son propre repere : origine au
 * centre du boitier, canon vers -Z, +Y vers le haut. Le meme modele sert
 * pour la vue a la premiere personne et pour le coequipier vu de dehors.
 *
 * Organisation :
 *   - position de l'arme a l'ecran (hanche / visee, balancement, recul...)
 *   - logique de tir (lancer de rayon + dispersion) et de rechargement
 *   - modeles 3D (HK416, harpon, fleche, mains)
 *   - passe de rendu "view model" : l'arme est dessinee dans un espace vue
 *     separe (camera a l'origine), par-dessus la scene, pour qu'elle ne
 *     rentre jamais dans les murs quand on s'en approche.
 */
#include "weapon.h"
#include "player.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "audio.h"
#include "render.h"
#include "hud.h"
#include "net.h"
#include "harpoon.h"
#include "util.h"

#include "raymath.h"

#include <math.h>

// Points remarquables du modele, dans le repere de l'arme (metres)
#define MUZZLE_Z      -0.68f   // bout du cache-flamme du HK416
#define SIGHT_HEIGHT  0.085f   // axe du viseur au-dessus de l'origine
#define SIGHT_Z       -0.04f
#define SPEAR_Y       0.04f     // fleche posee sur le dessus du harpon
#define SPEAR_TIP_Z   -0.97f    // pointe de la fleche quand le harpon est charge

// Palette des armes
static const Color METAL = { 38, 38, 36, 255 };     // acier sombre
static const Color METAL2 = { 55, 54, 50, 255 };    // acier un peu plus clair (contraste entre pieces)
static const Color POLYMER = { 30, 30, 30, 255 };   // plastique noir (poignee, crosse)

// Etat de depart : HK416 en main, chargeur plein, harpon charge
void WeaponReset(Weapon *w)
{
    *w = (Weapon){ 0 };       // tout a zero (pas de recul, pas de visee...)
    w->active = WPN_RIFLE;
    w->ammo = MAG_SIZE;
    w->reserve = START_RESERVE;
    w->harpoonLoaded = true;
    w->harpoons = HARPOON_START;
}

//----------------------------------------------------------------------------------
// Position de l'arme a l'ecran
//----------------------------------------------------------------------------------
// Position de l'arme par rapport a la camera, en espace vue (x = droite,
// y = haut, -z = devant). Melange : position hanche/visee, balancement de
// marche, sway, recul (kick), arme baissee en sprint ou pendant un changement.
static Vector3 WeaponOffset(const Player *p)
{
    const Weapon *w = &p->w;
    bool harpoon = (w->active == WPN_HARPOON);
    // A la hanche : arme en bas a droite de l'ecran
    Vector3 hip = harpoon ? V3(0.16f, -0.17f, -0.42f) : V3(0.17f, -0.19f, -0.45f);
    // En visant : point rouge (ou fleche du harpon) exactement au centre
    Vector3 ads = harpoon ? V3(0.0f, -SPEAR_Y - 0.025f, -0.30f) : V3(0.0f, -SIGHT_HEIGHT, -0.33f);
    // Interpolation selon w->ads : transition fluide entre hanche et visee
    Vector3 o = Vector3Lerp(hip, ads, w->ads);
    // Balancement de marche (suit p->bob) : petit "8" horizontal + rebond
    // vertical, reduit de 85% en visant pour garder la mire stable
    float bobAmt = 1.0f - w->ads*0.85f;
    o.x += sinf(p->bob)*0.012f*bobAmt + w->sway.x;
    o.y += -fabsf(cosf(p->bob))*0.012f*bobAmt + w->sway.y;
    o.z += w->kick*0.06f;                          // recul : l'arme revient vers l'epaule
    o.y -= w->sprintT*0.06f + w->switchT*0.25f;    // baissee en sprint / hors ecran au changement
    o.x -= w->sprintT*0.04f;
    return o;
}

// Repere de l'arme en espace vue (camera a l'origine, regardant vers -Z)
// = rotation (recul, rechargement, sprint, changement) puis translation (WeaponOffset).
static Matrix WeaponViewMatrix(const Player *p)
{
    const Weapon *w = &p->w;
    Vector3 o = WeaponOffset(p);
    // reloadTilt : 0 -> 1 -> 0 en forme de bosse (sinus sur une demi-periode)
    // pendant le rechargement : l'arme s'incline puis revient en place.
    float reloadTilt = 0.0f;
    if (w->active == WPN_RIFLE && w->reloading) reloadTilt = sinf(fminf(w->reloadT/w->reloadDur, 1.0f)*PI);
    if (w->active == WPN_HARPOON && w->harpoonT > 0) reloadTilt = 0.6f*sinf((1.0f - w->harpoonT/HARPOON_RELOAD)*PI);
    // Angles (radians) autour de X (canon qui monte/descend), Y (vers la gauche)
    // et Z (roulis, arme penchee sur le cote)
    Vector3 rot = V3(w->kick*0.12f - reloadTilt*0.25f - w->sprintT*0.25f - w->switchT*0.6f,
                     w->sprintT*0.7f + reloadTilt*0.2f,
                     reloadTilt*0.5f + w->sprintT*0.35f);
    return MatrixMultiply(MatrixRotateXYZ(rot), MatrixTranslate(o.x, o.y, o.z));
}

// Bout du canon (ou pointe de la fleche) en espace vue
static Vector3 MuzzleView(const Player *p)
{
    Vector3 tip = (p->w.active == WPN_HARPOON) ? V3(0, SPEAR_Y, SPEAR_TIP_Z) : V3(0, 0, MUZZLE_Z);
    return Vector3Transform(tip, WeaponViewMatrix(p));
}

// Convertit un point de l'espace vue (camera du joueur) vers le monde.
// On reconstruit la base de la camera : f = devant, r = droite, u = haut.
// En espace vue, -z est devant, d'ou le "f * -v.z".
static Vector3 ViewToWorld(const Player *p, Vector3 v)
{
    Vector3 f = PlayerForward(p);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);
    Vector3 eye = PlayerEye(p);
    return Vector3Add(eye, Vector3Add(Vector3Scale(r, v.x), Vector3Add(Vector3Scale(u, v.y), Vector3Scale(f, -v.z))));
}

// Le canon est place en espace vue (pour suivre l'animation a l'ecran) puis
// ramene dans le monde : les tracantes partent bien du canon visible.
Vector3 WeaponMuzzleWorld(const Player *p) { return ViewToWorld(p, MuzzleView(p)); }

// Dispersion des balles : rayon du cone de tir (en ~radians). Elle depend de :
//   - la visee : 0.022 a la hanche -> 0.002 en visant,
//   - la vitesse de deplacement (courir rend imprecis),
//   - le saut (+0.04 en l'air),
//   - le "bloom" qui grossit a chaque tir et redescend avec le temps.
// Le reticule du HUD utilise aussi cette valeur pour s'ecarter.
float WeaponSpread(const Player *p)
{
    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    return Lerp(0.022f, 0.002f, p->w.ads) + hspeed*0.003f + (p->grounded ? 0.0f : 0.04f) + p->w.bloom;
}

//----------------------------------------------------------------------------------
// Tir et rechargement
//----------------------------------------------------------------------------------
// Lance un rechargement du HK416 si c'est utile et possible
static void StartReload(Weapon *w)
{
    // Deja en cours, plus de reserve, ou chargeur deja plein : rien a faire
    if (w->reloading || w->reserve <= 0 || w->ammo >= MAG_SIZE) return;
    w->reloading = true;
    w->reloadT = 0;
    w->reloadStep = 0;
    // Chargeur vide : plus long, il faut en plus actionner la culasse
    w->reloadDur = (w->ammo == 0) ? RELOAD_EMPTY : RELOAD_TIME;
}

// Fin du rechargement : transfert des balles de la reserve vers le chargeur
static void FinishReload(Weapon *w)
{
    int chamber = (w->ammo > 0) ? 1 : 0;   // une balle reste chambree sur un rechargement tactique
    int need = MAG_SIZE + chamber - w->ammo;   // ex. 30 + 1 : on peut avoir 31 balles
    // On ne prend pas plus que ce qui reste en reserve
    int take = (need < w->reserve) ? need : w->reserve;
    w->ammo += take;
    w->reserve -= take;
    w->reloading = false;
}

// Tire une balle de HK416 : munitions, recul, son, puis lancer de rayon
// (hitscan : la balle touche instantanement) et application des degats.
static void Shoot(Player *p)
{
    Weapon *w = &p->w;
    w->ammo--;
    w->fireT = FIRE_DELAY;      // cadence : prochain tir possible dans FIRE_DELAY s
    w->muzzleT = 0.05f;         // flash de bouche pendant 50 ms
    w->kick = 1.0f;             // animation de recul de l'arme
    // Recul : le canon monte (moins en visant) et devie un peu au hasard a
    // gauche/droite. Il redescend tout seul dans WeaponUpdate.
    w->recoilPitch += Randf(0.010f, 0.016f)*(1.0f - w->ads*0.35f);
    w->recoilYaw += Randf(-0.006f, 0.006f);
    w->bloom = fminf(w->bloom + 0.006f, 0.04f);   // rafales = moins precis (plafonne)
    w->sprintT = 0.0f;
    p->shake = fmaxf(p->shake, 0.04f);
    SfxWorld(SND_SHOT, Randf(0.92f, 1.08f), 0.55f);

    // Direction du tir = regard + dispersion aleatoire dans un cone
    Vector3 eye = PlayerEye(p);
    Vector3 f = PlayerForward(p);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);
    // Angle aleatoire autour de l'axe + ecart aleatoire (0..spread) : on
    // decale f dans le plan (r, u) perpendiculaire au regard
    float ang = Randf(0, 2*PI), mag = Randf(0, 1)*WeaponSpread(p);
    Vector3 dir = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(r, cosf(ang)*mag), Vector3Scale(u, sinf(ang)*mag))));
    Ray ray = { eye, dir };

    // Portee max 250 m. MapRaycast raccourcit dist jusqu'au decor touche ;
    // EnemyRaycast ne renvoie un mercenaire que s'il est avant ce point
    // (on ne tire pas a travers les murs). -1 = aucun mercenaire touche.
    float dist = 250.0f;
    Vector3 normal = V3(0, 1, 0);
    bool hitWorld = MapRaycast(ray, &dist, &normal);
    bool head = false;
    int target = EnemyRaycast(ray, &dist, &head);

    // Point d'impact ; la tracante va du canon visible a ce point
    Vector3 hit = Vector3Add(eye, Vector3Scale(dir, dist));
    AddTracer(WeaponMuzzleWorld(p), hit);
    float dmg = BULLET_DAMAGE*(head ? HEADSHOT_MULT : 1.0f);
    if (target >= 0 && NetIsClient())
    {
        // En reseau, c'est l'hote qui applique les degats
        // Le client predit l'effet (sang, hitmarker) pour une reponse immediate
        EnemyPredictHit(&enemies[target], dmg, head, hit, dir, p);
        NetReportHit(target, dmg, head, hit, dir, HIT_BULLET);
    }
    else if (target >= 0) EnemyDamage(&enemies[target], dmg, head, hit, dir, p);   // solo / hote
    else if (hitWorld) BurstSparks(hit, normal);   // impact dans le decor : etincelles
}

// Harpon : un tir, puis rechargement automatique s'il reste des fleches
static void UpdateHarpoonGun(Player *p, float dt)
{
    Weapon *w = &p->w;
    const PlayerInput *in = &p->in;
    // Rechargement en cours : on attend la fin, aucune autre action possible
    if (w->harpoonT > 0)
    {
        w->harpoonT -= dt;
        if (w->harpoonT <= 0)
        {
            w->harpoonT = 0;
            w->harpoonLoaded = true;
            PlaySfx(SND_CLICK, 0.6f, 0.7f);
        }
        return;
    }
    // Vide mais des fleches en reserve : R ou gachette lance le rechargement
    if (!w->harpoonLoaded && w->harpoons > 0 && (in->reload || in->firePressed))
    {
        w->harpoons--;
        w->harpoonT = HARPOON_RELOAD;
        return;
    }
    // Tir au coup par coup (firePressed), pas pendant un changement d'arme ni en sprint
    if (!in->firePressed || w->switchT > 0.2f || p->sprinting) return;
    if (!w->harpoonLoaded) { PlaySfx(SND_EMPTY, 0.8f, 0.6f); return; }

    HarpoonFire(p);            // cree le projectile (harpoon.c)
    w->harpoonLoaded = false;
    w->kick = 1.0f;
    w->recoilPitch += 0.035f;  // gros recul fixe
    p->shake = fmaxf(p->shake, 0.12f);
    // Rechargement automatique de la fleche suivante
    if (w->harpoons > 0)
    {
        w->harpoons--;
        w->harpoonT = HARPOON_RELOAD;
    }
}

// Changement d'arme : bascule (E / molette / RB) ou choix direct (1 / 2).
// switchT passe a 1 puis redescend en SWITCH_TIME s : l'arme remonte a l'ecran.
static void UpdateWeaponSwitch(Player *p, float dt)
{
    Weapon *w = &p->w;
    WeaponId want = w->active;
    if (p->in.switchWeapon) want = (w->active == WPN_RIFLE) ? WPN_HARPOON : WPN_RIFLE;
    if (p->in.selectWeapon >= 0) want = (WeaponId)p->in.selectWeapon;
    if (want != w->active)
    {
        w->active = want;
        w->switchT = 1.0f;
        w->reloading = false;   // un rechargement en cours est annule
        PlaySfx(SND_CLICK, 0.8f, 0.6f);
    }
    w->switchT = fmaxf(0, w->switchT - dt/SWITCH_TIME);
}

// Mise a jour de l'arme d'un joueur local, chaque frame
void WeaponUpdate(Player *p, float dt)
{
    Weapon *w = &p->w;
    const PlayerInput *in = &p->in;

    // Retour progressif au repos : le recul et le bloom diminuent tout seuls.
    // "x -= x*fminf(1, dt*6)" = decroissance exponentielle (environ 6x par seconde)
    w->fireT -= dt;
    w->muzzleT -= dt;
    w->kick = fmaxf(0, w->kick - dt*12.0f);
    w->recoilPitch -= w->recoilPitch*fminf(1.0f, dt*6.0f);
    w->recoilYaw -= w->recoilYaw*fminf(1.0f, dt*6.0f);
    w->bloom = fmaxf(0, w->bloom - dt*0.08f);
    if (!p->alive) return;     // mort : on laisse juste les animations retomber

    UpdateWeaponSwitch(p, dt);
    // Visee et arme baissee en sprint : on tend vers 0 ou 1 en douceur
    bool aiming = in->aim && !w->reloading && !p->sprinting;
    w->ads += ((aiming ? 1.0f : 0.0f) - w->ads)*fminf(1.0f, dt*14.0f);
    w->sprintT += ((p->sprinting ? 1.0f : 0.0f) - w->sprintT)*fminf(1.0f, dt*10.0f);

    // L'arme "traine" legerement derriere les mouvements de camera
    w->sway.x += (in->look.x*0.18f - w->sway.x)*fminf(1.0f, dt*8.0f);
    w->sway.y += (-in->look.y*0.18f - w->sway.y)*fminf(1.0f, dt*8.0f);
    w->sway.x = Clamp(w->sway.x, -0.03f, 0.03f);
    w->sway.y = Clamp(w->sway.y, -0.03f, 0.03f);

    if (w->active == WPN_HARPOON) { UpdateHarpoonGun(p, dt); return; }

    // --- HK416 ---
    if (in->reload) StartReload(w);
    if (in->toggleMode)
    {
        w->semi = !w->semi;
        FeedAddLocal(SKYBLUE, "%sMode de tir : %s", PlayerTag(p), w->semi ? "semi-auto" : "automatique");
        PlaySfx(SND_CLICK, 1.4f, 0.5f);
    }

    if (w->reloading)
    {
        // Clics : chargeur retire, chargeur insere, culasse (si vide)
        // marks = instants (s) des clics ; le 3e (culasse) n'a lieu qu'a vide
        const float marks[3] = { 0.3f, 1.45f, 2.35f };
        int steps = (w->reloadDur > RELOAD_TIME) ? 3 : 2;
        w->reloadT += dt;
        if (w->reloadStep < steps && w->reloadT > marks[w->reloadStep])
        {
            PlaySfx(SND_CLICK, w->reloadStep == 2 ? 0.7f : 1.0f, 0.7f);
            w->reloadStep++;
        }
        if (w->reloadT >= w->reloadDur) FinishReload(w);
        return;                // pas de tir pendant le rechargement
    }

    // Semi-auto : une balle par appui (on attend le relachement) ; auto : tant que c'est enfonce
    if (!in->fire) w->shotThisPress = false;
    bool trigger = w->semi ? (in->fire && !w->shotThisPress) : in->fire;
    if (!trigger || w->fireT > 0 || p->sprinting || w->switchT > 0.2f) return;

    if (w->ammo > 0)
    {
        Shoot(p);
        w->shotThisPress = true;
    }
    else
    {
        // Chargeur vide : clic a vide puis rechargement automatique
        if (in->firePressed) PlaySfx(SND_EMPTY, 1.0f, 0.6f);
        w->fireT = 0.25f;
        StartReload(w);
    }
}

//----------------------------------------------------------------------------------
// Modele 3D
//----------------------------------------------------------------------------------
// Fonctions de dessin utilisees ici (render.c), toutes dans un repere "parent" :
//   Box3(repere, centre, rotation XYZ, taille, couleur)   boite
//   Ball(repere, centre, rayons, couleur)                 ellipsoide
//   Seg / Limb(repere, debut, fin, rayon, couleur)        cylindre (Limb : bouts arrondis)
// Position du chargeur pendant le rechargement (il sort puis rentre).
// Chronologie : 0-0.3 s il descend, 0.3-1.1 s cache (dans la main, hors
// arme), 1.1-1.45 s il remonte dans le puits. *visible = faut-il le dessiner.
static Vector3 MagazinePos(const Weapon *w, bool *visible)
{
    Vector3 pos = V3(0, -0.14f, -0.065f);
    *visible = true;
    if (w && w->reloading)
    {
        float t = w->reloadT;
        if (t < 0.3f) pos.y -= t/0.3f*0.12f;
        else if (t < 1.1f) { pos.y -= 0.4f; *visible = false; }
        else if (t < 1.45f) pos.y -= (1.0f - (t - 1.1f)/0.35f)*0.15f;
    }
    return pos;
}

// Flash de bouche : boule lumineuse + etoile de 4 branches, taille et
// rotation aleatoires a chaque frame pour un effet vivant.
static void DrawMuzzleFlash(Matrix frame)
{
    float k = 0.8f + Randf(0, 0.6f);   // facteur de taille aleatoire
    Matrix fm = MatrixMultiply(MatrixRotateZ(Randf(0, PI)), MatrixMultiply(MatrixTranslate(0, 0, MUZZLE_Z - 0.04f), frame));
    RenderSetEmissive(true);   // couleur pleine, non affectee par l'eclairage
    Ball(fm, V3(0, 0, 0), V3(0.035f*k, 0.035f*k, 0.06f*k), (Color){ 255, 240, 170, 255 });
    for (int i = 0; i < 4; i++)
        Box3(fm, V3(0, 0, -0.01f), V3(0, 0, i*PI/4), V3(0.12f*k, 0.012f, 0.012f), (Color){ 255, 180, 60, 255 });
    Ball(fm, V3(0, 0, -0.05f*k), V3(0.02f*k, 0.02f*k, 0.08f*k), (Color){ 255, 200, 90, 255 });
    RenderSetEmissive(false);
}

// Modele du HK416 dans le repere W0 (canon vers -Z). w = etat pour les
// animations (chargeur, flash) ; NULL pour une arme au repos.
void DrawHK416(Matrix W0, const Weapon *w)
{
    // Boitiers superieur / inferieur, fenetre d'ejection, levier d'armement
    Box3(W0, V3(0, 0, 0.0f), NOROT, V3(0.05f, 0.06f, 0.28f), METAL);
    Box3(W0, V3(0, -0.048f, 0.02f), NOROT, V3(0.045f, 0.045f, 0.2f), METAL2);
    Box3(W0, V3(0.026f, 0.005f, -0.02f), NOROT, V3(0.004f, 0.03f, 0.07f), (Color){ 25, 25, 25, 255 });
    Box3(W0, V3(0, 0.032f, 0.15f), NOROT, V3(0.04f, 0.012f, 0.025f), METAL2);

    // Poignee pistolet et pontet
    Box3(W0, V3(0, -0.11f, 0.085f), V3(-0.35f, 0, 0), V3(0.036f, 0.11f, 0.045f), POLYMER);
    Box3(W0, V3(0, -0.085f, 0.025f), NOROT, V3(0.012f, 0.008f, 0.07f), METAL);

    // Garde-main quad rail (signature du HK416) + rail picatinny superieur
    Box3(W0, V3(0, 0.0f, -0.29f), NOROT, V3(0.062f, 0.062f, 0.3f), METAL2);
    for (int i = 0; i < 4; i++)   // 4 rails a 90 deg autour du garde-main
    {
        float a = i*PI*0.5f;
        Box3(W0, V3(cosf(a)*0.036f, sinf(a)*0.036f, -0.29f), V3(0, 0, a), V3(0.008f, 0.02f, 0.29f), METAL);
    }
    Box3(W0, V3(0, 0.036f, -0.13f), NOROT, V3(0.024f, 0.01f, 0.6f), METAL);
    // Crans du rail picatinny
    for (int i = 0; i < 22; i++) Box3(W0, V3(0, 0.043f, 0.15f - i*0.026f), NOROT, V3(0.026f, 0.006f, 0.012f), METAL2);

    // Bloc de gaz (piston), canon, cache-flamme
    Box3(W0, V3(0, 0.012f, -0.475f), NOROT, V3(0.034f, 0.05f, 0.04f), METAL);
    Seg(W0, V3(0, 0, -0.44f), V3(0, 0, -0.62f), 0.011f, METAL);
    Seg(W0, V3(0, 0, -0.6f), V3(0, 0, MUZZLE_Z), 0.016f, (Color){ 20, 20, 20, 255 });

    // Tube de crosse et crosse
    Seg(W0, V3(0, 0.005f, 0.14f), V3(0, 0.005f, 0.34f), 0.016f, METAL);
    Box3(W0, V3(0, -0.02f, 0.3f), NOROT, V3(0.046f, 0.085f, 0.17f), POLYMER);
    Box3(W0, V3(0, -0.035f, 0.39f), NOROT, V3(0.05f, 0.12f, 0.02f), (Color){ 20, 20, 20, 255 });

    // Viseur point rouge : anneau ouvert pour pouvoir viser au travers
    // 8 petites plaques en cercle autour de l'axe de visee forment le tube
    const float R = 0.02f;     // rayon du tube du viseur
    Box3(W0, V3(0, 0.055f, SIGHT_Z), NOROT, V3(0.026f, 0.03f, 0.04f), METAL);
    for (int i = 0; i < 8; i++)
    {
        float a = i*PI/4;
        Box3(W0, V3(cosf(a)*R, SIGHT_HEIGHT + sinf(a)*R, SIGHT_Z), V3(0, 0, a - PI/2), V3(0.0175f, 0.004f, 0.06f), POLYMER);
    }
    Box3(W0, V3(0.023f, SIGHT_HEIGHT, SIGHT_Z), NOROT, V3(0.008f, 0.012f, 0.014f), POLYMER);   // molette
    // Le point rouge, sur l'axe de visee : centre exact de l'ecran en visant
    RenderSetEmissive(true);
    Ball(W0, V3(0, SIGHT_HEIGHT, SIGHT_Z - 0.03f), V3(0.0012f, 0.0012f, 0.0012f), (Color){ 255, 30, 30, 255 });
    RenderSetEmissive(false);

    // Chargeur courbe (anime pendant le rechargement)
    bool magVisible;
    Vector3 mag = MagazinePos(w, &magVisible);
    if (magVisible)
    {
        // Repere du chargeur : legerement incline vers l'avant, puis place sous l'arme
        Matrix mm = MatrixMultiply(MatrixMultiply(MatrixRotateX(0.18f), MatrixTranslate(mag.x, mag.y, mag.z)), W0);
        // Partie droite, partie courbee (2e boite inclinee), semelle
        Box3(mm, V3(0, 0.03f, 0), NOROT, V3(0.034f, 0.11f, 0.066f), METAL2);
        Box3(mm, V3(0, -0.04f, -0.008f), V3(0.2f, 0, 0), V3(0.034f, 0.07f, 0.066f), METAL2);
        Box3(mm, V3(0, -0.08f, -0.018f), V3(0.2f, 0, 0), V3(0.04f, 0.014f, 0.072f), POLYMER);
    }

    if (w && w->muzzleT > 0) DrawMuzzleFlash(W0);
}

// Fleche de harpon dans le repere "frame" (pointe a l'origine, vers -Z)
void DrawSpear(Matrix frame)
{
    // Pointe a l'origine, tige vers +Z
    const Color steel = { 190, 195, 200, 255 };
    Seg(frame, V3(0, 0, 0.02f), V3(0, 0, 1.15f), 0.0055f, steel);
    Ball(frame, V3(0, 0, 0.02f), V3(0.009f, 0.009f, 0.045f), (Color){ 220, 225, 230, 255 });
    for (int k = -1; k <= 1; k += 2)   // ardillons
        Box3(frame, V3(0.009f*k, 0, 0.07f), V3(0, 0.5f*k, 0), V3(0.004f, 0.004f, 0.04f), steel);
}

// Modele du fusil-harpon (meme convention que DrawHK416). Les elastiques et
// la fleche dependent de l'etat de chargement.
void DrawHarpoonGun(Matrix W0, const Weapon *w)
{
    const Color body = { 45, 50, 55, 255 }, orange = { 240, 120, 30, 255 };

    // Fut, crosse, poignee pistolet, poignee avant
    Seg(W0, V3(0, 0, 0.3f), V3(0, 0, -0.74f), 0.022f, body);
    Box3(W0, V3(0, -0.01f, 0.3f), NOROT, V3(0.045f, 0.07f, 0.12f), POLYMER);
    Box3(W0, V3(0, -0.035f, 0.05f), NOROT, V3(0.04f, 0.05f, 0.14f), body);
    Box3(W0, V3(0, -0.1f, 0.085f), V3(-0.35f, 0, 0), V3(0.036f, 0.11f, 0.045f), POLYMER);
    Box3(W0, V3(0, -0.085f, 0.025f), NOROT, V3(0.012f, 0.008f, 0.07f), METAL);
    Box3(W0, V3(0, -0.045f, -0.35f), NOROT, V3(0.03f, 0.06f, 0.06f), POLYMER);
    Box3(W0, V3(0, SPEAR_Y - 0.012f, -0.25f), NOROT, V3(0.016f, 0.012f, 0.95f), METAL);   // rail de la fleche

    // Tete du harpon et elastiques orange (tendus quand il est charge)
    Box3(W0, V3(0, 0.012f, -0.74f), NOROT, V3(0.075f, 0.05f, 0.05f), body);
    bool loaded = !w || w->harpoonLoaded;   // sans etat : dessine charge
    // Point d'accroche des elastiques : tires vers l'arriere si charge,
    // relaches pres de la tete sinon
    float notchZ = loaded ? -0.22f : -0.62f;
    for (int k = -1; k <= 1; k += 2)
        Limb(W0, V3(0.032f*k, 0.012f, -0.74f), V3(0.02f*k, SPEAR_Y + 0.006f, notchZ), 0.008f, orange);

    // Fleche chargee (pendant le rechargement, elle glisse depuis l'arriere)
    // slide = recul de la fleche vers l'arriere (m) ; -1 = pas de fleche
    // Elle n'apparait que dans les derniers 60% du rechargement (la main l'a
    // amenee), puis glisse jusqu'a sa place quand harpoonT atteint 0.
    float slide = -1.0f;
    if (loaded) slide = 0.0f;
    else if (w && w->harpoonT > 0 && w->harpoonT < HARPOON_RELOAD*0.6f) slide = w->harpoonT*1.4f;
    if (slide >= 0) DrawSpear(MatrixMultiply(MatrixTranslate(0, SPEAR_Y, SPEAR_TIP_Z + slide), W0));
}

// Mains gantees et avant-bras du joueur (vue a la premiere personne)
static void DrawHands(Matrix W0, const Weapon *w)
{
    const Color glove = { 40, 38, 34, 255 }, sleeve = { 96, 100, 70, 255 };

    // Main gauche sur le garde-main, ou qui va chercher le chargeur
    Vector3 handL = V3(-0.005f, -0.045f, -0.33f);   // position de repos (garde-main)
    if (w->active == WPN_HARPOON && w->harpoonT > 0)   // va chercher une fleche
        handL = Vector3Lerp(handL, V3(-0.06f, 0.0f, 0.2f), sinf((1.0f - w->harpoonT/HARPOON_RELOAD)*PI));
    if (w->active == WPN_RIFLE && w->reloading)
    {
        bool visible;
        Vector3 mag = MagazinePos(w, &visible);
        Vector3 atMag = V3(mag.x - 0.01f, mag.y - 0.06f, mag.z);   // main sous le chargeur
        float t = w->reloadT;
        // La main rejoint le chargeur (en 0.2 s), le suit, puis repart a 1.6 s
        if (t > 0.1f && t < 1.6f) handL = Vector3Lerp(handL, atMag, fminf(1.0f, (t - 0.1f)*5.0f));
        if (t >= 1.1f && t < 1.45f) handL = atMag;
    }
    // Main + avant-bras qui part vers le bas/gauche, hors de l'ecran
    Ball(W0, handL, V3(0.04f, 0.035f, 0.06f), glove);
    Limb(W0, Vector3Add(handL, V3(-0.02f, -0.03f, 0.03f)), V3(-0.13f, -0.26f, 0.05f), 0.042f, sleeve);

    // Main droite sur la poignee
    Ball(W0, V3(0.0f, -0.1f, 0.095f), V3(0.035f, 0.05f, 0.045f), glove);
    Limb(W0, V3(0.02f, -0.125f, 0.12f), V3(0.12f, -0.25f, 0.38f), 0.045f, sleeve);
}

// Dessine l'arme et les mains a la premiere personne, apres la scene.
// worldCam = camera du monde, utilisee seulement pour orienter la lumiere.
void WeaponRenderViewModel(const Player *p, Camera3D worldCam)
{
    // Passe separee : camera fixe a l'origine, depth buffer vide pour que
    // l'arme passe toujours devant le decor.
    RenderClearDepth();

    Camera3D vc = { 0 };
    vc.position = V3(0, 0, 0);
    vc.target = V3(0, 0, -1);
    vc.up = V3(0, 1, 0);
    // FOV propre a l'arme (independant du FOV du monde) : l'arme garde une
    // taille agreable, et grossit un peu en visant
    vc.fovy = Lerp(55.0f, 40.0f, p->w.ads);
    vc.projection = CAMERA_PERSPECTIVE;

    // Direction du soleil exprimee en espace vue
    // L'arme est dessinee avec une camera fixe : pour qu'elle soit eclairee
    // comme le reste du monde, on tourne la direction du soleil par la partie
    // rotation (3x3) de la matrice de vue de la vraie camera (sans translation,
    // car c'est une direction et non un point).
    Vector3 sun = RenderSunDir();
    Matrix view = MatrixLookAt(worldCam.position, worldCam.target, worldCam.up);
    Vector3 ld = { view.m0*sun.x + view.m4*sun.y + view.m8*sun.z,
                   view.m1*sun.x + view.m5*sun.y + view.m9*sun.z,
                   view.m2*sun.x + view.m6*sun.y + view.m10*sun.z };

    BeginMode3D(vc);
        // Parametres d'eclairage : pas de brouillard (0), et lumiere du flash de
        // bouche placee au canon pendant un tir
        RenderSetFrame(vc.position, Vector3Normalize(ld), 0.0f, MuzzleView(p), p->w.muzzleT > 0 ? 0.25f : 0.0f);
        RenderSetLamps(false);   // les lampes sont en coordonnees monde, pas en espace vue
        Matrix W0 = WeaponViewMatrix(p);
        if (p->w.active == WPN_HARPOON) DrawHarpoonGun(W0, &p->w);
        else DrawHK416(W0, &p->w);
        DrawHands(W0, &p->w);
    EndMode3D();
}
