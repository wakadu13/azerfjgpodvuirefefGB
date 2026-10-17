/*
 * enemy.c - Les mercenaires.
 *
 * IA : chaque mercenaire entre par une porte puis va attraper les poussins
 * sur le comptoir. S'il passe pres d'un joueur (ou si c'est un
 * "Eclaireur"), il le poursuit et le frappe au corps a corps. Chaque
 * poussin enleve le rend plus costaud. Le modele 3D est dans enemymodel.c.
 *
 * Machine a etats (champ `state`) :
 *
 *   ES_WALK --(arrive au comptoir)--> ES_GRAB --(GRAB_TIME ecoule)--> poussin vole,
 *      ^                                 |                            grabT remis a 0,
 *      +------(un joueur s'approche)-----+                            reste en ES_GRAB
 *
 *   n'importe quel etat --(hp <= 0 ou harpon)--> ES_DEAD --(quelques secondes)--> active = false
 *
 * En reseau, seul l'hote (ou le joueur solo) fait tourner l'IA (EnemiesUpdate).
 * Les clients recoivent les positions et se contentent de lisser et d'animer
 * (EnemiesClientUpdate). Les tirs d'un client sont "predits" localement
 * (EnemyPredictHit / EnemyPredictHarpoon) pour un retour immediat, puis
 * l'hote confirme.
 */
#include "enemy.h"
#include "map.h"
#include "net.h"
#include "effects.h"
#include "pickup.h"
#include "audio.h"
#include "hud.h"
#include "game.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define AGGRO_RANGE  7.0f    // distance a laquelle un mercenaire prefere le joueur aux poussins

// Tableau global de tous les mercenaires (cases libres : active == false)
Enemy enemies[MAX_ENEMIES];

// Remet tout le tableau a zero : plus aucun mercenaire actif
void EnemiesClear(void) { memset(enemies, 0, sizeof(enemies)); }

// Nom affiche dans le fil d'evenements (kills, arrivee du chef...)
const char *EnemyName(EnemyType type)
{
    switch (type)
    {
        case E_FAST: return "L'Eclaireur";
        case E_TANK: return "Le Colosse";
        case E_BOSS: return "LE CHEF DE GUERRE";
        default:     return "Le Mercenaire";
    }
}

// Rayon du cylindre de collision : proportionnel a la taille et a la carrure
float EnemyRadius(const Enemy *e) { return 0.42f*e->sz*e->fat; }

// Compte les mercenaires encore en vie (les morts en train de tomber ne comptent pas)
int EnemiesAlive(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active && enemies[i].state != ES_DEAD) n++;
    return n;
}

// Vrai si le mercenaire a deja saisi un poussin (seconde partie de l'enlevement)
bool EnemyHoldsChick(const Enemy *e) { return e->active && e->state == ES_GRAB && e->grabT > GRAB_HOLD; }

// Nombre de poussins actuellement dans les mains des mercenaires.
// Sert a ne pas laisser plus de mercenaires saisir un poussin qu'il n'y en a sur le comptoir.
int EnemiesHoldingChicks(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (EnemyHoldsChick(&enemies[i])) n++;
    return n;
}

//----------------------------------------------------------------------------------
// Apparition
//----------------------------------------------------------------------------------
// Statistiques selon le type et la vague courante.
// sz = taille globale (1 = humain ~1.9 m), fat = carrure (elargit le corps).
static void SetStats(Enemy *e, int wave)
{
    switch (e->type)
    {
        case E_FAST: e->maxHp = 60;  e->speed = 4.6f; e->sz = 0.85f; e->fat = 1.0f;  e->dmg = 7;  break;
        case E_TANK: e->maxHp = 320; e->speed = 1.7f; e->sz = 1.35f; e->fat = 1.35f; e->dmg = 20; break;
        case E_BOSS:
            // Le chef a ses propres points de vie qui augmentent avec la vague
            e->maxHp = 2200 + 300*wave; e->speed = 1.9f; e->sz = 2.3f; e->fat = 1.4f; e->dmg = 35;
            e->gear = GEAR_NONE;   // il porte son beret sur la tete
            break;
        default:     e->maxHp = 90;  e->speed = 2.7f; e->sz = 1.0f;  e->fat = 1.0f;  e->dmg = 12; break;
    }
    // Plus resistants et un peu plus rapides au fil des vagues (et en coop)
    if (e->type != E_BOSS) e->maxHp *= 1.0f + 0.12f*(wave - 1);   // +12 % de vie par vague
    if (PlayersConnected() > 1) e->maxHp *= 1.25f;                 // +25 % de vie en multijoueur
    e->speed *= fminf(1.0f + 0.03f*(wave - 1), 1.4f);              // +3 % de vitesse par vague, plafonne a +40 %
    e->hp = e->maxHp;
}

