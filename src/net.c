/*
 * net.c - Protocole reseau (voir net.h pour le principe general).
 *
 * Paquets (tous commencent par NET_MAGIC puis un octet de type) :
 *   HELLO    client -> hote : demande a rejoindre
 *   WELCOME  hote -> client : numero de joueur attribue
 *   REFUSE   hote -> client : partie pleine / version differente
 *   STATE    client -> hote : position, visee, arme, touches, effets
 *   SNAPSHOT hote -> client : etat complet du monde
 *   BYE      dans les deux sens : depart
 *
 * Les touches sur les gloutons doivent arriver a coup sur : le client les
 * renvoie dans chaque paquet jusqu'a ce que l'hote en accuse reception.
 * Les effets (sons, particules) peuvent se perdre sans gravite.
 * Toutes les cibles (x86, ARM) etant little-endian, les nombres sont copies tels quels.
 */
#include "net.h"
#include "netsock.h"
#include "config.h"
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "pickup.h"
#include "effects.h"
#include "hud.h"
#include "audio.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_MAGIC    0x48535242u   // "BRSH"
#define NET_VERSION  1
#define PACKET_MAX   8192
#define MAX_EVENTS   64
#define MAX_HITS     32
#define FEED_RING    8

enum { PKT_HELLO = 1, PKT_WELCOME, PKT_REFUSE, PKT_STATE, PKT_SNAPSHOT, PKT_BYE };

typedef struct {
    uint8_t type, origin, param;   // origin = joueur a l'origine (ne lui est pas renvoye)
    Vector3 a, b;
    float f1, f2;
} NetEvent;

typedef struct {
    uint32_t id;
    uint8_t enemy, head;
    float dmg;
    Vector3 pos, dir;
} NetHit;

typedef struct { uint32_t id; Color c; char text[96]; } NetFeedMsg;

typedef struct {
    bool used;
    NetAddr addr;
    float silence;      // secondes depuis le dernier paquet
    uint32_t lastHit;   // derniere touche appliquee
} RemoteClient;

static NetRole role;
static NetStatus status;
static char error[96];
static int localId;
static float sendT, silenceT, helloT;
static bool suppress;
static int recordOrigin;

// Hote
static RemoteClient clients[MAX_PLAYERS];
static NetFeedMsg feedRing[FEED_RING];
static uint32_t nextFeedId = 1;

// Client
static NetAddr hostAddr;
static NetHit hits[MAX_HITS];
static int hitCount;
static uint32_t nextHitId;
static uint32_t lastFeedSeen;
static bool firstSnapshot;

// Evenements en attente d'envoi
static NetEvent events[MAX_EVENTS];
static int eventCount;

//----------------------------------------------------------------------------------
// Lecture / ecriture de paquets
//----------------------------------------------------------------------------------
typedef struct { uint8_t *data; int pos, size; bool err; } Buf;

static void Put(Buf *b, const void *p, int n)
{
    if (b->pos + n > b->size) { b->err = true; return; }
    memcpy(b->data + b->pos, p, n);
    b->pos += n;
}
static void PutU8(Buf *b, int v) { uint8_t x = (uint8_t)v; Put(b, &x, 1); }
static void PutI16(Buf *b, int v) { int16_t x = (int16_t)v; Put(b, &x, 2); }
static void PutU32(Buf *b, uint32_t v) { Put(b, &v, 4); }
static void PutI32(Buf *b, int v) { int32_t x = v; Put(b, &x, 4); }
static void PutF32(Buf *b, float v) { Put(b, &v, 4); }
static void PutVec3(Buf *b, Vector3 v) { PutF32(b, v.x); PutF32(b, v.y); PutF32(b, v.z); }
static void PutStr(Buf *b, const char *s)
{
    int n = (int)strlen(s);
    if (n > 95) n = 95;
    PutU8(b, n);
    Put(b, s, n);
}

