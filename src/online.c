/*
 * online.c - Client du serveur de parties (voir online.h et online_proto.h).
 *
 * Deroulement cote hote :
 *   OnlineHost() -> ZM_HOST toutes les 0,5 s jusqu'a ZM_HOSTED (code + jeton)
 *   puis ZM_STATUS chaque seconde (joueurs, vague) ; le serveur accuse chaque
 *   STATUS : sans accuse pendant 6 s, le lobby affiche "serveur injoignable".
 *   Si le serveur a redemarre (ZE_UNKNOWN), l'hote se declare de nouveau.
 * Cote client :
 *   OnlineJoin(code) -> ZM_JOIN toutes les 0,5 s jusqu'a ZM_JOINED ; ensuite
 *   les paquets du jeu partent dans des ZM_DATA. Les HELLO de net.c envoyes
 *   avant l'inscription sont simplement perdus (net.c les repete).
 * Liste : ZM_LIST toutes les 2 s sur l'ecran des parties (5 s au menu, pour le
 * nombre de parties affiche a cote de REJOINDRE).
 */
#include "online.h"
#include "online_proto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static OnlineState state;
static uint32_t key, token;       // cle de la demande en cours, jeton donne par le serveur
static int code;                  // code de la partie
static float retryT, waitT;       // prochaine repetition, temps d'attente d'une reponse
static float statusT, ackT;       // hote : prochain STATUS, temps depuis le dernier accuse
static float listT, listAge = 999.0f, seenT = 999.0f;   // liste : prochaine demande, age ; derniere reponse
static char error[96];

static int platform;
static char pseudo[24];
static int maxPlayers = 50;
static int stPlayers = 1, stWave, stMax = 50;
static bool stPlaying, listed = true, statusDirty;

static SockGame games[40];
static int gameCount, totalGames = -1, totalPlayers;

static unsigned short gone[64];   // joueurs relayes partis, en attente de OnlinePopGone
static int goneCount;

static unsigned char msg[ZCB_MSG_MAX];

//----------------------------------------------------------------------------------
// Outils
//----------------------------------------------------------------------------------
// Cle aleatoire (jamais 0) : xorshift initialise avec l'heure et une adresse memoire
static uint32_t NewKey(void)
{
    static uint32_t x;
    if (!x) x = (uint32_t)time(NULL) ^ (uint32_t)clock() ^ (uint32_t)(uintptr_t)&x ^ 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x ? x : 1;
}

typedef struct { unsigned char *p; int n, cap; bool err; } W;
static void Put(W *w, const void *d, int n)
{
    if (w->n + n > w->cap) { w->err = true; return; }
    memcpy(w->p + w->n, d, n);
    w->n += n;
}
static void PutU8(W *w, int v) { unsigned char x = (unsigned char)v; Put(w, &x, 1); }
static void PutU16(W *w, int v) { PutU8(w, v); PutU8(w, v >> 8); }
static void PutU32(W *w, uint32_t v) { PutU16(w, (int)(v & 0xFFFF)); PutU16(w, (int)(v >> 16)); }
static void PutStr(W *w, const char *s)
{
    int n = (int)strlen(s);
    if (n > ZCB_NAME_MAX) n = ZCB_NAME_MAX;
    PutU8(w, n);
    Put(w, s, n);
}
static W Begin(int type)
{
    W w = { msg, 0, sizeof(msg), false };
    PutU32(&w, ZCB_MAGIC);
    PutU8(&w, type);
    return w;
}
static bool Send(W *w) { return !w->err && OnlineTransportSend(w->p, w->n); }