// Point d'apparition aleatoire, a l'exterieur de l'arene (au-dela de ARENA)
static Vector3 GateSpawnPoint(void)
{
    // Derriere une des quatre portes, un peu decale pour ne pas s'empiler
    // off  : decalage lateral par rapport a l'axe de la porte
    // dist : distance au centre, toujours plus grande que le mur (ARENA)
    float off = Randf(-1.6f, 1.6f), dist = ARENA + Randf(3.0f, 6.0f);
    switch (GetRandomValue(0, 3))
    {
        case 0:  return V3(off, 0, -dist);   // porte nord (-z)
        case 1:  return V3(off, 0, dist);    // porte sud (+z)
        case 2:  return V3(-dist, 0, off);   // porte ouest (-x)
        default: return V3(dist, 0, off);    // porte est (+x)
    }
}

// Cree un mercenaire du type demande dans la premiere case libre du tableau.
// Si le tableau est plein, l'apparition est simplement ignoree.
void EnemySpawn(EnemyType type, int wave)
{
    Enemy *e = NULL;
    for (int i = 0; i < MAX_ENEMIES; i++) if (!enemies[i].active) { e = &enemies[i]; break; }
    if (!e) return;

    memset(e, 0, sizeof(*e));   // repart d'une case propre (tous les champs a 0 / false)
    e->active = true;
    e->type = type;
    e->state = ES_WALK;
    // Apparence tiree au hasard pour varier la foule
    e->camo = (unsigned char)GetRandomValue(0, CAMO_COUNT - 1);
    e->skin = (unsigned char)GetRandomValue(0, SKIN_COUNT - 1);
    e->paint = (unsigned char)GetRandomValue(0, PAINT_COUNT - 1);
    e->gear = (unsigned char)GetRandomValue(0, GEAR_COUNT - 1);
    if (type == E_FAST) e->gear = GEAR_SCARF;   // l'Eclaireur porte toujours le foulard (reconnaissable)
    e->phase = Randf(0, 2*PI);   // phase de marche aleatoire : ils ne marchent pas tous au pas
    e->sideSign = GetRandomValue(0, 1) ? 1.0f : -1.0f;
    SetStats(e, wave);

    e->pos = GateSpawnPoint();
    // Regarde vers le centre de l'arene : direction (0 - pos) ; yaw = atan2(x, z) car 0 = +z
    e->yaw = atan2f(-e->pos.x, -e->pos.z);
    if (type == E_BOSS) FeedAdd(GOLD, "%s est arrive !", EnemyName(type));
}

//----------------------------------------------------------------------------------
// Degats
//----------------------------------------------------------------------------------
// Lance un rayon (tir) contre tous les mercenaires vivants.
// Chaque mercenaire est approxime par trois spheres empilees ; on garde le
// coup le plus proche. *dist sert a la fois de distance max en entree et de
// distance du coup en sortie (ce qui permet de tester d'abord les murs).
int EnemyRaycast(Ray ray, float *dist, bool *head)
{
    int hit = -1;
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active || e->state == ES_DEAD) continue;
        float s = e->sz;

        // Trois spheres (un peu genereuses) : tete, torse, jambes
        // Les hauteurs sont celles du modele (en metres pour sz = 1), mises a l'echelle par s.
        // Tete : rayon independant de la carrure (la tete ne grossit pas)
        RayCollision c = GetRayCollisionSphere(ray, V3(e->pos.x, 1.78f*s, e->pos.z), 0.17f*s);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = true; }

        // Torse
        c = GetRayCollisionSphere(ray, V3(e->pos.x, 1.28f*s, e->pos.z), 0.3f*s*e->fat);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = false; }

        // Jambes
        c = GetRayCollisionSphere(ray, V3(e->pos.x, 0.55f*s, e->pos.z), 0.3f*s*e->fat);
        if (c.hit && c.distance < *dist) { *dist = c.distance; hit = i; *head = false; }
    }
    return hit;
}