static void Get(Buf *b, void *p, int n)
{
    if (b->pos + n > b->size) { b->err = true; memset(p, 0, n); return; }
    memcpy(p, b->data + b->pos, n);
    b->pos += n;
}
static int GetU8(Buf *b) { uint8_t x; Get(b, &x, 1); return x; }
static int GetI16(Buf *b) { int16_t x; Get(b, &x, 2); return x; }
static uint32_t GetU32(Buf *b) { uint32_t x; Get(b, &x, 4); return x; }
static int GetI32(Buf *b) { int32_t x; Get(b, &x, 4); return x; }
static float GetF32(Buf *b) { float x; Get(b, &x, 4); return x; }
static Vector3 GetVec3(Buf *b) { Vector3 v; v.x = GetF32(b); v.y = GetF32(b); v.z = GetF32(b); return v; }
static void GetStr(Buf *b, char *out, int cap)
{
    int n = GetU8(b);
    if (n >= cap) n = cap - 1;
    Get(b, out, n);
    out[b->err ? 0 : n] = '\0';
}

static void BeginPacket(Buf *b, uint8_t *storage, int type)
{
    *b = (Buf){ storage, 0, PACKET_MAX, false };
    PutU32(b, NET_MAGIC);
    PutU8(b, type);
}

static void SendSimple(NetAddr to, int type)
{
    uint8_t data[16];
    Buf b;
    BeginPacket(&b, data, type);
    b.size = sizeof(data);
    SockSend(to, b.data, b.pos);
}

static void PutEvents(Buf *b)
{
    PutU8(b, eventCount);
    for (int i = 0; i < eventCount; i++)
    {
        const NetEvent *e = &events[i];
        PutU8(b, e->type); PutU8(b, e->origin); PutU8(b, e->param);
        PutVec3(b, e->a); PutVec3(b, e->b);
        PutF32(b, e->f1); PutF32(b, e->f2);
    }
}

static NetEvent GetEvent(Buf *b)
{
    NetEvent e;
    e.type = (uint8_t)GetU8(b); e.origin = (uint8_t)GetU8(b); e.param = (uint8_t)GetU8(b);
    e.a = GetVec3(b); e.b = GetVec3(b);
    e.f1 = GetF32(b); e.f2 = GetF32(b);
    return e;
}

// Rejoue localement un effet venu d'une autre machine
static void ApplyEvent(const NetEvent *e)
{
    suppress = true;
    switch (e->type)
    {
        case EV_TRACER: AddTracer(e->a, e->b); break;
        case EV_SAUCE:  BurstSauce(e->a, e->b, e->param); break;
        case EV_SPARKS: BurstSparks(e->a, e->b); break;
        case EV_CRUMBS: BurstCrumbs(e->a); break;
        case EV_SOUND:
            if (e->param < SND_COUNT)   // les tirs des autres un peu moins forts que les siens
                PlaySfx((SndId)e->param, e->f1, e->param == SND_SHOT ? e->f2*0.6f : e->f2);
            break;
        default: break;
    }
    suppress = false;
}

static void QueueEvent(NetEvent e)
{
    if (eventCount < MAX_EVENTS) events[eventCount++] = e;
}

//----------------------------------------------------------------------------------
// API generale
//----------------------------------------------------------------------------------
NetRole NetGetRole(void) { return role; }
NetStatus NetGetStatus(void) { return status; }
const char *NetError(void) { return error; }
int NetLocalId(void) { return localId; }
bool NetIsClient(void) { return role == NET_CLIENT; }
bool NetIsAuthority(void) { return role != NET_CLIENT; }
void NetSuppress(bool on) { suppress = on; }

static void ResetState(void)
{
    memset(clients, 0, sizeof(clients));
    memset(feedRing, 0, sizeof(feedRing));
    eventCount = hitCount = 0;
    sendT = silenceT = helloT = 0;
    lastFeedSeen = 0;
    suppress = false;
    error[0] = '\0';
}

bool NetHost(void)
{
    NetStop();
    ResetState();
    if (!SockOpen(NET_PORT))
    {
        snprintf(error, sizeof(error), "Impossible d'ouvrir le port %d (deja utilise ?)", NET_PORT);
        return false;
    }
    role = NET_HOST;
    status = NET_CONNECTED;
    localId = recordOrigin = 0;
    return true;
}