typedef struct { const unsigned char *p; int n, pos; bool err; } R;
static int GetU8(R *r) { if (r->pos >= r->n) { r->err = true; return 0; } return r->p[r->pos++]; }
static int GetU16(R *r) { int a = GetU8(r); return a | (GetU8(r) << 8); }
static uint32_t GetU32(R *r) { uint32_t a = (uint32_t)GetU16(r); return a | ((uint32_t)GetU16(r) << 16); }
// Chaine : seulement l'ASCII imprimable (la police du jeu n'a rien d'autre)
static void GetStr(R *r, char *out, int cap)
{
    int n = GetU8(r), k = 0;
    for (int i = 0; i < n && !r->err; i++)
    {
        int c = GetU8(r);
        if (c >= 32 && c < 127 && k < cap - 1) out[k++] = (char)c;
    }
    out[k] = '\0';
}

static void SetError(const char *text) { snprintf(error, sizeof(error), "%s", text); }

//----------------------------------------------------------------------------------
// Envois
//----------------------------------------------------------------------------------
static void SendHost(void)
{
    char name[ZCB_NAME_MAX + 1] = "";
    if (pseudo[0]) snprintf(name, sizeof(name), "Partie de %.20s", pseudo);
    W w = Begin(ZM_HOST);
    PutU8(&w, ZCB_PROTO); PutU32(&w, key); PutU8(&w, NET_VERSION); PutU8(&w, maxPlayers);
    PutU8(&w, platform); PutU8(&w, listed ? ZG_LISTED : 0); PutStr(&w, name);
    Send(&w);
}

static void SendStatus(void)
{
    W w = Begin(ZM_STATUS);
    PutU32(&w, token); PutU8(&w, stPlayers); PutU8(&w, stMax); PutU16(&w, stWave);
    PutU8(&w, (stPlaying ? ZG_PLAYING : 0) | (listed ? ZG_LISTED : 0));
    Send(&w);
}

static void SendJoin(void)
{
    W w = Begin(ZM_JOIN);
    PutU8(&w, ZCB_PROTO); PutU32(&w, key); PutU16(&w, code);
    Send(&w);
}

// Demande de liste, completee par des zeros (le serveur ignore les demandes trop courtes)
static void SendList(void)
{
    W w = Begin(ZM_LIST);
    PutU8(&w, ZCB_PROTO);
    while (w.n < ZCB_LIST_MIN) PutU8(&w, 0);
    Send(&w);
}

//----------------------------------------------------------------------------------
// API
//----------------------------------------------------------------------------------
void OnlineSetIdentity(int plat, const char *name)
{
    platform = plat;
    // Pseudo : ASCII imprimable, sans '|', 20 caracteres au plus
    int k = 0;
    for (const char *s = name ? name : ""; *s && k < 20; s++)
        if (*s >= 32 && *s < 127 && *s != '|') pseudo[k++] = *s;
    pseudo[k] = '\0';
}

void OnlineHost(int maxP)
{
    OnlineLeave();
    maxPlayers = maxP;
    state = OL_HOST_WAIT;
    key = NewKey();
    retryT = waitT = 0;
    stPlayers = 1; stMax = maxP; stWave = 0; stPlaying = false;
}

void OnlineJoin(int c)
{
    OnlineLeave();
    state = OL_JOINING;
    code = c;
    key = NewKey();
    retryT = waitT = 0;
}

void OnlineLeave(void)
{
    if (state == OL_HOSTING || state == OL_JOINED)
    {
        W w = Begin(ZM_LEAVE);
        PutU32(&w, token);
        Send(&w);
    }
    state = OL_IDLE;
    code = 0;
    token = 0;
    goneCount = 0;
    error[0] = '\0';
}

void OnlineLinkLost(void)
{
    goneCount = 0;
    if (state == OL_HOSTING || state == OL_HOST_WAIT)
    {
        // Les joueurs relayes sont perdus ; la partie sera declaree de nouveau
        // des que la connexion reviendra (avec un nouveau code)
        state = OL_HOST_WAIT;
        key = NewKey();
        retryT = waitT = 0;
        code = 0;
    }
    else if (state == OL_JOINING || state == OL_JOINED)
    {
        state = OL_FAILED;
        SetError("Connexion au serveur de parties perdue");
    }
}