// Points de score gagnes pour un mercenaire tue (+50 % pour un headshot)
static int KillPoints(const Enemy *e, bool head)
{
    int pts = 100;
    if (e->type == E_FAST) pts = 120;
    if (e->type == E_TANK) pts = 300;
    if (e->type == E_BOSS) pts = 2500;
    return head ? pts*3/2 : pts;
}

// Mort d'un mercenaire (cote hote / solo) : statistiques du tireur, score,
// message, effets, sons et bonus laches au sol.
// harpoon : change seulement le message (le vol du corps est gere par LaunchBody).
static void KillEnemy(Enemy *e, bool head, bool harpoon, Vector3 dir, Player *shooter)
{
    e->state = ES_DEAD;
    e->deadT = 0;
    shooter->kills++;
    if (head) shooter->headshots++;
    shooter->hitKill = true;   // le reticule affiche un marqueur de kill
    G.score += KillPoints(e, head);

    if (harpoon) FeedAdd(SKYBLUE, "%s%s harponne !", PlayerTag(shooter), EnemyName(e->type));
    else FeedAdd(head ? ORANGE : RAYWHITE, head ? "%s%s neutralise (HEADSHOT)" : "%s%s neutralise", PlayerTag(shooter), EnemyName(e->type));
    BurstImpact(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, e->type == E_BOSS ? 120 : 40);   // gerbe de particules
    SfxWorld(SND_KILL, 1.0f, 0.5f);
    PickupDrop(e->pos);   // peut lacher un bonus (munitions, soin...)
    if (e->type == E_BOSS)
    {
        // Le chef lache plus de bonus (le second un peu a cote pour ne pas les superposer)
        PickupDrop(e->pos);
        PickupDrop(V3(e->pos.x + 1, 0, e->pos.z));
    }
}

// Applique des degats (cote hote / solo, fait autorite).
// hitPos : point d'impact (pour les particules) ; dir : direction du tir.
void EnemyDamage(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter)
{
    e->hp -= dmg;
    e->hitFlash = 1.0f;          // le modele clignote en blanc
    shooter->hitT = 0.18f;       // duree du marqueur de touche sur le reticule
    shooter->hitKill = false;
    shooter->hitHead = head;
    BurstImpact(hitPos, Vector3Negate(dir), head ? 14 : 7);   // particules renvoyees vers le tireur
    SfxWorld(SND_HIT, Randf(0.9f, 1.15f), 0.7f);              // hauteur aleatoire pour varier le son
    if (e->hp <= 0) KillEnemy(e, head, false, dir, shooter);
}

// Meme chose que EnemyDamage mais pour un client reseau : on montre tout de
// suite le resultat probable, sans score ni bonus (c'est l'hote qui decide).
void EnemyPredictHit(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter)
{
    // Effets locaux seulement (non retransmis) : l'hote rejouera les siens
    NetSuppress(true);
    e->hp -= dmg;
    e->hitFlash = 1.0f;
    shooter->hitT = 0.18f;
    shooter->hitHead = head;
    shooter->hitKill = e->hp <= 0;
    BurstImpact(hitPos, Vector3Negate(dir), head ? 14 : 7);
    PlaySfx(SND_HIT, Randf(0.9f, 1.15f), 0.7f);   // PlaySfx : son local uniquement (pas SfxWorld)
    if (e->hp <= 0)
    {
        // Mort predite : predictT laisse le temps a l'hote de confirmer avant
        // que les paquets reseau ne reprennent la main sur cet ennemi
        e->state = ES_DEAD;
        e->deadT = 0;
        e->predictT = 0.8f;
        BurstImpact(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, 40);
        PlaySfx(SND_KILL, 1.0f, 0.5f);
    }
    NetSuppress(false);
}