bool NetJoin(const char *address)
{
    NetStop();
    ResetState();

    // "ip" ou "ip:port"
    char host[64];
    snprintf(host, sizeof(host), "%s", address);
    unsigned short port = NET_PORT;
    char *colon = strchr(host, ':');
    if (colon) { *colon = '\0'; port = (unsigned short)atoi(colon + 1); }
    if (!host[0] || !SockResolve(host, port, &hostAddr))
    {
        snprintf(error, sizeof(error), "Adresse invalide : %s", address);
        status = NET_FAILED;
        return false;
    }
    if (!SockOpen(0))
    {
        snprintf(error, sizeof(error), "Impossible d'ouvrir une socket");
        status = NET_FAILED;
        return false;
    }
    role = NET_CLIENT;
    status = NET_CONNECTING;
    localId = -1;
    return true;
}

void NetStop(void)
{
    if (role == NET_HOST)
    {
        for (int i = 0; i < MAX_PLAYERS; i++) if (clients[i].used) SendSimple(clients[i].addr, PKT_BYE);
    }
    else if (role == NET_CLIENT && status == NET_CONNECTED) SendSimple(hostAddr, PKT_BYE);
    SockClose();
    role = NET_OFF;
    status = NET_IDLE;
}

//----------------------------------------------------------------------------------
// Enregistrement
//----------------------------------------------------------------------------------
void NetRecordEvent(NetEventType type, Vector3 a, Vector3 b, int param, float f1, float f2)
{
    if (role == NET_OFF || suppress || status != NET_CONNECTED) return;
    NetEvent e = { (uint8_t)type, (uint8_t)(role == NET_HOST ? recordOrigin : localId), (uint8_t)param, a, b, f1, f2 };
    QueueEvent(e);
}

void NetRecordFeed(Color c, const char *text)
{
    if (role != NET_HOST || suppress) return;
    for (int i = FEED_RING - 1; i > 0; i--) feedRing[i] = feedRing[i - 1];
    feedRing[0].id = nextFeedId++;
    feedRing[0].c = c;
    snprintf(feedRing[0].text, sizeof(feedRing[0].text), "%s", text);
}

void NetReportHit(int enemy, float dmg, bool head, Vector3 pos, Vector3 dir)
{
    if (role != NET_CLIENT) return;
    if (hitCount == MAX_HITS)   // file pleine : on oublie la plus ancienne
    {
        memmove(hits, hits + 1, (MAX_HITS - 1)*sizeof(NetHit));
        hitCount--;
    }
    hits[hitCount++] = (NetHit){ ++nextHitId, (uint8_t)enemy, (uint8_t)head, dmg, pos, dir };
}

//----------------------------------------------------------------------------------
// Hote
//----------------------------------------------------------------------------------
static int FindClient(NetAddr a)
{
    for (int i = 1; i < MAX_PLAYERS; i++) if (clients[i].used && SockAddrEqual(clients[i].addr, a)) return i;
    return -1;
}

static void DropClient(int id, const char *why)
{
    clients[id].used = false;
    PlayerLeave(id);
    FeedAdd(GRAY, "J%d %s", id + 1, why);
}

static void SendRefuse(NetAddr to, const char *reason)
{
    uint8_t data[128];
    Buf b;
    BeginPacket(&b, data, PKT_REFUSE);
    b.size = sizeof(data);
    PutStr(&b, reason);
    SockSend(to, b.data, b.pos);
}

static void HostHandleHello(Buf *b, NetAddr from)
{
    int version = GetU8(b);
    if (version != NET_VERSION) { SendRefuse(from, "Version du jeu differente de celle de l'hote"); return; }

    int id = FindClient(from);
    if (id < 0)
    {
        for (int i = 1; i < MAX_PLAYERS && id < 0; i++) if (!clients[i].used && !players[i].connected) id = i;
        if (id < 0) { SendRefuse(from, "La partie est pleine (4 joueurs)"); return; }
        clients[id] = (RemoteClient){ true, from, 0, 0 };
        PlayerJoin(id);
    }
    uint8_t data[16];
    Buf out;
    BeginPacket(&out, data, PKT_WELCOME);
    out.size = sizeof(data);
    PutU8(&out, id);
    SockSend(from, out.data, out.pos);
}

