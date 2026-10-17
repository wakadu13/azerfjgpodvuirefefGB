/*
 * weapon.c - Logique et rendu du HK416.
 *
 * Le modele de l'arme est defini dans son propre repere : origine au
 * centre du boitier, canon vers -Z, +Y vers le haut. Le meme modele sert
 * pour la vue a la premiere personne et pour le coequipier vu de dehors.
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
#include "util.h"

#include "raymath.h"

#include <math.h>

#define MUZZLE_Z      -0.68f
#define SIGHT_HEIGHT  0.085f   // axe du viseur au-dessus de l'origine
#define SIGHT_Z       -0.04f

static const Color METAL = { 38, 38, 36, 255 };
static const Color METAL2 = { 55, 54, 50, 255 };
static const Color POLYMER = { 30, 30, 30, 255 };

void WeaponReset(Weapon *w)
{
    *w = (Weapon){ 0 };
    w->ammo = MAG_SIZE;
    w->reserve = START_RESERVE;
}

//----------------------------------------------------------------------------------
// Position de l'arme a l'ecran
//----------------------------------------------------------------------------------
static Vector3 WeaponOffset(const Player *p)
{
    const Weapon *w = &p->w;
    Vector3 hip = V3(0.17f, -0.19f, -0.45f);
    Vector3 ads = V3(0.0f, -SIGHT_HEIGHT, -0.33f);   // point rouge exactement au centre
    Vector3 o = Vector3Lerp(hip, ads, w->ads);
    float bobAmt = 1.0f - w->ads*0.85f;
    o.x += sinf(p->bob)*0.012f*bobAmt + w->sway.x;
    o.y += -fabsf(cosf(p->bob))*0.012f*bobAmt + w->sway.y;
    o.z += w->kick*0.06f;
    o.y -= w->sprintT*0.06f;
    o.x -= w->sprintT*0.04f;
    return o;
}

// Repere de l'arme en espace vue (camera a l'origine, regardant vers -Z)
static Matrix WeaponViewMatrix(const Player *p)
{
    const Weapon *w = &p->w;
    Vector3 o = WeaponOffset(p);
    float reloadTilt = w->reloading ? sinf(fminf(w->reloadT/w->reloadDur, 1.0f)*PI) : 0.0f;
    Vector3 rot = V3(w->kick*0.12f - reloadTilt*0.25f - w->sprintT*0.25f,
                     w->sprintT*0.7f + reloadTilt*0.2f,
                     reloadTilt*0.5f + w->sprintT*0.35f);
    return MatrixMultiply(MatrixRotateXYZ(rot), MatrixTranslate(o.x, o.y, o.z));
}

static Vector3 MuzzleView(const Player *p) { return Vector3Transform(V3(0, 0, MUZZLE_Z), WeaponViewMatrix(p)); }

static Vector3 ViewToWorld(const Player *p, Vector3 v)
{
    Vector3 f = PlayerForward(p);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);
    Vector3 eye = PlayerEye(p);
    return Vector3Add(eye, Vector3Add(Vector3Scale(r, v.x), Vector3Add(Vector3Scale(u, v.y), Vector3Scale(f, -v.z))));
}

Vector3 WeaponMuzzleWorld(const Player *p) { return ViewToWorld(p, MuzzleView(p)); }

float WeaponSpread(const Player *p)
{
    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    return Lerp(0.022f, 0.002f, p->w.ads) + hspeed*0.003f + (p->grounded ? 0.0f : 0.04f) + p->w.bloom;
}

//----------------------------------------------------------------------------------
// Tir et rechargement
//----------------------------------------------------------------------------------
static void StartReload(Weapon *w)
{
    if (w->reloading || w->reserve <= 0 || w->ammo >= MAG_SIZE) return;
    w->reloading = true;
    w->reloadT = 0;
    w->reloadStep = 0;
    w->reloadDur = (w->ammo == 0) ? RELOAD_EMPTY : RELOAD_TIME;
}

static void FinishReload(Weapon *w)
{
    int chamber = (w->ammo > 0) ? 1 : 0;   // une balle reste chambree sur un rechargement tactique
    int need = MAG_SIZE + chamber - w->ammo;
    int take = (need < w->reserve) ? need : w->reserve;
    w->ammo += take;
    w->reserve -= take;
    w->reloading = false;
}

static void Shoot(Player *p)
{
    Weapon *w = &p->w;
    w->ammo--;
    w->fireT = FIRE_DELAY;
    w->muzzleT = 0.05f;
    w->kick = 1.0f;
    w->recoilPitch += Randf(0.010f, 0.016f)*(1.0f - w->ads*0.35f);
    w->recoilYaw += Randf(-0.006f, 0.006f);
    w->bloom = fminf(w->bloom + 0.006f, 0.04f);
    w->sprintT = 0.0f;
    p->shake = fmaxf(p->shake, 0.04f);
    SfxWorld(SND_SHOT, Randf(0.92f, 1.08f), 0.55f);

    // Direction du tir = regard + dispersion aleatoire dans un cone
    Vector3 eye = PlayerEye(p);
    Vector3 f = PlayerForward(p);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, V3(0, 1, 0)));
    Vector3 u = Vector3CrossProduct(r, f);
    float ang = Randf(0, 2*PI), mag = Randf(0, 1)*WeaponSpread(p);
    Vector3 dir = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(r, cosf(ang)*mag), Vector3Scale(u, sinf(ang)*mag))));
    Ray ray = { eye, dir };

    float dist = 250.0f;
    Vector3 normal = V3(0, 1, 0);
    bool hitWorld = MapRaycast(ray, &dist, &normal);
    bool head = false;
    int target = EnemyRaycast(ray, &dist, &head);

    Vector3 hit = Vector3Add(eye, Vector3Scale(dir, dist));
    AddTracer(WeaponMuzzleWorld(p), hit);
    float dmg = BULLET_DAMAGE*(head ? HEADSHOT_MULT : 1.0f);
    if (target >= 0 && NetIsClient())
    {
        // En reseau, c'est l'hote qui applique les degats
        EnemyPredictHit(&enemies[target], dmg, head, hit, dir, p);
        NetReportHit(target, dmg, head, hit, dir);
    }
    else if (target >= 0) EnemyDamage(&enemies[target], dmg, head, hit, dir, p);
    else if (hitWorld) BurstSparks(hit, normal);
}

void WeaponUpdate(Player *p, float dt)
{
    Weapon *w = &p->w;
    const PlayerInput *in = &p->in;

    w->fireT -= dt;
    w->muzzleT -= dt;
    w->kick = fmaxf(0, w->kick - dt*12.0f);
    w->recoilPitch -= w->recoilPitch*fminf(1.0f, dt*6.0f);
    w->recoilYaw -= w->recoilYaw*fminf(1.0f, dt*6.0f);
    w->bloom = fmaxf(0, w->bloom - dt*0.08f);
    if (!p->alive) return;

    bool aiming = in->aim && !w->reloading && !p->sprinting;
    w->ads += ((aiming ? 1.0f : 0.0f) - w->ads)*fminf(1.0f, dt*14.0f);
    w->sprintT += ((p->sprinting ? 1.0f : 0.0f) - w->sprintT)*fminf(1.0f, dt*10.0f);

    // L'arme "traine" legerement derriere les mouvements de camera
    w->sway.x += (in->look.x*0.18f - w->sway.x)*fminf(1.0f, dt*8.0f);
    w->sway.y += (-in->look.y*0.18f - w->sway.y)*fminf(1.0f, dt*8.0f);
    w->sway.x = Clamp(w->sway.x, -0.03f, 0.03f);
    w->sway.y = Clamp(w->sway.y, -0.03f, 0.03f);

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
        const float marks[3] = { 0.3f, 1.45f, 2.35f };
        int steps = (w->reloadDur > RELOAD_TIME) ? 3 : 2;
        w->reloadT += dt;
        if (w->reloadStep < steps && w->reloadT > marks[w->reloadStep])
        {
            PlaySfx(SND_CLICK, w->reloadStep == 2 ? 0.7f : 1.0f, 0.7f);
            w->reloadStep++;
        }
        if (w->reloadT >= w->reloadDur) FinishReload(w);
        return;
    }

    if (!in->fire) w->shotThisPress = false;
    bool trigger = w->semi ? (in->fire && !w->shotThisPress) : in->fire;
    if (!trigger || w->fireT > 0 || p->sprinting) return;

    if (w->ammo > 0)
    {
        Shoot(p);
        w->shotThisPress = true;
    }
    else
    {
        if (in->firePressed) PlaySfx(SND_EMPTY, 1.0f, 0.6f);
        w->fireT = 0.25f;
        StartReload(w);
    }
}

//----------------------------------------------------------------------------------
// Modele 3D
//----------------------------------------------------------------------------------
// Position du chargeur pendant le rechargement (il sort puis rentre).
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

static void DrawMuzzleFlash(Matrix frame)
{
    float k = 0.8f + Randf(0, 0.6f);
    Matrix fm = MatrixMultiply(MatrixRotateZ(Randf(0, PI)), MatrixMultiply(MatrixTranslate(0, 0, MUZZLE_Z - 0.04f), frame));
    RenderSetEmissive(true);
    Ball(fm, V3(0, 0, 0), V3(0.035f*k, 0.035f*k, 0.06f*k), (Color){ 255, 240, 170, 255 });
    for (int i = 0; i < 4; i++)
        Box3(fm, V3(0, 0, -0.01f), V3(0, 0, i*PI/4), V3(0.12f*k, 0.012f, 0.012f), (Color){ 255, 180, 60, 255 });
    Ball(fm, V3(0, 0, -0.05f*k), V3(0.02f*k, 0.02f*k, 0.08f*k), (Color){ 255, 200, 90, 255 });
    RenderSetEmissive(false);
}

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
    for (int i = 0; i < 4; i++)
    {
        float a = i*PI*0.5f;
        Box3(W0, V3(cosf(a)*0.036f, sinf(a)*0.036f, -0.29f), V3(0, 0, a), V3(0.008f, 0.02f, 0.29f), METAL);
    }
    Box3(W0, V3(0, 0.036f, -0.13f), NOROT, V3(0.024f, 0.01f, 0.6f), METAL);
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
    const float R = 0.02f;
    Box3(W0, V3(0, 0.055f, SIGHT_Z), NOROT, V3(0.026f, 0.03f, 0.04f), METAL);
    for (int i = 0; i < 8; i++)
    {
        float a = i*PI/4;
        Box3(W0, V3(cosf(a)*R, SIGHT_HEIGHT + sinf(a)*R, SIGHT_Z), V3(0, 0, a - PI/2), V3(0.0175f, 0.004f, 0.06f), POLYMER);
    }
    Box3(W0, V3(0.023f, SIGHT_HEIGHT, SIGHT_Z), NOROT, V3(0.008f, 0.012f, 0.014f), POLYMER);   // molette
    RenderSetEmissive(true);
    Ball(W0, V3(0, SIGHT_HEIGHT, SIGHT_Z - 0.03f), V3(0.0012f, 0.0012f, 0.0012f), (Color){ 255, 30, 30, 255 });
    RenderSetEmissive(false);

    // Chargeur courbe (anime pendant le rechargement)
    bool magVisible;
    Vector3 mag = MagazinePos(w, &magVisible);
    if (magVisible)
    {
        Matrix mm = MatrixMultiply(MatrixMultiply(MatrixRotateX(0.18f), MatrixTranslate(mag.x, mag.y, mag.z)), W0);
        Box3(mm, V3(0, 0.03f, 0), NOROT, V3(0.034f, 0.11f, 0.066f), METAL2);
        Box3(mm, V3(0, -0.04f, -0.008f), V3(0.2f, 0, 0), V3(0.034f, 0.07f, 0.066f), METAL2);
        Box3(mm, V3(0, -0.08f, -0.018f), V3(0.2f, 0, 0), V3(0.04f, 0.014f, 0.072f), POLYMER);
    }

    if (w && w->muzzleT > 0) DrawMuzzleFlash(W0);
}

// Mains gantees et avant-bras du joueur (vue a la premiere personne)
static void DrawHands(Matrix W0, const Weapon *w)
{
    const Color glove = { 40, 38, 34, 255 }, sleeve = { 96, 100, 70, 255 };

    // Main gauche sur le garde-main, ou qui va chercher le chargeur
    Vector3 handL = V3(-0.005f, -0.045f, -0.33f);
    if (w->reloading)
    {
        bool visible;
        Vector3 mag = MagazinePos(w, &visible);
        Vector3 atMag = V3(mag.x - 0.01f, mag.y - 0.06f, mag.z);
        float t = w->reloadT;
        if (t > 0.1f && t < 1.6f) handL = Vector3Lerp(handL, atMag, fminf(1.0f, (t - 0.1f)*5.0f));
        if (t >= 1.1f && t < 1.45f) handL = atMag;
    }
    Ball(W0, handL, V3(0.04f, 0.035f, 0.06f), glove);
    Limb(W0, Vector3Add(handL, V3(-0.02f, -0.03f, 0.03f)), V3(-0.13f, -0.26f, 0.05f), 0.042f, sleeve);

    // Main droite sur la poignee
    Ball(W0, V3(0.0f, -0.1f, 0.095f), V3(0.035f, 0.05f, 0.045f), glove);
    Limb(W0, V3(0.02f, -0.125f, 0.12f), V3(0.12f, -0.25f, 0.38f), 0.045f, sleeve);
}

void WeaponRenderViewModel(const Player *p, Camera3D worldCam)
{
    // Passe separee : camera fixe a l'origine, depth buffer vide pour que
    // l'arme passe toujours devant le decor.
    RenderClearDepth();

    Camera3D vc = { 0 };
    vc.position = V3(0, 0, 0);
    vc.target = V3(0, 0, -1);
    vc.up = V3(0, 1, 0);
    vc.fovy = Lerp(55.0f, 40.0f, p->w.ads);
    vc.projection = CAMERA_PERSPECTIVE;

    // Direction du soleil exprimee en espace vue
    Vector3 sun = RenderSunDir();
    Matrix view = MatrixLookAt(worldCam.position, worldCam.target, worldCam.up);
    Vector3 ld = { view.m0*sun.x + view.m4*sun.y + view.m8*sun.z,
                   view.m1*sun.x + view.m5*sun.y + view.m9*sun.z,
                   view.m2*sun.x + view.m6*sun.y + view.m10*sun.z };

    BeginMode3D(vc);
        RenderSetFrame(vc.position, Vector3Normalize(ld), 0.0f, MuzzleView(p), p->w.muzzleT > 0 ? 0.25f : 0.0f);
        Matrix W0 = WeaponViewMatrix(p);
        DrawHK416(W0, &p->w);
        DrawHands(W0, &p->w);
    EndMode3D();
}