//----------------------------------------------------------------------------------
// Harpon : mercenaire projete
//----------------------------------------------------------------------------------
// Donne une vitesse au corps pour le projeter dans la direction dir.
// power : 1 pour un coup de harpon direct, moins pour un mercenaire percute en vol.
// killer : indice du joueur a crediter pour les mercenaires renverses par ce corps.
static void LaunchBody(Enemy *e, Vector3 dir, float power, int killer)
{
    // Direction horizontale uniquement ; si le tir est quasi vertical, on le
    // projette en arriere par rapport a son regard (yaw + PI)
    Vector3 h = { dir.x, 0, dir.z };
    h = (Vector3Length(h) < 0.01f) ? V3(sinf(e->yaw + PI), 0, cosf(e->yaw + PI)) : Vector3Normalize(h);
    float mass = 1.0f/(0.6f + 0.4f*e->sz*e->fat);   // les gros volent moins loin
    // Vitesse = poussee horizontale + elan vertical (+2 m/s minimum pour bien decoller du sol)
    e->vel = Vector3Add(Vector3Scale(h, HARPOON_LAUNCH*power*mass), V3(0, HARPOON_LIFT*power*mass + 2.0f, 0));
    e->launched = e->airborne = true;
    e->spin = 0;
    e->yaw = atan2f(-h.x, -h.z);   // face au tireur : il part a la renverse
    e->deadT = 0;
    e->killer = killer;
    e->bowled = 0;                 // personne n'a encore ete percute pendant ce vol
}

// Coup de harpon (hote / solo) : mort immediate puis vol du corps
void EnemyHarpoon(Enemy *e, Vector3 dir, Player *shooter)
{
    e->hitFlash = 1.0f;
    shooter->hitT = 0.25f;
    shooter->hitHead = false;
    KillEnemy(e, false, true, dir, shooter);
    LaunchBody(e, dir, 1.0f, shooter->id);
    SfxWorld(SND_THUD, 0.9f, 0.9f);
}

// Coup de harpon predit par un client reseau (effets locaux, l'hote confirmera)
void EnemyPredictHarpoon(Enemy *e, Vector3 dir, Player *shooter)
{
    NetSuppress(true);
    e->hitFlash = 1.0f;
    e->state = ES_DEAD;
    e->predictT = 1.5f;   // le vol est simule localement pendant 1.5 s (voir EnemiesClientUpdate)
    shooter->hitT = 0.25f;
    shooter->hitKill = true;
    shooter->hitHead = false;
    BurstImpact(V3(e->pos.x, 1.2f*e->sz, e->pos.z), dir, 40);
    LaunchBody(e, dir, 1.0f, shooter->id);
    PlaySfx(SND_KILL, 1.0f, 0.5f);
    PlaySfx(SND_THUD, 0.9f, 0.9f);
    NetSuppress(false);
}

// En vol, le mercenaire harponne renverse ceux qu'il percute
// (comme une boule de bowling dans des quilles). Chaque victime n'est touchee
// qu'une fois par vol grace au masque de bits `bowled` (bit i = enemies[i]).
static void Bowl(Enemy *e)
{
    Vector3 push = Vector3Normalize(V3(e->vel.x, 0, e->vel.z));   // direction horizontale du vol
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *o = &enemies[i];
        if (o == e || !o->active || o->state == ES_DEAD || (e->bowled & (1ull << i))) continue;
        // Collision cercle/cercle au sol + le corps doit voler plus bas que la tete de la cible
        float dx = o->pos.x - e->pos.x, dz = o->pos.z - e->pos.z, rr = EnemyRadius(e) + EnemyRadius(o);
        if (dx*dx + dz*dz > rr*rr || e->pos.y > 2.2f*o->sz) continue;
        e->bowled |= 1ull << i;
        o->pos = Vector3Add(o->pos, push);   // la victime recule d'un metre
        EnemyDamage(o, HARPOON_BOWL_DMG, false, V3(o->pos.x, 1.2f*o->sz, o->pos.z), push, &players[e->killer]);
        if (o->state == ES_DEAD) LaunchBody(o, push, 0.6f, e->killer);   // la quille s'envole aussi
    }
}