static void HostApplyHit(int id, const NetHit *h)
{
    if (h->enemy >= MAX_ENEMIES) return;
    Enemy *e = &enemies[h->enemy];
    if (!e->active || e->state == ES_DEAD) return;
    recordOrigin = id;   // les effets produits ne seront pas renvoyes au tireur
    EnemyDamage(e, fminf(h->dmg, BULLET_DAMAGE*HEADSHOT_MULT), h->head, h->pos, h->dir, &players[id]);
    recordOrigin = localId;
}

static void HostHandleState(Buf *b, int id)
{
    Player *p = &players[id];
    int round = GetU8(b), respawnSeen = GetU8(b);
    Vector3 pos = GetVec3(b), vel = GetVec3(b);
    float yaw = GetF32(b), pitch = GetF32(b), bob = GetF32(b);
    int flags = GetU8(b);
    float muzzleT = GetF32(b), reloadT = GetF32(b), reloadDur = GetF32(b), ads = GetF32(b), sprintT = GetF32(b);
    int ammo = GetI16(b), reserve = GetI16(b);
    if (b->err) return;

    // Paquet d'une partie precedente ou d'avant la reapparition : position perimee
    bool fresh = (round == (G.round & 0xFF)) && (respawnSeen == (p->respawnCount & 0xFF));
    if (fresh)
    {
        p->netPos = pos;
        p->vel = vel;
        p->netYaw = yaw;
        p->netPitch = pitch;
        p->bob = bob;
        p->grounded = flags & 1;
        p->sprinting = flags & 2;
        p->w.reloading = flags & 4;
        p->w.muzzleT = muzzleT;
        p->w.reloadT = reloadT;
        p->w.reloadDur = reloadDur;
        p->w.ads = ads;
        p->w.sprintT = sprintT;
        p->w.ammo = ammo;
        p->w.reserve = reserve;
    }

    int nh = GetU8(b);
    for (int i = 0; i < nh && !b->err; i++)
    {
        NetHit h;
        h.id = GetU32(b); h.enemy = (uint8_t)GetU8(b); h.head = (uint8_t)GetU8(b); h.dmg = GetF32(b);
        h.pos = GetVec3(b); h.dir = GetVec3(b);
        if (b->err || h.id <= clients[id].lastHit) continue;
        clients[id].lastHit = h.id;
        if (fresh && G.state == ST_PLAY) HostApplyHit(id, &h);
    }

    int ne = GetU8(b);
    for (int i = 0; i < ne && !b->err; i++)
    {
        NetEvent e = GetEvent(b);
        if (b->err) break;
        e.origin = (uint8_t)id;
        ApplyEvent(&e);
        QueueEvent(e);   // retransmis aux autres clients
    }
}

static void HostReceive(int type, Buf *b, NetAddr from)
{
    int id = FindClient(from);
    if (id >= 0) clients[id].silence = 0;
    switch (type)
    {
        case PKT_HELLO: HostHandleHello(b, from); break;
        case PKT_STATE: if (id >= 0) HostHandleState(b, id); break;
        case PKT_BYE:   if (id >= 0) DropClient(id, "a quitte la partie"); break;
        default: break;
    }
}

static void PutPlayer(Buf *b, const Player *p)
{
    int flags = (p->connected ? 1 : 0) | (p->alive ? 2 : 0) | (p->grounded ? 4 : 0) | (p->w.reloading ? 8 : 0) | (p->sprinting ? 16 : 0);
    PutU8(b, flags);
    PutF32(b, p->hp);
    PutI16(b, p->kills); PutI16(b, p->headshots);
    PutI32(b, p->ammoGrant);
    PutU8(b, p->respawnCount);
    PutVec3(b, p->pos);
    PutF32(b, p->vel.x); PutF32(b, p->vel.z);
    PutF32(b, p->yaw); PutF32(b, p->pitch); PutF32(b, p->bob);
    PutF32(b, p->w.muzzleT); PutF32(b, p->w.reloadT); PutF32(b, p->w.reloadDur);
    PutF32(b, p->w.ads); PutF32(b, p->w.sprintT);
}

