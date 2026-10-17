/*
 * harpoon.c - Fleches de harpon : projectiles physiques avec gravite.
 *
 * Les fleches sont stockees dans un petit tableau de taille fixe (pas
 * d'allocation dynamique). Chaque frame, chaque fleche en vol :
 *   1. subit la gravite,
 *   2. lance un rayon sur la distance qu'elle va parcourir pendant la frame
 *      (pour ne jamais "traverser" un mur ou un ennemi a grande vitesse),
 *   3. touche un mercenaire, se plante dans le decor, ou avance.
 *
 * Reseau : chaque PC simule toutes les fleches (y compris celles des autres
 * joueurs) pour l'affichage, mais seul le PC du tireur decide de la touche.
 */
#include "harpoon.h"
#include "config.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "audio.h"
#include "net.h"
#include "weapon.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define MAX_HARPOONS   32      // nombre maximum de fleches simultanees (en vol ou plantees)
#define FLIGHT_TIME    3.0f    // une fleche qui ne touche rien disparait
#define STUCK_TIME     6.0f    // une fleche plantee dans le decor reste visible

// Une fleche de harpon
typedef struct {
    bool active, flying;       // active = emplacement utilise ; flying = en vol (false = plantee)
    int owner;                 // joueur qui a tire (-1 : fleche d'un autre PC)
    Vector3 pos, vel;          // pos = pointe de la fleche ; vel = vitesse (m/s)
    float life;                // secondes restantes avant disparition
} Harpoon;

// Reserve de fleches (un emplacement inactif peut etre reutilise)
static Harpoon harpoons[MAX_HARPOONS];

// Vide tout le tableau : plus aucune fleche dans le monde
void HarpoonsClear(void) { memset(harpoons, 0, sizeof(harpoons)); }

// Cree une fleche dans le premier emplacement libre. Si les 32 sont pris,
// la fleche est simplement ignoree (cas tres rare).
static void Spawn(Vector3 pos, Vector3 vel, int owner)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        if (harpoons[i].active) continue;
        // Ordre des champs : active, flying, owner, pos, vel, life
        harpoons[i] = (Harpoon){ true, true, owner, pos, vel, FLIGHT_TIME };
        return;
    }
}

// Tir d'un joueur local : cree la fleche, la signale au reseau et joue le son
void HarpoonFire(Player *p)
{
    // La fleche part de la pointe du harpon et vise le point sous le reticule
    // Probleme : le canon est decale (a droite, en bas) par rapport aux yeux.
    // Solution : on cherche d'abord ce que vise le centre de l'ecran (rayon
    // depuis les yeux), puis on oriente la fleche du canon vers ce point.
    Vector3 start = WeaponMuzzleWorld(p);
    Vector3 eye = PlayerEye(p), f = PlayerForward(p);
    Ray ray = { eye, f };
    float dist = 200.0f;       // portee max ; raccourcie par les raycasts s'ils touchent
    Vector3 normal;
    bool head;
    MapRaycast(ray, &dist, &normal);     // obstacle du decor le plus proche
    EnemyRaycast(ray, &dist, &head);     // ... ou mercenaire plus proche encore
    // Si la cible est tres proche (< 1.5 m), viser depuis le canon donnerait un
    // angle absurde : on tire alors simplement droit devant.
    Vector3 dir = (dist < 1.5f) ? f : Vector3Normalize(Vector3Subtract(Vector3Add(eye, Vector3Scale(f, dist)), start));

    Vector3 vel = Vector3Scale(dir, HARPOON_SPEED);
    Spawn(start, vel, p->id);
    // Evenement reseau : les autres PC creeront une copie visuelle (HarpoonSpawnRemote)
    NetRecordEvent(EV_HARPOON, start, vel, 0, 0, 0);
    SfxWorld(SND_HARPOON, 1.0f, 0.8f);
}

// Fleche tiree sur un autre PC : owner = -1, purement visuelle ici
void HarpoonSpawnRemote(Vector3 pos, Vector3 vel) { Spawn(pos, vel, -1); }