// Physique d'un corps projete par le harpon : gravite, rotation, collisions
// avec le decor, rebond, puis immobilisation couche au sol.
// authority : true chez l'hote (peut percuter et blesser d'autres mercenaires),
//             false pour la prediction locale d'un client (purement visuel).
static void UpdateLaunched(Enemy *e, float dt, bool authority)
{
    if (!e->airborne)
    {
        // Au sol : se couche sur le dos ou le ventre, puis disparait
        // (rapprochement exponentiel de spin vers restSpin)
        e->deadT += dt;
        e->spin += (e->restSpin - e->spin)*fminf(1.0f, dt*12.0f);
        return;
    }
    e->deadT += dt;
    e->vel.y -= 22.0f*dt;   // gravite (un peu plus forte que 9.81 pour un effet plus nerveux)
    e->pos = Vector3Add(e->pos, Vector3Scale(e->vel, dt));
    e->spin += dt*9.0f/e->sz;   // fait des saltos en arriere ; les gros tournent moins vite

    // S'ecrase contre les murs et les caisses (et glisse le long), sans revenir vers le tireur
    // ResolveCircle repousse le corps hors des obstacles ; le deplacement subi
    // nous donne la normale de l'obstacle touche.
    Vector3 before = e->pos;
    ResolveCircle(&e->pos, EnemyRadius(e)*0.7f, e->pos.y, 2.0f*e->sz);
    Vector3 pushed = Vector3Subtract(e->pos, before);
    float pl = Vector3Length(pushed);
    if (pl > 1e-4f)
    {
        Vector3 n = Vector3Scale(pushed, 1.0f/pl);           // normale de l'obstacle (vers l'exterieur)
        float into = e->vel.x*n.x + e->vel.z*n.z;            // vitesse dirigee vers l'obstacle (< 0)
        if (into < 0) { e->vel.x -= into*n.x; e->vel.z -= into*n.z; }   // on retire la composante vers l'obstacle
        // Frottement : perd la moitie de sa vitesse horizontale a chaque choc
        e->vel.x *= 0.5f;
        e->vel.z *= 0.5f;
    }
    // Ne sort jamais trop loin de l'arene (zone des portes comprise)
    e->pos.x = Clamp(e->pos.x, -ARENA - 8, ARENA + 8);
    e->pos.z = Clamp(e->pos.z, -ARENA - 8, ARENA + 8);
    if (authority) Bowl(e);

    // Contact avec le sol (on ne teste qu'en descente)
    if (e->pos.y <= 0 && e->vel.y < 0)
    {
        e->pos.y = 0;
        if (e->vel.y < -5.0f)   // rebondit une fois
        {
            // Rebond amorti : 30 % de la vitesse verticale, moitie de l'horizontale
            e->vel = V3(e->vel.x*0.5f, -e->vel.y*0.3f, e->vel.z*0.5f);
            PlaySfx(SND_THUD, Randf(0.8f, 1.0f), 0.6f);
        }
        else
        {
            // Atterrissage definitif : on choisit la position couchee la plus proche
            // de l'angle actuel. m = angle ramene dans [0, 2*PI[ ;
            // m < PI -> il finit a +PI/2 (sur le dos), sinon a 3*PI/2 (sur le ventre).
            e->airborne = false;
            e->vel = V3(0, 0, 0);
            e->deadT = 0;   // deadT compte maintenant le temps passe au sol
            float m = fmodf(e->spin, 2*PI);
            if (m < 0) m += 2*PI;
            e->restSpin = e->spin - m + ((m < PI) ? PI*0.5f : PI*1.5f);
        }
    }
    // Securite : s'il reste coince en l'air (sur une caisse...), on le pose au sol au bout de 5 s
    if (e->deadT > 5.0f) { e->airborne = false; e->pos.y = 0; e->deadT = 0; e->restSpin = PI*0.5f; }
}