void OnlineTick(float dt, float listEvery)
{
    bool ready = OnlineTransportReady();
    seenT += dt;
    switch (state)
    {
        case OL_HOST_WAIT:
            waitT += dt;
            retryT -= dt;
            if (ready && retryT <= 0)
            {
                SendHost();
                retryT = waitT < 6.0f ? 0.5f : 2.0f;   // plus calme si le serveur ne repond pas
            }
            if (waitT > 6.0f) SetError("Serveur de parties injoignable : reseau local seulement");
            break;
        case OL_HOSTING:
            statusT -= dt;
            ackT += dt;
            // Changement (joueur arrive, nouvelle vague) : envoye vite, mais pas plus de 4 fois par seconde
            if (ready && (statusT <= 0 || (statusDirty && statusT <= 0.75f)))
            {
                SendStatus();
                statusT = 1.0f;
                statusDirty = false;
            }
            if (ackT > 6.0f) SetError("Serveur de parties injoignable");
            else error[0] = '\0';
            break;
        case OL_JOINING:
            waitT += dt;
            retryT -= dt;
            if (ready && retryT <= 0) { SendJoin(); retryT = 0.5f; }
            if (waitT > 8.0f)
            {
                state = OL_FAILED;
                SetError(ready ? "Pas de reponse du serveur de parties" : "Serveur de parties injoignable");
            }
            break;
        default: break;
    }
    if (listEvery > 0)
    {
        listT -= dt;
        if (listT > listEvery) listT = listEvery;   // passage a un rythme plus rapide
        if (ready && listT <= 0) { SendList(); listT = listEvery; }
    }
    else listT = 0;          // premiere demande des le retour sur l'ecran
    listAge += dt;
    if (listAge > 12.0f) { gameCount = 0; totalGames = -1; totalPlayers = 0; }   // liste perimee
}

static void PushGone(unsigned short relay)
{
    for (int i = 0; i < goneCount; i++) if (gone[i] == relay) return;
    if (goneCount < (int)(sizeof(gone)/sizeof(gone[0]))) gone[goneCount++] = relay;
}

static void ReadGames(R *r)
{
    int total = GetU16(r), players = GetU16(r), n = GetU8(r);
    int k = 0;
    for (int i = 0; i < n && !r->err; i++)
    {
        SockGame g = { 0 };
        g.code = GetU16(r); g.version = GetU8(r); g.players = GetU8(r); g.maxPlayers = GetU8(r);
        g.wave = GetU16(r);
        int flags = GetU8(r);
        g.platform = GetU8(r);
        GetStr(r, g.name, sizeof(g.name));
        g.playing = flags & ZG_PLAYING;
        if (r->err) break;
        if (state == OL_HOSTING && g.code == code) continue;   // sa propre partie
        if (k < (int)(sizeof(games)/sizeof(games[0]))) games[k++] = g;
    }
    if (r->err) return;
    gameCount = k;
    totalGames = total;
    totalPlayers = players;
    listAge = 0;
}