// Une fleche vient de toucher le mercenaire numero "enemy" en allant dans la direction "dir"
static void HitEnemy(const Harpoon *h, int enemy, Vector3 dir)
{
    // Seul le tireur decide de la touche ; les fleches des autres PC sont visuelles
    if (h->owner < 0 || !players[h->owner].local) return;
    Player *shooter = &players[h->owner];
    Enemy *e = &enemies[enemy];
    if (NetIsClient())
    {
        // Client : effet immediat a l'ecran (prediction) + demande a l'hote,
        // qui reste seul juge de l'etat reel des mercenaires.
        EnemyPredictHarpoon(e, dir, shooter);
        NetReportHit(enemy, 0, false, h->pos, dir, HIT_HARPOON);
    }
    else EnemyHarpoon(e, dir, shooter);   // solo / hote : on applique directement
}

// Fait avancer toutes les fleches d'une frame (dt en secondes)
void HarpoonsUpdate(float dt)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        Harpoon *h = &harpoons[i];
        if (!h->active) continue;
        // Duree de vie : en vol (FLIGHT_TIME) ou plantee (STUCK_TIME)
        h->life -= dt;
        if (h->life <= 0) { h->active = false; continue; }
        if (!h->flying) continue;   // plantee : elle ne bouge plus

        // Gravite (faible, la fleche reste presque droite) puis rayon de la
        // longueur du deplacement de cette frame pour detecter les collisions
        h->vel.y -= HARPOON_GRAVITY*dt;
        float step = Vector3Length(h->vel)*dt;
        Ray ray = { h->pos, Vector3Normalize(h->vel) };
        float dist = step;          // les raycasts ne regardent pas plus loin que ce pas
        Vector3 normal;
        bool head;
        bool hitWorld = MapRaycast(ray, &dist, &normal);
        // EnemyRaycast ne renvoie un ennemi que s'il est plus proche que "dist"
        // (donc devant le mur eventuel), d'ou l'ordre des deux appels.
        int enemy = EnemyRaycast(ray, &dist, &head);

        if (enemy >= 0)
        {
            HitEnemy(h, enemy, ray.direction);
            h->active = false;   // la fleche reste plantee dans le mercenaire (dessinee avec lui)
            continue;
        }
        if (hitWorld)
        {
            // Plantee dans le decor
            h->pos = Vector3Add(h->pos, Vector3Scale(ray.direction, dist));   // point d'impact exact
            h->flying = false;
            h->life = STUCK_TIME;
            NetSuppress(true);   // chaque PC fait ses propres etincelles
            BurstSparks(h->pos, normal);
            NetSuppress(false);
            PlaySfx(SND_THUD, 1.3f, 0.5f);
            continue;
        }
        // Rien touche : la fleche avance
        h->pos = Vector3Add(h->pos, Vector3Scale(h->vel, dt));
        // Sortie largement hors de la carte ou sous le sol : on la supprime
        if (fabsf(h->pos.x) > 100 || fabsf(h->pos.z) > 100 || h->pos.y < -5) h->active = false;
    }
}

// Dessine toutes les fleches actives (en vol ou plantees dans le decor)
void HarpoonsDraw(void)
{
    for (int i = 0; i < MAX_HARPOONS; i++)
    {
        const Harpoon *h = &harpoons[i];
        if (!h->active) continue;
        // Repere de la fleche : pointe a la position, orientee selon la vitesse
        // Le modele (DrawSpear) pointe vers -Z : on calcule la rotation qui amene
        // -Z sur la direction de vol. Cas particulier : si la direction est
        // exactement +Z (opposee), la rotation "de -Z vers dir" est indefinie,
        // on fait donc un demi-tour autour de Y a la place.
        Vector3 dir = Vector3Normalize(h->vel);
        Quaternion q = (dir.z > 0.999f) ? QuaternionFromAxisAngle((Vector3){ 0, 1, 0 }, PI)
                                        : QuaternionFromVector3ToVector3((Vector3){ 0, 0, -1 }, dir);
        // Rotation d'abord, puis translation a la position de la pointe
        Matrix m = MatrixMultiply(QuaternionToMatrix(q), MatrixTranslate(h->pos.x, h->pos.y, h->pos.z));
        DrawSpear(m);
    }
}