//----------------------------------------------------------------------------------
// IA
//----------------------------------------------------------------------------------
// Le mercenaire emporte un poussin : il en reste un de moins, il grossit et
// reprend de la vie. Plus de poussins = partie perdue.
static void StealChick(Enemy *e)
{
    if (G.chicks <= 0 || G.state != ST_PLAY) return;
    G.chicks--;
    e->fat = fminf(e->fat + 0.1f, 1.8f);                         // +10 % de carrure, max 1.8
    e->hp = fminf(e->hp + e->maxHp*0.3f, e->maxHp*1.6f);          // +30 % de vie, peut depasser le max (160 %)
    BurstFeathers(V3(e->pos.x, 1.6f*e->sz, e->pos.z));
    SfxWorld(SND_CHIRP, Randf(0.9f, 1.15f), 0.9f);
    FeedAdd((Color){ 255, 120, 80, 255 }, "%s a enleve un poussin !", EnemyName(e->type));
    if (G.chicks == 0) GameOver("Les mercenaires ont enleve tous les poussins !");
}

// Tourne progressivement le mercenaire vers la direction dir (pas de rotation instantanee)
static void TurnTowards(Enemy *e, Vector3 dir, float dt)
{
    float dy = atan2f(dir.x, dir.z) - e->yaw;   // ecart d'angle a combler
    // Ramene l'ecart dans [-PI, PI] pour tourner par le chemin le plus court
    while (dy > PI) dy -= 2*PI;
    while (dy < -PI) dy += 2*PI;
    e->yaw += dy*fminf(1.0f, dt*8.0f);   // comble ~8x l'ecart par seconde (lissage exponentiel)
}