int OnlineReceive(const unsigned char *data, int len, unsigned char *out, int cap, unsigned short *relay)
{
    R r = { data, len, 0, false };
    if (GetU32(&r) != ZCB_MAGIC) return 0;
    int type = GetU8(&r);
    if (r.err) return 0;
    seenT = 0;
    switch (type)
    {
        case ZM_GAMES: ReadGames(&r); break;
        case ZM_HOSTED:
        {
            uint32_t k = GetU32(&r);
            int c = GetU16(&r);
            uint32_t t = GetU32(&r);
            if (r.err || k != key || (state != OL_HOST_WAIT && state != OL_HOSTING)) break;
            if (state == OL_HOST_WAIT) statusT = 0;   // premier STATUS tout de suite
            state = OL_HOSTING;
            code = c;
            token = t;
            ackT = 0;
            error[0] = '\0';
            break;
        }
        case ZM_JOINED:
        {
            uint32_t k = GetU32(&r);
            int c = GetU16(&r);
            GetU16(&r);                 // notre numero dans la partie (inutile ici)
            uint32_t t = GetU32(&r);
            if (r.err || k != key || state != OL_JOINING) break;
            state = OL_JOINED;
            code = c;
            token = t;
            break;
        }
        case ZM_ERROR:
        {
            uint32_t k = GetU32(&r);
            int e = GetU8(&r);
            char text[96];
            GetStr(&r, text, sizeof(text));
            if (r.err) break;
            if (state == OL_JOINING && k == key) { state = OL_FAILED; SetError(text); }
            else if (state == OL_HOST_WAIT && k == key) { state = OL_HOST_REFUSED; SetError(text); }
            else if (e == ZE_UNKNOWN && state == OL_HOSTING)
            {
                // Le serveur nous a oublies (redemarrage...) : nouvelle declaration
                state = OL_HOST_WAIT;
                key = NewKey();
                retryT = waitT = 0;
            }
            else if (e == ZE_UNKNOWN && state == OL_JOINED) { state = OL_FAILED; SetError("Connexion au serveur de parties perdue"); }
            break;
        }
        case ZM_GONE:
        {
            int id = GetU16(&r);
            if (r.err) break;
            if (state == OL_HOSTING && id != 0) PushGone((unsigned short)(id + 1));
            else if (state == OL_JOINED && id == 0) PushGone(1);
            break;
        }
        case ZM_DATA:
        {
            int id = GetU16(&r), flags = GetU8(&r);
            int n = len - r.pos;
            if (r.err || n <= 0 || n > cap || (flags & 1)) break;   // bit 0 (compression) : pas encore utilise
            if (state != OL_HOSTING && state != OL_JOINED) break;
            memcpy(out, data + r.pos, n);
            *relay = (unsigned short)(id + 1);
            return n;
        }
        default: break;
    }
    return 0;
}

// Paquet du jeu : l'hote choisit son destinataire (relay - 1 = numero dans la partie),
// un client ecrit toujours a l'hote
bool OnlineSendTo(unsigned short relay, const void *data, int len)
{
    if (state != OL_HOSTING && state != OL_JOINED) return false;
    W w = Begin(ZM_DATA);
    PutU32(&w, token);
    PutU16(&w, state == OL_HOSTING ? relay - 1 : 0);
    PutU8(&w, 0);
    Put(&w, data, len);
    return Send(&w);
}

bool OnlineSendAll(const void *data, int len)
{
    return state == OL_HOSTING && OnlineSendTo(1, data, len);   // destinataire 0 = tous
}

bool OnlinePopGone(unsigned short *relay)
{
    if (goneCount == 0) return false;
    *relay = gone[0];
    memmove(gone, gone + 1, (--goneCount)*sizeof(gone[0]));
    return true;
}

OnlineState OnlineGetState(void) { return state; }
int OnlineCode(void) { return (state == OL_HOSTING || state == OL_JOINED || state == OL_JOINING) ? code : 0; }
const char *OnlineError(void) { return error; }
bool OnlineServerSeen(void) { return seenT < 8.0f; }

void OnlineStatus(int players, int maxP, int wave, bool playing)
{
    if (players != stPlayers || maxP != stMax || wave != stWave || playing != stPlaying) statusDirty = true;
    stPlayers = players; stMax = maxP; stWave = wave; stPlaying = playing;
}

void OnlineSetListed(bool on)
{
    if (on != listed) statusDirty = true;
    listed = on;
}
bool OnlineListed(void) { return listed; }

int OnlineGames(SockGame *out, int max)
{
    int n = gameCount < max ? gameCount : max;
    memcpy(out, games, n*sizeof(SockGame));
    return n;
}

int OnlineTotals(int *players)
{
    if (players) *players = totalPlayers;
    return totalGames;
}