static void PutEnemy(Buf *b, int idx, const Enemy *e)
{
    PutU8(b, idx); PutU8(b, e->type); PutU8(b, e->state); PutU8(b, e->hasCap);
    PutU8(b, e->shirt); PutU8(b, e->skin); PutU8(b, e->pants); PutU8(b, e->cap);
    PutF32(b, e->pos.x); PutF32(b, e->pos.z); PutF32(b, e->yaw);
    PutF32(b, e->hp); PutF32(b, e->sz); PutF32(b, e->fat);
    PutF32(b, e->eatT); PutF32(b, e->deadT);
    PutU8(b, (int)(Clamp(e->hitFlash, 0, 1)*255)); PutU8(b, (int)(Clamp(e->attackAnim, 0, 1)*255));
}

static void HostSendSnapshot(int id, uint8_t *data)
{
    Buf b;
    BeginPacket(&b, data, PKT_SNAPSHOT);
    PutU8(&b, id);
    PutU32(&b, clients[id].lastHit);

    // Partie
    PutU8(&b, G.state); PutU8(&b, G.round);
    PutI16(&b, G.wave); PutI16(&b, G.toSpawn); PutI16(&b, G.alive);
    PutU8(&b, (G.bossPending ? 1 : 0) | (G.inIntermission ? 2 : 0));
    PutF32(&b, G.interT); PutF32(&b, G.bannerT);
    PutI32(&b, G.score); PutU8(&b, G.burgers);
    PutStr(&b, G.overReason);

    for (int i = 0; i < MAX_PLAYERS; i++) PutPlayer(&b, &players[i]);

    // Pour chaque liste : un octet de nombre d'elements, complete apres coup
    int countPos = b.pos, count = 0;
    PutU8(&b, 0);
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        if (!enemies[i].active) continue;
        PutEnemy(&b, i, &enemies[i]);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    countPos = b.pos;
    PutU8(&b, 0);
    count = 0;
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        int type;
        Vector3 pos;
        float t;
        if (!PickupGet(i, &type, &pos, &t)) continue;
        PutU8(&b, i); PutU8(&b, type); PutF32(&b, pos.x); PutF32(&b, pos.z); PutF32(&b, t);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    countPos = b.pos;
    PutU8(&b, 0);
    count = 0;
    for (int i = 0; i < FEED_RING; i++)
    {
        if (!feedRing[i].id) continue;
        PutU32(&b, feedRing[i].id);
        PutU8(&b, feedRing[i].c.r); PutU8(&b, feedRing[i].c.g); PutU8(&b, feedRing[i].c.b);
        PutStr(&b, feedRing[i].text);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    PutEvents(&b);
    if (!b.err) SockSend(clients[id].addr, b.data, b.pos);
}

static void HostUpdate(float dt)
{
    for (int i = 1; i < MAX_PLAYERS; i++)
    {
        if (!clients[i].used) continue;
        clients[i].silence += dt;
        if (clients[i].silence > NET_TIMEOUT) DropClient(i, "a perdu la connexion");
    }

    sendT -= dt;
    if (sendT > 0) return;
    sendT = 1.0f/NET_SNAPSHOT_RATE;
    static uint8_t data[PACKET_MAX];
    for (int i = 1; i < MAX_PLAYERS; i++) if (clients[i].used) HostSendSnapshot(i, data);
    eventCount = 0;
}

//----------------------------------------------------------------------------------
// Client
//----------------------------------------------------------------------------------
static void ClientApplyLocalPlayer(Player *p, int flags, float hp, int ammoGrant, int respawnCount, Vector3 pos)
{
    bool alive = flags & 2;
    if (alive && p->alive && hp < p->hp - 0.5f)   // degats subis : retour visuel et sonore
    {
        p->hurtT = 1.0f;
        p->shake = fmaxf(p->shake, 0.3f);
        p->regenDelay = 5.0f;
        PlaySfx(SND_HURT, 1.0f, 0.8f);
    }
    if (p->alive && !alive) p->deadT = 0;
    p->hp = hp;
    p->alive = alive;

    if (respawnCount != (p->respawnSeen & 0xFF))   // l'hote nous a fait reapparaitre
    {
        p->respawnSeen = respawnCount;
        p->pos = pos;
        p->vel = V3(0, 0, 0);
        p->yaw = PI;
        p->pitch = -0.08f;
        PlayerRefillWeapon(p);
    }
    if (ammoGrant != p->ammoGrantSeen)
    {
        int d = ammoGrant - p->ammoGrantSeen;
        p->ammoGrantSeen = ammoGrant;
        if (d > 0)
        {
            p->w.reserve = (p->w.reserve + d > MAX_RESERVE) ? MAX_RESERVE : p->w.reserve + d;
            PlaySfx(SND_PICK, 1.0f, 0.6f);
        }
    }
}

static void ClientReadPlayer(Buf *b, int i)
{
    Player *p = &players[i];
    int flags = GetU8(b);
    float hp = GetF32(b);
    int kills = GetI16(b), headshots = GetI16(b);
    int ammoGrant = GetI32(b);
    int respawnCount = GetU8(b);
    Vector3 pos = GetVec3(b);
    float vx = GetF32(b), vz = GetF32(b);
    float yaw = GetF32(b), pitch = GetF32(b), bob = GetF32(b);
    float muzzleT = GetF32(b), reloadT = GetF32(b), reloadDur = GetF32(b), ads = GetF32(b), sprintT = GetF32(b);
    if (b->err) return;

    bool connected = flags & 1;
    if (i == localId)
    {
        p->kills = kills;
        p->headshots = headshots;
        ClientApplyLocalPlayer(p, flags, hp, ammoGrant, respawnCount, pos);
        return;
    }
    if (connected && !p->connected) p->pos = pos;   // nouveau joueur : pas de lissage
    p->connected = connected;
    p->local = false;
    p->id = i;
    p->alive = flags & 2;
    p->grounded = flags & 4;
    p->w.reloading = flags & 8;
    p->sprinting = flags & 16;
    p->hp = hp;
    p->kills = kills;
    p->headshots = headshots;
    p->netPos = pos;
    p->vel = V3(vx, 0, vz);
    p->netYaw = yaw;
    p->netPitch = pitch;
    p->bob = bob;
    if (muzzleT > 0) p->w.muzzleT = muzzleT;
    p->w.reloadT = reloadT;
    p->w.reloadDur = reloadDur;
    p->w.ads = ads;
    p->w.sprintT = sprintT;
}

static void ClientReadEnemies(Buf *b)
{
    bool seen[MAX_ENEMIES] = { 0 };
    int count = GetU8(b);
    for (int n = 0; n < count && !b->err; n++)
    {
        int idx = GetU8(b), type = GetU8(b), state = GetU8(b), hasCap = GetU8(b);
        int shirt = GetU8(b), skin = GetU8(b), pants = GetU8(b), cap = GetU8(b);
        float x = GetF32(b), z = GetF32(b), yaw = GetF32(b);
        float hp = GetF32(b), sz = GetF32(b), fat = GetF32(b), eatT = GetF32(b), deadT = GetF32(b);
        float hitFlash = GetU8(b)/255.0f, attackAnim = GetU8(b)/255.0f;
        if (b->err || idx >= MAX_ENEMIES) break;

        Enemy *e = &enemies[idx];
        seen[idx] = true;
        bool fresh = !e->active || e->type != (EnemyType)type || (e->state == ES_DEAD && state != ES_DEAD && e->predictT <= 0);
        if (e->predictT > 0 && state != ES_DEAD) continue;   // on garde la mort predite en attendant l'hote

        if (state == ES_DEAD && e->state != ES_DEAD) e->deadT = deadT;
        if (state == ES_EAT && e->state != ES_EAT) e->eatT = eatT;
        e->active = true;
        e->type = (EnemyType)type;
        e->state = (EnemyState)state;
        e->hasCap = hasCap;
        e->shirt = (unsigned char)shirt; e->skin = (unsigned char)skin;
        e->pants = (unsigned char)pants; e->cap = (unsigned char)cap;
        e->hp = hp; e->sz = sz; e->fat = fat;
        e->hitFlash = fmaxf(e->hitFlash, hitFlash);
        e->attackAnim = fmaxf(e->attackAnim, attackAnim);
        e->netPos = V3(x, 0, z);
        e->netYaw = yaw;
        if (fresh)
        {
            e->pos = e->netPos;
            e->yaw = yaw;
            e->deadT = deadT;
            e->predictT = 0;
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) if (!seen[i]) enemies[i].active = false;
}

static void ClientReadPickups(Buf *b)
{
    bool seen[MAX_PICKUPS] = { 0 };
    int count = GetU8(b);
    for (int n = 0; n < count && !b->err; n++)
    {
        int idx = GetU8(b), type = GetU8(b);
        float x = GetF32(b), z = GetF32(b), t = GetF32(b);
        if (b->err || idx >= MAX_PICKUPS) break;
        seen[idx] = true;
        PickupSet(idx, true, type, V3(x, 0, z), t);
    }
    for (int i = 0; i < MAX_PICKUPS; i++) if (!seen[i]) PickupSet(i, false, 0, V3(0, 0, 0), 0);
}

static void ClientReadFeed(Buf *b)
{
    NetFeedMsg msgs[FEED_RING];
    int count = GetU8(b);
    if (count > FEED_RING) { b->err = true; return; }
    for (int n = 0; n < count; n++)
    {
        msgs[n].id = GetU32(b);
        msgs[n].c = (Color){ (unsigned char)GetU8(b), (unsigned char)GetU8(b), (unsigned char)GetU8(b), 255 };
        GetStr(b, msgs[n].text, sizeof(msgs[n].text));
    }
    if (b->err) return;

    // Les messages arrivent du plus recent au plus ancien : on affiche les nouveaux dans l'ordre
    uint32_t newest = lastFeedSeen;
    for (int n = count - 1; n >= 0; n--)
    {
        if (msgs[n].id <= lastFeedSeen) continue;
        if (!firstSnapshot) FeedPush(msgs[n].c, msgs[n].text);   // pas d'historique a l'arrivee
        if (msgs[n].id > newest) newest = msgs[n].id;
    }
    lastFeedSeen = newest;
}

static void ClientHandleSnapshot(Buf *b)
{
    int id = GetU8(b);
    uint32_t ack = GetU32(b);
    if (b->err || id != localId) return;

    // Touches confirmees par l'hote
    int keep = 0;
    for (int i = 0; i < hitCount; i++) if (hits[i].id > ack) hits[keep++] = hits[i];
    hitCount = keep;

    GameState previous = G.state;
    int state = GetU8(b), round = GetU8(b);
    int wave = GetI16(b), toSpawn = GetI16(b), alive = GetI16(b);
    int flags = GetU8(b);
    float interT = GetF32(b), bannerT = GetF32(b);
    int score = GetI32(b), burgers = GetU8(b);
    char reason[96];
    GetStr(b, reason, sizeof(reason));
    if (b->err) return;

    bool newRound = (round != (G.round & 0xFF));
    G.state = (GameState)state;
    G.round = round;
    G.wave = wave; G.toSpawn = toSpawn; G.alive = alive;
    G.bossPending = flags & 1;
    G.inIntermission = flags & 2;
    G.interT = interT;
    G.bannerT = bannerT;
    G.score = score;
    G.burgers = burgers;
    snprintf(G.overReason, sizeof(G.overReason), "%s", reason);
    if (newRound) GameNetRoundStarted();

    for (int i = 0; i < MAX_PLAYERS && !b->err; i++) ClientReadPlayer(b, i);
    ClientReadEnemies(b);
    ClientReadPickups(b);
    ClientReadFeed(b);

    int ne = GetU8(b);
    for (int i = 0; i < ne && !b->err; i++)
    {
        NetEvent e = GetEvent(b);
        if (!b->err && e.origin != localId) ApplyEvent(&e);
    }
    firstSnapshot = false;
    if (G.state != previous) GameNetStateChanged(previous);
}

static void ClientReceive(int type, Buf *b, NetAddr from)
{
    if (!SockAddrEqual(from, hostAddr)) return;
    silenceT = 0;
    switch (type)
    {
        case PKT_WELCOME:
            if (status != NET_CONNECTING) break;
            localId = GetU8(b);
            if (b->err || localId <= 0 || localId >= MAX_PLAYERS) { localId = -1; break; }
            status = NET_CONNECTED;
            firstSnapshot = true;
            PlayersResetNet(localId);
            break;
        case PKT_REFUSE:
            GetStr(b, error, sizeof(error));
            status = NET_FAILED;
            break;
        case PKT_SNAPSHOT:
            if (status == NET_CONNECTED) ClientHandleSnapshot(b);
            break;
        case PKT_BYE:
            snprintf(error, sizeof(error), "L'hote a ferme la partie");
            status = NET_LOST;
            break;
        default: break;
    }
}

static void ClientSendState(void)
{
    static uint8_t data[PACKET_MAX];
    const Player *p = &players[localId];
    Buf b;
    BeginPacket(&b, data, PKT_STATE);
    PutU8(&b, G.round); PutU8(&b, p->respawnSeen);
    PutVec3(&b, p->pos); PutVec3(&b, p->vel);
    PutF32(&b, p->yaw); PutF32(&b, p->pitch); PutF32(&b, p->bob);
    PutU8(&b, (p->grounded ? 1 : 0) | (p->sprinting ? 2 : 0) | (p->w.reloading ? 4 : 0));
    PutF32(&b, p->w.muzzleT); PutF32(&b, p->w.reloadT); PutF32(&b, p->w.reloadDur);
    PutF32(&b, p->w.ads); PutF32(&b, p->w.sprintT);
    PutI16(&b, p->w.ammo); PutI16(&b, p->w.reserve);

    PutU8(&b, hitCount);
    for (int i = 0; i < hitCount; i++)
    {
        PutU32(&b, hits[i].id); PutU8(&b, hits[i].enemy); PutU8(&b, hits[i].head); PutF32(&b, hits[i].dmg);
        PutVec3(&b, hits[i].pos); PutVec3(&b, hits[i].dir);
    }
    PutEvents(&b);
    if (!b.err) SockSend(hostAddr, b.data, b.pos);
    eventCount = 0;
}

static void ClientUpdate(float dt)
{
    silenceT += dt;
    if (status == NET_CONNECTING)
    {
        helloT -= dt;
        if (helloT <= 0)
        {
            uint8_t data[16];
            Buf b;
            BeginPacket(&b, data, PKT_HELLO);
            b.size = sizeof(data);
            PutU8(&b, NET_VERSION);
            SockSend(hostAddr, b.data, b.pos);
            helloT = 0.5f;
        }
        if (silenceT > 8.0f)
        {
            snprintf(error, sizeof(error), "Pas de reponse de l'hote (adresse ? pare-feu ?)");
            status = NET_FAILED;
        }
        return;
    }
    if (status != NET_CONNECTED) return;
    if (silenceT > NET_TIMEOUT)
    {
        snprintf(error, sizeof(error), "Connexion perdue avec l'hote");
        status = NET_LOST;
        return;
    }
    sendT -= dt;
    if (sendT > 0) return;
    sendT = 1.0f/NET_STATE_RATE;
    ClientSendState();
}

//----------------------------------------------------------------------------------
// Boucle
//----------------------------------------------------------------------------------
void NetReceive(float dt)
{
    (void)dt;
    if (role == NET_OFF) return;
    static uint8_t data[PACKET_MAX];
    NetAddr from;
    int n;
    while ((n = SockRecv(data, sizeof(data), &from)) > 0)
    {
        Buf b = { data, 0, n, false };
        uint32_t magic = GetU32(&b);
        int type = GetU8(&b);
        if (b.err || magic != NET_MAGIC) continue;
        if (role == NET_HOST) HostReceive(type, &b, from);
        else ClientReceive(type, &b, from);
        if (role == NET_OFF) break;
    }
}

void NetSend(float dt)
{
    if (role == NET_HOST) HostUpdate(dt);
    else if (role == NET_CLIENT) ClientUpdate(dt);
}