// Une frame d'IA pour un mercenaire vivant (hote / solo uniquement)
static void UpdateEnemy(Enemy *e, float dt)
{
    float rad = EnemyRadius(e);
    float dPlayer;
    Player *target = NearestAlivePlayer(e->pos, &dPlayer);   // peut etre NULL (tous morts)
    // Encore dehors (derriere une porte) : il doit d'abord entrer dans l'arene
    bool outside = fabsf(e->pos.x) > ARENA - 0.5f || fabsf(e->pos.z) > ARENA - 0.5f;

    // Attaque au corps a corps
    // A portee si assez pres horizontalement ET si le joueur n'a pas saute plus haut que sa tete
    bool inReach = target && dPlayer < rad + PLAYER_RADIUS + 0.5f && target->pos.y < 1.6f*e->sz;
    if (inReach && e->attackCd <= 0)
    {
        e->attackCd = 1.3f;     // un coup toutes les 1.3 s
        e->attackAnim = 1.0f;
        PlayerHurt(target, e->dmg);
    }

    // Attrape un poussin : reste sur place sauf si un joueur s'approche
    if (e->state == ES_GRAB)
    {
        // Reste tourne vers le point du comptoir le plus proche
        Vector3 cp = CounterPoint(e->pos);
        e->yaw = atan2f(cp.x - e->pos.x, cp.z - e->pos.z);
        // Pas de poussin libre (tous deja dans les mains d'autres) : il cherche encore
        // startsHold : cette frame est celle ou il passerait de "fouille" a "tient le poussin".
        // On ne le laisse franchir ce cap que s'il reste un poussin non saisi sur le comptoir.
        bool startsHold = e->grabT <= GRAB_HOLD && e->grabT + dt > GRAB_HOLD;
        if (!startsHold || EnemiesHoldingChicks() < G.chicks) e->grabT += dt;
        if (target && dPlayer < 3.0f) e->state = ES_WALK;   // un joueur approche : il lache tout pour se battre
        else if (e->grabT > GRAB_TIME) { e->grabT = 0; StealChick(e); }   // poussin emporte, il recommence
        return;
    }

    // Choix de la destination
    // Priorite : 1) entrer dans l'arene, 2) poursuivre un joueur proche
    // (toujours pour l'Eclaireur), 3) sinon aller au comptoir.
    Vector3 dest;
    bool goStand = false;   // true si la destination est le comptoir
    if (outside) dest = V3(0, 0, 0);   // franchir la porte d'abord
    else if (target && (e->type == E_FAST || dPlayer < AGGRO_RANGE)) dest = V3(target->pos.x, 0, target->pos.z);
    else { dest = CounterPoint(e->pos); goStand = true; }

    Vector3 to = Vector3Subtract(dest, e->pos);
    to.y = 0;   // deplacement uniquement horizontal
    float dist = Vector3Length(to);

    if (goStand && dist < rad + 0.1f)   // assez pres pour se pencher sur le comptoir
    {
        e->state = ES_GRAB;
        e->grabT = 0;
        return;
    }

    // Deja au contact du joueur : il ne bouge plus, il le regarde et frappe (voir plus haut)
    if (!goStand && !outside && inReach)
    {
        e->yaw = atan2f(target->pos.x - e->pos.x, target->pos.z - e->pos.z);
        return;
    }
    if (dist < 0.01f) return;   // deja arrive (evite une division par ~0 ci-dessous)

    // Avance ; si bloque contre un obstacle, contourne par le cote
    float spd = e->speed/(0.7f + 0.3f*e->fat);   // plus gros = plus lent
    Vector3 dir = Vector3Scale(to, 1.0f/dist);   // direction normalisee vers la destination
    // Bloque depuis 0.4 s : on ajoute une composante perpendiculaire (-z, x) = rotation de 90 deg,
    // du cote sideSign, pour glisser le long de l'obstacle
    if (e->stuckT > 0.4f) dir = Vector3Normalize(Vector3Add(dir, V3(-dir.z*e->sideSign*1.5f, 0, dir.x*e->sideSign*1.5f)));

    Vector3 old = e->pos;
    e->pos = Vector3Add(e->pos, Vector3Scale(dir, spd*dt));
    ResolveCircle(&e->pos, rad, 0.0f, 2.0f*e->sz);   // collisions avec murs et decor
    // S'il a avance de moins de 30 % de la distance prevue, il est considere comme bloque
    if (Vector3Distance(old, e->pos) < spd*dt*0.3f) e->stuckT += dt;
    else e->stuckT = fmaxf(0, e->stuckT - dt*0.5f);
    // Toujours bloque apres 2 s : il essaie de contourner par l'autre cote
    if (e->stuckT > 2.0f) { e->stuckT = 0; e->sideSign = -e->sideSign; }

    TurnTowards(e, dir, dt);
    e->phase += dt*spd*3.2f/e->sz;   // cadence des pas proportionnelle a la vitesse (les grands font de plus grands pas)
}

// Les mercenaires se repoussent entre eux
// Pour chaque paire qui se chevauche, on ecarte les deux de la moitie du
// chevauchement chacun. Le chef (poids 0.1) ne se laisse presque pas pousser.
static void SeparateEnemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *a = &enemies[i];
        if (!a->active || a->state == ES_DEAD) continue;
        for (int j = i + 1; j < MAX_ENEMIES; j++)   // j > i : chaque paire n'est traitee qu'une fois
        {
            Enemy *b = &enemies[j];
            if (!b->active || b->state == ES_DEAD) continue;
            float dx = b->pos.x - a->pos.x, dz = b->pos.z - a->pos.z;
            float rr = (EnemyRadius(a) + EnemyRadius(b))*0.9f;   // 0.9 : on tolere un leger chevauchement
            float d2 = dx*dx + dz*dz;   // on compare les distances au carre (evite un sqrt inutile)
            if (d2 >= rr*rr || d2 < 1e-6f) continue;   // pas de contact (ou exactement superposes : direction indefinie)
            float d = sqrtf(d2), push = (rr - d)*0.5f;
            float wa = (a->type == E_BOSS) ? 0.1f : 1.0f, wb = (b->type == E_BOSS) ? 0.1f : 1.0f;
            // (dx, dz)/d = direction unitaire de a vers b
            a->pos.x -= dx/d*push*wa; a->pos.z -= dz/d*push*wa;
            b->pos.x += dx/d*push*wb; b->pos.z += dz/d*push*wb;
        }
    }
}

// Les mercenaires repoussent les joueurs (en reseau, chacun gere le sien)
// localOnly : true chez un client, qui ne deplace que ses propres joueurs locaux.
// Un joueur perche au-dessus du mercenaire (saut, caisse) n'est pas repousse.
void EnemiesPushPlayers(bool localOnly)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *a = &enemies[i];
        if (!a->active || a->state == ES_DEAD) continue;
        for (int k = 0; k < numPlayers; k++)
        {
            Player *p = &players[k];
            if (!p->connected || !p->alive || (localOnly && !p->local) || p->pos.y >= 1.5f*a->sz) continue;
            float dx = p->pos.x - a->pos.x, dz = p->pos.z - a->pos.z;
            float rr = EnemyRadius(a) + PLAYER_RADIUS;
            float d2 = dx*dx + dz*dz;
            if (d2 >= rr*rr || d2 < 1e-6f) continue;
            float d = sqrtf(d2);
            // Seul le joueur est deplace, de tout le chevauchement (le mercenaire est "lourd")
            p->pos.x += dx/d*(rr - d);
            p->pos.z += dz/d*(rr - d);
        }
    }
}

// Mise a jour de tous les mercenaires (hote ou partie solo) : timers, IA,
// corps morts, puis separation et poussee des joueurs.
void EnemiesUpdate(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        // Timers d'animation et de recharge
        e->hitFlash = fmaxf(0, e->hitFlash - dt*6);        // le flash dure ~1/6 s
        e->attackAnim = fmaxf(0, e->attackAnim - dt*2.5f); // l'animation de frappe dure ~0.4 s
        e->attackCd -= dt;
        if (e->state == ES_DEAD)
        {
            if (e->launched) UpdateLaunched(e, dt, true);
            else e->deadT += dt;
            // Le corps disparait 3.5 s apres etre au sol (il s'enfonce dans le sol au rendu)
            if (!e->airborne && e->deadT > 3.5f) e->active = false;
            continue;
        }
        UpdateEnemy(e, dt);
    }
    SeparateEnemies();
    EnemiesPushPlayers(false);
}

// Mise a jour cote client reseau : pas d'IA, on suit les positions recues
void EnemiesClientUpdate(float dt)
{
    // L'etat vient de l'hote ~30 fois/s : on lisse la position et on anime
    // localement entre deux paquets.
    // k : fraction du chemin restant parcourue cette frame (lissage exponentiel independant du framerate)
    float k = fminf(1.0f, dt*12.0f);
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        e->hitFlash = fmaxf(0, e->hitFlash - dt*6);
        e->attackAnim = fmaxf(0, e->attackAnim - dt*2.5f);
        e->predictT -= dt;
        if (e->state == ES_DEAD)
        {
            if (e->launched && e->predictT > 0) UpdateLaunched(e, dt, false);   // vol predit localement
            else if (e->launched)
            {
                // Prediction terminee : on rejoint en douceur la position donnee par l'hote
                e->pos = Vector3Lerp(e->pos, e->netPos, k);
                e->spin += (e->netSpin - e->spin)*k;
                if (!e->airborne) e->deadT += dt;
            }
            else e->deadT += dt;
            continue;
        }
        if (e->state == ES_GRAB) e->grabT += dt;   // pour l'animation (fouille / poussin en main)

        Vector3 old = e->pos;
        e->pos = Vector3Lerp(e->pos, e->netPos, k);
        // Meme lissage pour l'angle, par le chemin le plus court
        float dy = e->netYaw - e->yaw;
        while (dy > PI) dy -= 2*PI;
        while (dy < -PI) dy += 2*PI;
        e->yaw += dy*k;
        // La phase de marche avance avec la distance reellement parcourue,
        // avec le meme facteur que cote hote (vitesse divisee par la carrure)
        float moved = Vector3Distance(old, e->pos);
        if (e->state == ES_WALK) e->phase += moved*3.2f/(e->sz*(0.7f + 0.3f*e->fat));
    }
    EnemiesPushPlayers(true);
}
