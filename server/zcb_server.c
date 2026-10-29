/*
 * zcb_server.c - Serveur de parties de Zeffalie Come Back.
 *
 * Un seul programme, sans dependance (Linux, macOS, BSD), qui :
 *  - tient la LISTE des parties ouvertes dans le monde entier (chaque hote
 *    s'y declare, tout le monde la consulte) ;
 *  - sert de RELAIS entre l'hote et les joueurs qui rejoignent une partie par
 *    son code : rien a ouvrir sur la box de l'hote, et les navigateurs (qui ne
 *    savent pas faire d'UDP) jouent avec les PC et les telephones ;
 *  - sert un petit SITE WEB : la page publique des parties (/parties), la
 *    meme liste en JSON pour un autre site (/api/parties) et, si on les lui
 *    donne, le jeu lui-meme (index.html) et le script d'integration (embed.js).
 *
 * Ports (les memes numeros, 28016 par defaut) :
 *  - UDP : versions PC / mobiles du jeu ;
 *  - TCP : HTTP + WebSocket (/ws) pour le navigateur. Pour du HTTPS (obligatoire
 *    si le jeu est integre a un site en https), placer un proxy devant, par
 *    exemple Caddy qui obtient le certificat tout seul (voir server/LISEZMOI.md).
 *
 * Le protocole (messages ZM_xxx) est decrit dans src/online_proto.h.
 *
 * Compilation : cc -O2 -o zcb-server server/zcb_server.c
 * Lancement   : ./zcb-server [--port 28016] [--www dossier]   (--aide pour la liste)
 *
 * Organisation : une seule boucle poll() surveille la socket UDP, la socket
 * d'ecoute TCP et toutes les connexions (HTTP ou WebSocket). Tout est non
 * bloquant ; les messages a envoyer sur une connexion TCP sont mis dans un
 * tampon de sortie vide au rythme ou le client les accepte.
 */
#define _DEFAULT_SOURCE        // glibc : fonctions BSD/POSIX malgre -std=c99
#define _DARWIN_C_SOURCE       // macOS : idem

#include "../src/online_proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

//----------------------------------------------------------------------------------
// Reglages
//----------------------------------------------------------------------------------
#define MAX_CONNS      900     // connexions TCP simultanees (HTTP + WebSocket)
#define MAX_PEERS      4096    // joueurs inscrits (hotes + clients), indice sur 12 bits dans le jeton
#define MAX_GAMES      1000    // taille du tableau des parties (limite reelle : --max-parties)
#define MAX_MEMBERS    64      // clients relayes par partie (le jeu en accepte 49)
#define PEER_TIMEOUT   20.0    // secondes sans message avant d'oublier un hote ou un client
#define WS_PING        25.0    // ping WebSocket (garde la connexion ouverte a travers les proxys)
#define WS_TIMEOUT     70.0    // connexion WebSocket muette (meme pas de pong) : fermee
#define HTTP_TIMEOUT   15.0    // requete HTTP pas recue en entier : fermee
#define HTTP_HEAD_MAX  8192    // taille max des en-tetes d'une requete HTTP
#define WS_OUT_MAX     (512*1024)   // client WebSocket trop lent : on jette les paquets du jeu
#define FILE_MAX       (64*1024*1024)
#define DATA_RATE      600     // messages ZM_DATA par seconde et par joueur, au-dela : ignores
#define CTRL_RATE      20      // demandes (liste, creation...) par seconde et par adresse IP
#define WS_PER_IP      24      // connexions WebSocket par adresse IP

static int optPort = ZCB_SERVER_PORT;
static const char *optWww = "www";
static int optMaxGames = 500;
static int optGamesPerIp = 10;
static bool optTrustProxy = false;   // X-Forwarded-For accepte meme hors 127.0.0.1

//----------------------------------------------------------------------------------
// Outils
//----------------------------------------------------------------------------------
// Temps monotone en secondes (ne recule jamais, meme si l'heure du systeme change)
static double Now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec*1e-9;
}

// Journal : date et heure locales puis le message
static void Log(const char *fmt, ...)
{
    char when[32];
    time_t t = time(NULL);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&t));
    printf("[%s] ", when);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

// Generateur pseudo-aleatoire xorshift64*, initialise avec /dev/urandom
static uint64_t rngState = 88172645463325252ull;
static uint32_t Rand32(void)
{
    rngState ^= rngState >> 12;
    rngState ^= rngState << 25;
    rngState ^= rngState >> 27;
    return (uint32_t)((rngState*2685821657736338717ull) >> 32);
}
static void SeedRandom(void)
{
    uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(&seed, sizeof(seed), 1, f) != 1) seed ^= 0x9E3779B97F4A7C15ull; fclose(f); }
    rngState ^= seed ? seed : 1;
    for (int i = 0; i < 8; i++) Rand32();
}

// Ecriture d'un message : W = tampon + taille ; err passe a vrai s'il deborde
typedef struct { uint8_t *p; int n, cap; bool err; } W;
static void WPut(W *w, const void *d, int n)
{
    if (w->n + n > w->cap) { w->err = true; return; }
    memcpy(w->p + w->n, d, n);
    w->n += n;
}
static void WU8(W *w, int v) { uint8_t x = (uint8_t)v; WPut(w, &x, 1); }
static void WU16(W *w, int v) { uint8_t x[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; WPut(w, x, 2); }
static void WU32(W *w, uint32_t v) { uint8_t x[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; WPut(w, x, 4); }
static void WStr(W *w, const char *s)
{
    int n = (int)strlen(s);
    if (n > 255) n = 255;
    WU8(w, n);
    WPut(w, s, n);
}
static void WText(W *w, const char *s) { WPut(w, s, (int)strlen(s)); }
static void WFmt(W *w, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) WPut(w, tmp, n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1);
}
// Debut d'un message du protocole : magic + type
static void WHead(W *w, int type) { WU32(w, ZCB_MAGIC); WU8(w, type); }

// Lecture d'un message : R = donnees + curseur ; err passe a vrai s'il est trop court
typedef struct { const uint8_t *p; int n, pos; bool err; } R;
static int RU8(R *r) { if (r->pos + 1 > r->n) { r->err = true; return 0; } return r->p[r->pos++]; }
static int RU16(R *r) { int a = RU8(r), b = RU8(r); return a | (b << 8); }
static uint32_t RU32(R *r) { uint32_t a = (uint32_t)RU16(r), b = (uint32_t)RU16(r); return a | (b << 16); }
// Chaine : ne garde que l'ASCII imprimable (la police du jeu n'a rien d'autre), sans '|'
static void RStr(R *r, char *out, int cap)
{
    int n = RU8(r), k = 0;
    for (int i = 0; i < n && !r->err; i++)
    {
        int c = RU8(r);
        if (c >= 32 && c < 127 && c != '|' && k < cap - 1) out[k++] = (char)c;
    }
    while (k > 0 && out[k - 1] == ' ') k--;     // espaces de fin
    out[k] = '\0';
}

//----------------------------------------------------------------------------------
// SHA-1 et base64 : seulement pour la poignee de main WebSocket (RFC 6455)
//----------------------------------------------------------------------------------
static uint32_t Rol(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }

static void Sha1Block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i + 1] << 16 | (uint32_t)p[4*i + 2] << 8 | p[4*i + 3];
    for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = Rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = Rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void Sha1(const void *data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    const uint8_t *p = data;
    size_t full = len/64, rem = len % 64;
    for (size_t i = 0; i < full; i++) Sha1Block(h, p + 64*i);
    // Bourrage : 0x80, des zeros, puis la longueur en bits sur 8 octets
    uint8_t tail[128] = { 0 };
    memcpy(tail, p + 64*full, rem);
    tail[rem] = 0x80;
    size_t tl = rem + 9 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)len*8;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8*i));
    Sha1Block(h, tail);
    if (tl == 128) Sha1Block(h, tail + 64);
    for (int i = 0; i < 5; i++)
    {
        out[4*i] = (uint8_t)(h[i] >> 24); out[4*i + 1] = (uint8_t)(h[i] >> 16);
        out[4*i + 2] = (uint8_t)(h[i] >> 8); out[4*i + 3] = (uint8_t)h[i];
    }
}

static void Base64(const uint8_t *in, int n, char *out)
{
    static const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int k = 0;
    for (int i = 0; i < n; i += 3)
    {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[k++] = abc[(v >> 18) & 63];
        out[k++] = abc[(v >> 12) & 63];
        out[k++] = i + 1 < n ? abc[(v >> 6) & 63] : '=';
        out[k++] = i + 2 < n ? abc[v & 63] : '=';
    }
    out[k] = '\0';
}

//----------------------------------------------------------------------------------
// Donnees
//----------------------------------------------------------------------------------
// Connexion TCP : requete HTTP, puis eventuellement WebSocket
typedef struct {
    int fd;                    // -1 = emplacement libre
    bool ws;                   // passee en WebSocket
    bool closing;              // fermer une fois le tampon de sortie vide
    bool dead;                 // a fermer a la fin du tour de boucle
    char ip[64];               // adresse du client (celle donnee par le proxy le cas echeant)
    bool fromLoopback;         // connexion venue de la machine elle-meme (proxy local)
    uint8_t *in; int inLen;    // donnees recues pas encore traitees
    uint8_t *out; int outLen, outPos, outCap;   // tampon de sortie
    double opened, lastRecv, lastPing;
    int peer;                  // joueur associe (-1 = aucun)
    double rateT; int rateN;   // limitation des demandes
} Conn;

// Joueur inscrit dans une partie (hote ou client), joint en UDP ou en WebSocket
typedef struct {
    bool used;
    int game;                  // indice dans games[]
    int id;                    // numero dans la partie : 0 = hote, 1, 2... = clients
    uint32_t key, token;       // cle de la demande (repetitions), jeton secret
    int conn;                  // WebSocket : indice dans conns[] ; UDP : -1
    struct sockaddr_in udp;    // UDP : adresse du joueur
    char ip[64];
    double lastSeen;
    double rateT; int rateN;   // limitation des paquets relayes
} Peer;

// Partie
typedef struct {
    bool used;
    int code;                  // 1000..9999
    int host;                  // indice de l'hote dans peers[]
    int members[MAX_MEMBERS], memberCount;   // clients relayes (indices dans peers[])
    int nextId;                // prochain numero de client
    char name[ZCB_NAME_MAX + 1];
    int version, players, maxPlayers, wave, flags, platform;
    int bestPlayers;           // record de joueurs (journal)
    double created;
} Game;

// Origine d'un message recu : une adresse UDP ou une connexion WebSocket
typedef struct {
    bool udp;
    struct sockaddr_in addr;
    int conn;
    const char *ip;
} Src;

static int udpFd = -1, listenFd = -1;
static Conn conns[MAX_CONNS];
static Peer peers[MAX_PEERS];
static Game games[MAX_GAMES];
static int gameOfCode[10000];  // code -> indice de partie, -1 si libre
static int gameCount;
static uint8_t scratch[ZCB_MSG_MAX + 64];   // construction des messages sortants

//----------------------------------------------------------------------------------
// Limitation de debit des demandes UDP (par adresse IP, fenetres d'une seconde)
//----------------------------------------------------------------------------------
static struct { uint32_t ip; double t; int n; } rateSlots[4096];

static bool RateOkIp(uint32_t ip, int max)
{
    uint32_t h = (ip*2654435761u) >> 20;          // 12 bits
    double now = Now();
    if (rateSlots[h].ip != ip || now - rateSlots[h].t >= 1.0) { rateSlots[h].ip = ip; rateSlots[h].t = now; rateSlots[h].n = 0; }
    return ++rateSlots[h].n <= max;
}
static bool RateOkWindow(double *t, int *n, int max)
{
    double now = Now();
    if (now - *t >= 1.0) { *t = now; *n = 0; }
    return ++*n <= max;
}

//----------------------------------------------------------------------------------
// Envoi
//----------------------------------------------------------------------------------
static void FlushConn(int c);

// Ajoute des octets au tampon de sortie d'une connexion (agrandi si besoin)
static bool QueueOut(int c, const void *data, int n)
{
    Conn *k = &conns[c];
    if (k->fd < 0 || k->dead) return false;
    if (k->outPos > 0 && k->outPos == k->outLen) k->outPos = k->outLen = 0;
    if (k->outLen + n > k->outCap)
    {
        // On recupere d'abord la place deja envoyee, puis on agrandit
        if (k->outPos > 0)
        {
            memmove(k->out, k->out + k->outPos, k->outLen - k->outPos);
            k->outLen -= k->outPos;
            k->outPos = 0;
        }
        if (k->outLen + n > k->outCap)
        {
            int cap = k->outCap ? k->outCap : 4096;
            while (cap < k->outLen + n) cap *= 2;
            uint8_t *p = realloc(k->out, cap);
            if (!p) { k->dead = true; return false; }
            k->out = p;
            k->outCap = cap;
        }
    }
    memcpy(k->out + k->outLen, data, n);
    k->outLen += n;
    return true;
}

// Message WebSocket binaire (trame unique, non masquee cote serveur)
static void WsSend(int c, int opcode, const uint8_t *data, int n, bool droppable)
{
    Conn *k = &conns[c];
    if (k->fd < 0 || k->dead || !k->ws) return;
    // Client trop lent : les paquets du jeu sont perdus (comme en UDP) plutot qu'accumules
    if (droppable && k->outLen - k->outPos > WS_OUT_MAX) return;
    uint8_t head[10];
    int h = 0;
    head[h++] = (uint8_t)(0x80 | opcode);
    if (n < 126) head[h++] = (uint8_t)n;
    else if (n < 65536) { head[h++] = 126; head[h++] = (uint8_t)(n >> 8); head[h++] = (uint8_t)n; }
    else { head[h++] = 127; for (int i = 7; i >= 0; i--) head[h++] = (uint8_t)((uint64_t)n >> (8*i)); }
    if (QueueOut(c, head, h) && QueueOut(c, data, n)) FlushConn(c);
}

// Envoie un message du protocole vers une origine (UDP ou WebSocket)
static void SendSrc(const Src *s, const uint8_t *data, int n, bool droppable)
{
    if (s->udp) sendto(udpFd, data, n, 0, (const struct sockaddr *)&s->addr, sizeof(s->addr));
    else WsSend(s->conn, 2, data, n, droppable);
}

static Src PeerSrc(int p)
{
    Src s = { peers[p].conn < 0, peers[p].udp, peers[p].conn, peers[p].ip };
    return s;
}

static void SendPeer(int p, const uint8_t *data, int n, bool droppable)
{
    Src s = PeerSrc(p);
    SendSrc(&s, data, n, droppable);
}

static void SendError(const Src *s, uint32_t key, int code, const char *msg)
{
    uint8_t buf[300];
    W w = { buf, 0, sizeof(buf), false };
    WHead(&w, ZM_ERROR); WU32(&w, key); WU8(&w, code); WStr(&w, msg);
    SendSrc(s, buf, w.n, false);
}

static void SendGone(int p, int who)
{
    uint8_t buf[16];
    W w = { buf, 0, sizeof(buf), false };
    WHead(&w, ZM_GONE); WU16(&w, who);
    SendPeer(p, buf, w.n, false);
}

//----------------------------------------------------------------------------------
// Parties et joueurs
//----------------------------------------------------------------------------------
static const char *PlatformName(int p)
{
    static const char *names[] = { "?", "PC Windows", "Mac", "PC Linux", "Android", "iPhone / iPad", "Navigateur" };
    return (p >= 0 && p <= ZP_WEB) ? names[p] : names[0];
}

static int NewPeer(void)
{
    for (int i = 0; i < MAX_PEERS; i++)
        if (!peers[i].used)
        {
            memset(&peers[i], 0, sizeof(peers[i]));
            peers[i].used = true;
            peers[i].conn = -1;
            // Jeton : 20 bits aleatoires + l'indice sur 12 bits (retrouve sans recherche)
            peers[i].token = (Rand32() & 0xFFFFF000u) | (uint32_t)i;
            if (peers[i].token == 0) peers[i].token = 0x1000u | (uint32_t)i;
            peers[i].lastSeen = Now();
            return i;
        }
    return -1;
}

static bool SameUdp(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

// Joueur deja inscrit depuis cette origine (meme adresse UDP ou meme connexion), -1 sinon
static int PeerOfSrc(const Src *s)
{
    if (!s->udp) return conns[s->conn].peer;
    for (int i = 0; i < MAX_PEERS; i++)
        if (peers[i].used && peers[i].conn < 0 && SameUdp(&peers[i].udp, &s->addr)) return i;
    return -1;
}

// Joueur designe par un jeton, a condition que le message vienne bien de lui
static int PeerOfToken(uint32_t token, const Src *s)
{
    int i = (int)(token & 0xFFFu);
    Peer *p = &peers[i];
    if (!p->used || p->token != token) return -1;
    if (s->udp ? (p->conn >= 0 || !SameUdp(&p->udp, &s->addr)) : (p->conn != s->conn)) return -1;
    return i;
}

static void FreePeer(int p)
{
    if (peers[p].conn >= 0 && conns[peers[p].conn].peer == p) conns[peers[p].conn].peer = -1;
    peers[p].used = false;
}

// Termine une partie : les clients sont prevenus (ZM_GONE 0) puis oublies
static void EndGame(int g, const char *why)
{
    Game *G = &games[g];
    for (int i = 0; i < G->memberCount; i++)
    {
        SendGone(G->members[i], 0);
        FreePeer(G->members[i]);
    }
    if (G->host >= 0) FreePeer(G->host);
    Log("Partie %d terminee (%s) : \"%s\", %s, jusqu'a %d joueur(s), vague %d, %.0f min",
        G->code, why, G->name, PlatformName(G->platform), G->bestPlayers, G->wave, (Now() - G->created)/60.0);
    gameOfCode[G->code] = -1;
    G->used = false;
    gameCount--;
}

// Retire un joueur : l'hote emporte sa partie, un client est signale a l'hote
static void RemovePeer(int p, const char *why)
{
    if (!peers[p].used) return;
    int g = peers[p].game;
    Game *G = &games[g];
    if (!G->used) { FreePeer(p); return; }
    if (peers[p].id == 0) { EndGame(g, why); return; }
    for (int i = 0; i < G->memberCount; i++)
        if (G->members[i] == p) { G->members[i] = G->members[--G->memberCount]; break; }
    if (G->host >= 0) SendGone(G->host, peers[p].id);
    FreePeer(p);
}

// Nombre de parties hebergees depuis une adresse IP
static int GamesFromIp(const char *ip)
{
    int n = 0;
    for (int g = 0; g < MAX_GAMES; g++)
        if (games[g].used && games[g].host >= 0 && strcmp(peers[games[g].host].ip, ip) == 0) n++;
    return n;
}

static int NewCode(void)
{
    for (int tries = 0; tries < 64; tries++)
    {
        int code = 1000 + (int)(Rand32() % 9000);
        if (gameOfCode[code] < 0) return code;
    }
    for (int code = 1000; code < 10000; code++) if (gameOfCode[code] < 0) return code;
    return -1;
}

//----------------------------------------------------------------------------------
// Messages du protocole
//----------------------------------------------------------------------------------
// Liste des parties visibles, les plus peuplees d'abord, dans la limite de ZCB_LIST_MAX octets
static void SendGames(const Src *s)
{
    int order[MAX_GAMES], n = 0, total = 0, players = 0;
    for (int g = 0; g < MAX_GAMES; g++)
    {
        if (!games[g].used) continue;
        players += games[g].players;
        if (!(games[g].flags & ZG_LISTED)) continue;
        total++;
        order[n++] = g;
    }
    // Tri par insertion : plus de joueurs d'abord, puis les plus anciennes
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--)
        {
            Game *a = &games[order[j - 1]], *b = &games[order[j]];
            if (b->players > a->players || (b->players == a->players && b->created < a->created))
            { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
            else break;
        }
    uint8_t buf[ZCB_LIST_MAX];
    W w = { buf, 0, sizeof(buf), false };
    WHead(&w, ZM_GAMES); WU16(&w, total); WU16(&w, players);
    int countPos = w.n, count = 0;
    WU8(&w, 0);
    for (int i = 0; i < n && count < 255; i++)
    {
        Game *G = &games[order[i]];
        int len = (int)strlen(G->name);
        if (w.n + 11 + len > w.cap) break;
        WU16(&w, G->code); WU8(&w, G->version); WU8(&w, G->players); WU8(&w, G->maxPlayers);
        WU16(&w, G->wave); WU8(&w, G->flags); WU8(&w, G->platform); WStr(&w, G->name);
        count++;
    }
    buf[countPos] = (uint8_t)count;
    SendSrc(s, buf, w.n, false);
}

static void SendHosted(const Src *s, int p)
{
    uint8_t buf[32];
    W w = { buf, 0, sizeof(buf), false };
    WHead(&w, ZM_HOSTED); WU32(&w, peers[p].key); WU16(&w, games[peers[p].game].code); WU32(&w, peers[p].token);
    SendSrc(s, buf, w.n, false);
}

static void SendJoined(const Src *s, int p)
{
    uint8_t buf[32];
    W w = { buf, 0, sizeof(buf), false };
    WHead(&w, ZM_JOINED); WU32(&w, peers[p].key); WU16(&w, games[peers[p].game].code);
    WU16(&w, peers[p].id); WU32(&w, peers[p].token);
    SendSrc(s, buf, w.n, false);
}

// ZM_HOST : creation d'une partie
static void OnHost(const Src *s, R *r)
{
    int proto = RU8(r);
    uint32_t key = RU32(r);
    int version = RU8(r), maxPlayers = RU8(r), platform = RU8(r), flags = RU8(r);
    char name[ZCB_NAME_MAX + 1];
    RStr(r, name, sizeof(name));
    if (r->err) return;
    if (proto != ZCB_PROTO) { SendError(s, key, ZE_PROTO, "Version du serveur differente : mettez le jeu a jour"); return; }

    int p = PeerOfSrc(s);
    if (p >= 0)
    {
        // Demande repetee (la reponse s'est perdue) : meme reponse
        if (peers[p].id == 0 && peers[p].key == key) { peers[p].lastSeen = Now(); SendHosted(s, p); return; }
        RemovePeer(p, "remplacee");     // l'hote recommence une nouvelle partie
    }
    if (gameCount >= optMaxGames || GamesFromIp(s->ip) >= optGamesPerIp)
    {
        SendError(s, key, ZE_LIMIT, "Trop de parties sur le serveur pour l'instant");
        return;
    }
    int g = -1;
    for (int i = 0; i < MAX_GAMES && g < 0; i++) if (!games[i].used) g = i;
    int code = NewCode();
    p = NewPeer();
    if (g < 0 || code < 0 || p < 0)
    {
        if (p >= 0) FreePeer(p);
        SendError(s, key, ZE_LIMIT, "Serveur plein");
        return;
    }
    Game *G = &games[g];
    memset(G, 0, sizeof(*G));
    G->used = true;
    G->code = code;
    G->host = p;
    G->nextId = 1;
    G->version = version;
    G->maxPlayers = maxPlayers;
    G->players = G->bestPlayers = 1;
    G->platform = platform;
    G->flags = flags & ZG_LISTED;
    G->created = Now();
    if (name[0]) snprintf(G->name, sizeof(G->name), "%s", name);
    else snprintf(G->name, sizeof(G->name), "Partie %d", code);
    gameOfCode[code] = g;
    gameCount++;

    Peer *P = &peers[p];
    P->game = g;
    P->id = 0;
    P->key = key;
    P->conn = s->udp ? -1 : s->conn;
    P->udp = s->addr;
    snprintf(P->ip, sizeof(P->ip), "%s", s->ip);
    if (!s->udp) conns[s->conn].peer = p;
    Log("Partie %d creee : \"%s\" (%s, %s, %s)", code, G->name, PlatformName(platform),
        s->udp ? "UDP" : "WebSocket", (flags & ZG_LISTED) ? "publique" : "privee");
    SendHosted(s, p);
}

// ZM_STATUS : nouvelles de l'hote (joueurs, vague...), toutes les secondes
static void OnStatus(const Src *s, R *r)
{
    uint32_t token = RU32(r);
    int players = RU8(r), maxPlayers = RU8(r), wave = RU16(r), flags = RU8(r);
    if (r->err) return;
    int p = PeerOfToken(token, s);
    if (p < 0 || peers[p].id != 0) { SendError(s, 0, ZE_UNKNOWN, "Partie inconnue du serveur"); return; }
    Game *G = &games[peers[p].game];
    peers[p].lastSeen = Now();
    G->players = players;
    G->maxPlayers = maxPlayers;
    G->wave = wave;
    G->flags = flags & (ZG_PLAYING | ZG_LISTED);
    if (players > G->bestPlayers) G->bestPlayers = players;
    SendHosted(s, p);   // accuse de reception
}

// ZM_JOIN : un joueur rejoint une partie par son code
static void OnJoin(const Src *s, R *r)
{
    int proto = RU8(r);
    uint32_t key = RU32(r);
    int code = RU16(r);
    if (r->err) return;
    if (proto != ZCB_PROTO) { SendError(s, key, ZE_PROTO, "Version du serveur differente : mettez le jeu a jour"); return; }
    int p = PeerOfSrc(s);
    if (p >= 0)
    {
        if (peers[p].id != 0 && peers[p].key == key) { peers[p].lastSeen = Now(); SendJoined(s, p); return; }
        RemovePeer(p, "l'hote est parti rejoindre une autre partie");
    }
    int g = (code >= 1000 && code <= 9999) ? gameOfCode[code] : -1;
    if (g < 0) { SendError(s, key, ZE_NOGAME, "Aucune partie avec ce code (terminee ?)"); return; }
    Game *G = &games[g];
    if (G->memberCount >= MAX_MEMBERS || G->nextId > 0xFFFF) { SendError(s, key, ZE_FULL, "La partie est pleine"); return; }
    p = NewPeer();
    if (p < 0) { SendError(s, key, ZE_LIMIT, "Serveur plein"); return; }
    Peer *P = &peers[p];
    P->game = g;
    P->id = G->nextId++;
    P->key = key;
    P->conn = s->udp ? -1 : s->conn;
    P->udp = s->addr;
    snprintf(P->ip, sizeof(P->ip), "%s", s->ip);
    if (!s->udp) conns[s->conn].peer = p;
    G->members[G->memberCount++] = p;
    SendJoined(s, p);
}

// ZM_DATA : paquet du jeu a faire suivre
static void OnData(const Src *s, R *r)
{
    uint32_t token = RU32(r);
    int dest = RU16(r), flags = RU8(r);
    if (r->err) return;
    int p = PeerOfToken(token, s);
    if (p < 0) return;
    Peer *P = &peers[p];
    P->lastSeen = Now();
    if (!RateOkWindow(&P->rateT, &P->rateN, DATA_RATE)) return;
    Game *G = &games[P->game];
    const uint8_t *payload = r->p + r->pos;
    int len = r->n - r->pos;

    W w = { scratch, 0, sizeof(scratch), false };
    WHead(&w, ZM_DATA); WU16(&w, P->id); WU8(&w, flags); WPut(&w, payload, len);
    if (w.err) return;
    if (P->id != 0) { if (G->host >= 0) SendPeer(G->host, scratch, w.n, true); return; }
    // Venant de l'hote : a un client, ou a tous (dest = 0)
    for (int i = 0; i < G->memberCount; i++)
        if (dest == 0 || peers[G->members[i]].id == dest) SendPeer(G->members[i], scratch, w.n, true);
}

static void OnLeave(const Src *s, R *r)
{
    uint32_t token = RU32(r);
    if (r->err) return;
    int p = PeerOfToken(token, s);
    if (p >= 0) RemovePeer(p, "fermee par l'hote");
}

// Aiguillage d'un message recu (UDP ou WebSocket)
static void OnMessage(const Src *s, const uint8_t *data, int n)
{
    R r = { data, n, 0, false };
    if (RU32(&r) != ZCB_MAGIC) return;
    int type = RU8(&r);
    if (r.err) return;
    if (type == ZM_DATA) { OnData(s, &r); return; }   // le plus frequent : limite par joueur
    // Demandes : limitees par adresse IP (UDP) ou par connexion (WebSocket)
    bool ok = s->udp ? RateOkIp(s->addr.sin_addr.s_addr, CTRL_RATE)
                     : RateOkWindow(&conns[s->conn].rateT, &conns[s->conn].rateN, CTRL_RATE);
    if (!ok) return;
    switch (type)
    {
        case ZM_LIST: if (n >= ZCB_LIST_MIN) SendGames(s); break;
        case ZM_HOST: OnHost(s, &r); break;
        case ZM_STATUS: OnStatus(s, &r); break;
        case ZM_JOIN: OnJoin(s, &r); break;
        case ZM_LEAVE: OnLeave(s, &r); break;
        default: break;
    }
}

//----------------------------------------------------------------------------------
// Pages web
//----------------------------------------------------------------------------------
// Chaine JSON : guillemets et barres echappes, < > & en \u00xx (sans danger dans une page html)
static void WJsonStr(W *w, const char *s)
{
    WU8(w, '"');
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { WU8(w, '\\'); WU8(w, c); }
        else if (c < 32 || c == '<' || c == '>' || c == '&') WFmt(w, "\\u%04x", c);
        else WU8(w, c);
    }
    WU8(w, '"');
}

static bool FileExists(const char *name)
{
    char path[1024];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", optWww, name);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Liste des parties visibles en JSON (pour /api/parties et la page /parties)
static void WGamesJson(W *w)
{
    int total = 0, players = 0;
    double now = Now();
    WText(w, "{\"parties\":[");
    bool first = true;
    for (int g = 0; g < MAX_GAMES; g++)
    {
        Game *G = &games[g];
        if (!G->used) continue;
        players += G->players;
        if (!(G->flags & ZG_LISTED)) continue;
        total++;
        if (!first) WU8(w, ',');
        first = false;
        WFmt(w, "{\"code\":%d,\"nom\":", G->code);
        WJsonStr(w, G->name);
        WFmt(w, ",\"joueurs\":%d,\"max\":%d,\"vague\":%d,\"en_cours\":%s,\"plateforme\":",
             G->players, G->maxPlayers, G->wave, (G->flags & ZG_PLAYING) ? "true" : "false");
        WJsonStr(w, PlatformName(G->platform));
        WFmt(w, ",\"version\":%d,\"depuis_s\":%d}", G->version, (int)(now - G->created));
    }
    WFmt(w, "],\"total_parties\":%d,\"total_joueurs\":%d,\"jeu_disponible\":%s}",
         total, players, FileExists("index.html") ? "true" : "false");
}

// Page publique des parties. widget = version compacte a integrer dans un autre site (iframe).
// Le contenu est dessine par le script a partir du JSON, puis rafraichi toutes les 4 secondes.
static const char *PAGE_HEAD =
"<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Parties en ligne - Zeffalie Come Back</title><style>"
":root{--bg:#05081a;--card:#0f1530;--line:#26305a;--text:#f2f2f2;--muted:#9aa3c7;--accent:#ffc83c;--live:#5fd38d}"
"*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,sans-serif}"
"main{max-width:860px;margin:0 auto;padding:16px}"
".top{display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap;margin-bottom:14px}"
"h1{color:var(--accent);margin:0;font-size:clamp(22px,5vw,32px);letter-spacing:1px}"
".sub{color:var(--muted);margin:2px 0 0}"
".list{display:grid;gap:8px}"
".game{display:grid;grid-template-columns:1fr auto auto;gap:12px;align-items:center;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 14px}"
".name{font-weight:600;overflow-wrap:anywhere}.meta{color:var(--muted);font-size:14px}"
".count{font-variant-numeric:tabular-nums;white-space:nowrap;text-align:right}.count b{font-size:20px}"
".live{color:var(--live)}"
"a.btn{display:inline-block;background:var(--accent);color:#1a1300;text-decoration:none;font-weight:700;padding:8px 14px;border-radius:8px;white-space:nowrap}"
"a.btn:focus-visible{outline:3px solid #fff;outline-offset:2px}"
".code{font-family:ui-monospace,monospace;color:var(--accent);font-weight:700}"
".empty{color:var(--muted);padding:24px;text-align:center;border:1px dashed var(--line);border-radius:10px}"
"body.widget main{padding:8px}body.widget h1{font-size:20px}body.widget .game{padding:8px 10px}"
"@media (max-width:520px){.game{grid-template-columns:1fr auto}.count{grid-column:1;text-align:left}}"
"</style></head>";

static const char *PAGE_SCRIPT =
"<script>\n"
"var list=document.getElementById('list'),sum=document.getElementById('sum');\n"
"function el(t,c,x){var e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e;}\n"
"function render(d){\n"
"  list.textContent='';\n"
"  var p=d.parties||[];\n"
"  sum.textContent=p.length?(p.length+(p.length>1?' parties ouvertes':' partie ouverte')+' \\u2022 '+d.total_joueurs+(d.total_joueurs>1?' joueurs en ligne':' joueur en ligne')):'Aucune partie en ce moment';\n"
"  if(!p.length){list.appendChild(el('div','empty','Aucune partie ouverte pour l\\u2019instant. Lancez le jeu et cr\\u00e9ez-en une : elle appara\\u00eetra ici pour tout le monde.'));return;}\n"
"  p.forEach(function(g){\n"
"    var row=el('div','game'),info=el('div');\n"
"    info.appendChild(el('div','name',g.nom));\n"
"    var m=el('div','meta');m.appendChild(document.createTextNode('Code '));m.appendChild(el('span','code',String(g.code)));\n"
"    m.appendChild(document.createTextNode(' \\u2022 '+g.plateforme+' \\u2022 '));\n"
"    m.appendChild(g.en_cours?el('span','live','vague '+g.vague):document.createTextNode('salle d\\u2019attente'));\n"
"    info.appendChild(m);row.appendChild(info);\n"
"    var c=el('div','count');c.appendChild(el('b',null,String(g.joueurs)));c.appendChild(document.createTextNode(' / '+g.max));row.appendChild(c);\n"
"    if(d.jeu_disponible&&g.joueurs<g.max){var a=el('a','btn','Rejoindre');a.href='./?rejoindre='+g.code;if(WIDGET)a.target='_blank';row.appendChild(a);}\n"
"    else row.appendChild(el('span'));\n"
"    list.appendChild(row);\n"
"  });\n"
"}\n"
"render(DATA);\n"
"setInterval(function(){fetch('api/parties',{cache:'no-store'}).then(function(r){return r.json();}).then(render).catch(function(){});},4000);\n"
"</script></body></html>";

static void WPartiesPage(W *w, bool widget)
{
    bool game = FileExists("index.html");
    WText(w, PAGE_HEAD);
    WText(w, widget ? "<body class=\"widget\"><main>" : "<body><main>");
    WText(w, "<div class=\"top\"><div><h1>Parties en ligne</h1><p class=\"sub\" id=\"sum\">Chargement\xE2\x80\xA6</p></div>");
    if (game) WFmt(w, "<a class=\"btn\" href=\"./\"%s>Jouer</a>", widget ? " target=\"_blank\"" : "");
    WText(w, "</div><div class=\"list\" id=\"list\"></div>"
             "<noscript><p class=\"empty\">Activez JavaScript pour voir la liste.</p></noscript></main>");
    WFmt(w, "<script>var WIDGET=%s,DATA=", widget ? "true" : "false");
    WGamesJson(w);
    WText(w, ";</script>");
    WText(w, PAGE_SCRIPT);
}

//----------------------------------------------------------------------------------
// HTTP
//----------------------------------------------------------------------------------
static void HttpRespond(int c, int status, const char *reason, const char *type, const char *extra,
                        const void *body, int len, bool head)
{
    char h[1024];
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n%s\r\n", status, reason, type, len, extra ? extra : "");
    QueueOut(c, h, n);
    if (!head && len > 0) QueueOut(c, body, len);
    conns[c].closing = true;
    FlushConn(c);
}

static void HttpText(int c, int status, const char *reason, const char *text)
{
    HttpRespond(c, status, reason, "text/plain; charset=utf-8", NULL, text, (int)strlen(text), false);
}

static const char *MimeType(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".js")) return "text/javascript; charset=utf-8";
    if (!strcmp(dot, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json";
    if (!strcmp(dot, ".png")) return "image/png";
    if (!strcmp(dot, ".ico")) return "image/x-icon";
    if (!strcmp(dot, ".svg")) return "image/svg+xml";
    if (!strcmp(dot, ".wasm")) return "application/wasm";
    if (!strcmp(dot, ".txt") || !strcmp(dot, ".md")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// Fichier du dossier www (noms simples seulement : pas de sous-dossier, pas de "..")
static bool ServeFile(int c, const char *name, bool head)
{
    if (!name[0] || name[0] == '.' || strlen(name) > 128) return false;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '_'))
            return false;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", optWww, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > FILE_MAX) { fclose(f); return false; }
    int len = (int)st.st_size;
    uint8_t *data = malloc(len ? len : 1);
    if (!data || (len && fread(data, 1, len, f) != (size_t)len)) { free(data); fclose(f); return false; }
    fclose(f);
    // Le jeu change a chaque mise a jour : le navigateur redemande au bout de 5 minutes
    HttpRespond(c, 200, "OK", MimeType(name), "Cache-Control: public, max-age=300\r\nAccess-Control-Allow-Origin: *\r\n", data, len, head);
    free(data);
    return true;
}

// Valeur d'un en-tete HTTP (nom insensible a la casse), copiee dans out
static bool HeaderValue(const char *req, const char *name, char *out, int cap)
{
    size_t nl = strlen(name);
    for (const char *line = strstr(req, "\r\n"); line && line[2] && !(line[2] == '\r'); line = strstr(line + 2, "\r\n"))
    {
        const char *l = line + 2;
        if (strncasecmp(l, name, nl) != 0 || l[nl] != ':') continue;
        l += nl + 1;
        while (*l == ' ' || *l == '\t') l++;
        int k = 0;
        while (*l && *l != '\r' && k < cap - 1) out[k++] = *l++;
        while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
        out[k] = '\0';
        return true;
    }
    return false;
}

// Vrai si la liste "a, b, c" (en-tete Connection...) contient le mot (casse ignoree)
static bool HasToken(const char *list, const char *word)
{
    size_t wl = strlen(word);
    for (const char *p = list; *p; )
    {
        while (*p == ' ' || *p == ',') p++;
        const char *e = p;
        while (*e && *e != ',') e++;
        const char *t = e;
        while (t > p && t[-1] == ' ') t--;
        if ((size_t)(t - p) == wl && strncasecmp(p, word, wl) == 0) return true;
        p = e;
    }
    return false;
}

static int WsCountFromIp(const char *ip)
{
    int n = 0;
    for (int i = 0; i < MAX_CONNS; i++) if (conns[i].fd >= 0 && conns[i].ws && strcmp(conns[i].ip, ip) == 0) n++;
    return n;
}

// Requete HTTP complete (en-tetes termines par une ligne vide) : reponse ou passage en WebSocket
static void HandleRequest(int c, char *req, int headLen)
{
    Conn *k = &conns[c];
    char method[16] = "", target[512] = "";
    if (sscanf(req, "%15s %511s", method, target) != 2) { HttpText(c, 400, "Bad Request", "Requete invalide\n"); return; }
    bool head = !strcmp(method, "HEAD");
    if (strcmp(method, "GET") && !head) { HttpText(c, 405, "Method Not Allowed", "Methode non prise en charge\n"); return; }

    // Derriere un proxy (Caddy, nginx...) : la vraie adresse du joueur est la derniere de X-Forwarded-For
    char fwd[256];
    if ((k->fromLoopback || optTrustProxy) && HeaderValue(req, "X-Forwarded-For", fwd, sizeof(fwd)))
    {
        char *last = strrchr(fwd, ',');
        last = last ? last + 1 : fwd;
        while (*last == ' ') last++;
        if (*last) snprintf(k->ip, sizeof(k->ip), "%.63s", last);
    }

    char path[512], *query = strchr(target, '?');
    snprintf(path, sizeof(path), "%s", target);
    if (strchr(path, '?')) *strchr(path, '?') = '\0';
    bool widget = query && strstr(query, "widget=1");

    if (!strcmp(path, "/ws"))
    {
        char upgrade[64] = "", connection[128] = "", key[64] = "", version[16] = "";
        HeaderValue(req, "Upgrade", upgrade, sizeof(upgrade));
        HeaderValue(req, "Connection", connection, sizeof(connection));
        HeaderValue(req, "Sec-WebSocket-Version", version, sizeof(version));
        if (strcasecmp(upgrade, "websocket") || !HasToken(connection, "upgrade") || !HeaderValue(req, "Sec-WebSocket-Key", key, sizeof(key)))
        { HttpText(c, 426, "Upgrade Required", "Adresse reservee au jeu (WebSocket)\n"); return; }
        if (strcmp(version, "13")) { HttpRespond(c, 426, "Upgrade Required", "text/plain", "Sec-WebSocket-Version: 13\r\n", "", 0, false); return; }
        if (WsCountFromIp(k->ip) >= WS_PER_IP) { HttpText(c, 429, "Too Many Requests", "Trop de connexions\n"); return; }
        // Reponse = base64(SHA-1(cle + GUID fixe de la RFC 6455))
        char both[128];
        uint8_t hash[20];
        char accept[32];
        snprintf(both, sizeof(both), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
        Sha1(both, strlen(both), hash);
        Base64(hash, 20, accept);
        char resp[256];
        int n = snprintf(resp, sizeof(resp), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
        k->ws = true;
        k->lastRecv = k->lastPing = Now();
        // Les octets deja recus apres les en-tetes sont le debut des trames WebSocket
        memmove(k->in, k->in + headLen, k->inLen - headLen);
        k->inLen -= headLen;
        QueueOut(c, resp, n);
        FlushConn(c);
        return;
    }

    if (!strcmp(path, "/api/parties"))
    {
        static uint8_t buf[256*1024];
        W w = { buf, 0, sizeof(buf), false };
        WGamesJson(&w);
        HttpRespond(c, 200, "OK", "application/json; charset=utf-8",
                    "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n", buf, w.n, head);
        return;
    }
    if (!strcmp(path, "/parties") || !strcmp(path, "/parties.html") || (!strcmp(path, "/") && !FileExists("index.html")))
    {
        static uint8_t buf[256*1024];
        W w = { buf, 0, sizeof(buf), false };
        WPartiesPage(&w, widget);
        HttpRespond(c, 200, "OK", "text/html; charset=utf-8", "Cache-Control: no-store\r\n", buf, w.n, head);
        return;
    }
    if (ServeFile(c, !strcmp(path, "/") ? "index.html" : path + 1, head)) return;
    HttpText(c, 404, "Not Found", "Page introuvable\n");
}

//----------------------------------------------------------------------------------
// Connexions TCP
//----------------------------------------------------------------------------------
static void FlushConn(int c)
{
    Conn *k = &conns[c];
    while (k->fd >= 0 && !k->dead && k->outPos < k->outLen)
    {
        ssize_t n = send(k->fd, k->out + k->outPos, k->outLen - k->outPos, 0);
        if (n > 0) { k->outPos += (int)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
        k->dead = true;
    }
    if (k->outPos == k->outLen) { k->outPos = k->outLen = 0; if (k->closing) k->dead = true; }
}

static void CloseConn(int c)
{
    Conn *k = &conns[c];
    if (k->fd < 0) return;
    close(k->fd);
    k->fd = -1;
    free(k->in); free(k->out);
    k->in = k->out = NULL;
    if (k->peer >= 0)
    {
        int p = k->peer;
        k->peer = -1;
        if (peers[p].used && peers[p].conn == c) RemovePeer(p, "deconnexion");
    }
}

// Ferme les connexions marquees (la fermeture peut en marquer d'autres : on recommence)
static void ReapConns(void)
{
    for (bool again = true; again; )
    {
        again = false;
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].fd >= 0 && conns[i].dead) { CloseConn(i); again = true; }
    }
}

// Trames WebSocket recues (le navigateur masque toujours les siennes)
static void HandleWsFrames(int c)
{
    Conn *k = &conns[c];
    for (;;)
    {
        if (k->inLen < 2) return;
        uint8_t *b = k->in;
        bool fin = b[0] & 0x80;
        int op = b[0] & 0x0F;
        bool masked = b[1] & 0x80;
        uint64_t len = b[1] & 0x7F;
        int h = 2;
        if (len == 126) { if (k->inLen < 4) return; len = (uint64_t)b[2] << 8 | b[3]; h = 4; }
        else if (len == 127)
        {
            if (k->inLen < 10) return;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | b[2 + i];
            h = 10;
        }
        if (!masked || len > ZCB_MSG_MAX || !fin || op == 0) { k->dead = true; return; }   // hors protocole
        if (k->inLen < h + 4 + (int)len) return;      // trame incomplete
        uint8_t *mask = b + h, *data = b + h + 4;
        for (uint64_t i = 0; i < len; i++) data[i] ^= mask[i & 3];
        k->lastRecv = Now();
        if (op == 2)
        {
            Src s = { false, { 0 }, c, k->ip };
            OnMessage(&s, data, (int)len);
        }
        else if (op == 8) { WsSend(c, 8, data, len >= 2 ? 2 : 0, false); k->closing = true; FlushConn(c); }
        else if (op == 9) WsSend(c, 10, data, (int)len, false);   // ping -> pong
        if (k->dead || k->fd < 0) return;
        int used = h + 4 + (int)len;
        memmove(k->in, k->in + used, k->inLen - used);
        k->inLen -= used;
    }
}

static void ReadConn(int c)
{
    Conn *k = &conns[c];
    int cap = k->ws ? ZCB_MSG_MAX + 16 : HTTP_HEAD_MAX;
    for (;;)
    {
        if (k->dead || k->fd < 0) return;
        if (k->inLen >= cap) { k->dead = true; return; }
        ssize_t n = recv(k->fd, k->in + k->inLen, cap - k->inLen, 0);
        if (n == 0) { k->dead = true; return; }
        if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) k->dead = true; break; }
        k->inLen += (int)n;
        if (k->ws) { HandleWsFrames(c); if (k->dead) return; continue; }
        if (k->closing) { k->inLen = 0; continue; }   // reponse deja partie : on ignore la suite
        // En-tetes complets ?
        k->in[k->inLen < cap ? k->inLen : cap - 1] = '\0';
        char *end = strstr((char *)k->in, "\r\n\r\n");
        if (!end) continue;
        int headLen = (int)(end - (char *)k->in) + 4;
        HandleRequest(c, (char *)k->in, headLen);
        if (k->ws) { HandleWsFrames(c); if (k->dead) return; }
        else k->inLen = 0;
    }
}

static void AcceptConns(void)
{
    for (;;)
    {
        struct sockaddr_storage a;
        socklen_t al = sizeof(a);
        int fd = accept(listenFd, (struct sockaddr *)&a, &al);
        if (fd < 0) return;
        int c = -1;
        for (int i = 0; i < MAX_CONNS && c < 0; i++) if (conns[i].fd < 0) c = i;
        uint8_t *in = c >= 0 ? malloc(ZCB_MSG_MAX + 32) : NULL;
        if (c < 0 || !in) { free(in); close(fd); continue; }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));   // pas d'attente pour les petits paquets
#if defined(SO_NOSIGPIPE)
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        Conn *k = &conns[c];
        memset(k, 0, sizeof(*k));
        k->fd = fd;
        k->in = in;
        k->peer = -1;
        k->opened = k->lastRecv = Now();
        // Adresse texte, sans le prefixe "::ffff:" des IPv4 vues par une socket IPv6
        if (a.ss_family == AF_INET6)
        {
            struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&a;
            inet_ntop(AF_INET6, &s6->sin6_addr, k->ip, sizeof(k->ip));
            if (!strncmp(k->ip, "::ffff:", 7)) memmove(k->ip, k->ip + 7, strlen(k->ip + 7) + 1);
        }
        else inet_ntop(AF_INET, &((struct sockaddr_in *)&a)->sin_addr, k->ip, sizeof(k->ip));
        k->fromLoopback = !strcmp(k->ip, "127.0.0.1") || !strcmp(k->ip, "::1");
    }
}

//----------------------------------------------------------------------------------
// Boucle principale
//----------------------------------------------------------------------------------
static void ReadUdp(void)
{
    static uint8_t buf[65536];
    for (int i = 0; i < 512; i++)
    {
        struct sockaddr_in a;
        socklen_t al = sizeof(a);
        ssize_t n = recvfrom(udpFd, buf, sizeof(buf), 0, (struct sockaddr *)&a, &al);
        if (n <= 0) return;
        if (n > ZCB_MSG_MAX) continue;
        char ip[64];
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
        Src s = { true, a, -1, ip };
        OnMessage(&s, buf, (int)n);
    }
}

// Une fois par seconde : joueurs muets, connexions endormies, pings WebSocket
static void Sweep(void)
{
    double now = Now();
    for (int p = 0; p < MAX_PEERS; p++)
        if (peers[p].used && now - peers[p].lastSeen > PEER_TIMEOUT) RemovePeer(p, "plus de nouvelles");
    for (int c = 0; c < MAX_CONNS; c++)
    {
        Conn *k = &conns[c];
        if (k->fd < 0 || k->dead) continue;
        // HTTP : requete trop lente a arriver, ou telechargement abandonne
        if (!k->ws) { if (now - k->opened > (k->closing ? 600.0 : HTTP_TIMEOUT)) k->dead = true; continue; }
        if (now - k->lastRecv > WS_TIMEOUT) { k->dead = true; continue; }
        if (now - k->lastPing > WS_PING) { k->lastPing = now; WsSend(c, 9, (const uint8_t *)"zcb", 3, false); }
    }
}

static int OpenUdp(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    int size = 4*1024*1024;   // grands tampons : rafales de paquets a plusieurs joueurs
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

// Ecoute TCP en IPv6 + IPv4 si possible, sinon IPv4 seulement
static int OpenTcp(int port)
{
    int one = 1, zero = 0;
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd >= 0)
    {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
        a.sin6_port = htons((unsigned short)port);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
        if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 128) != 0) { close(fd); fd = -1; }
    }
    if (fd < 0)
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((unsigned short)port);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 128) != 0) { close(fd); return -1; }
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static void Usage(void)
{
    printf("Serveur de parties de Zeffalie Come Back\n\n"
           "Usage : zcb-server [options]\n"
           "  --port N            port UDP (jeu PC/mobile) et TCP (HTTP + WebSocket), defaut %d\n"
           "  --www DOSSIER       fichiers servis : index.html (le jeu), embed.js... (defaut : www)\n"
           "  --max-parties N     parties simultanees au plus (defaut %d)\n"
           "  --parties-par-ip N  parties par adresse IP au plus (defaut %d)\n"
           "  --proxy             croire l'en-tete X-Forwarded-For meme si le proxy n'est pas sur\n"
           "                      cette machine (sinon : seulement depuis 127.0.0.1)\n"
           "  --aide              cette aide\n\n"
           "Pages : /parties (liste publique), /parties?widget=1 (a integrer dans un site),\n"
           "        /api/parties (JSON), /ws (jeu dans le navigateur), / (le jeu si www/index.html existe)\n",
           ZCB_SERVER_PORT, optMaxGames, optGamesPerIp);
}

static void Quit(int sig) { (void)sig; Log("Arret du serveur"); exit(0); }

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        bool hasValue = i + 1 < argc;
        if (!strcmp(a, "--port") && hasValue) optPort = atoi(argv[++i]);
        else if (!strcmp(a, "--www") && hasValue) optWww = argv[++i];
        else if (!strcmp(a, "--max-parties") && hasValue) optMaxGames = atoi(argv[++i]);
        else if (!strcmp(a, "--parties-par-ip") && hasValue) optGamesPerIp = atoi(argv[++i]);
        else if (!strcmp(a, "--proxy")) optTrustProxy = true;
        else if (!strcmp(a, "--aide") || !strcmp(a, "--help") || !strcmp(a, "-h")) { Usage(); return 0; }
        else { fprintf(stderr, "Option inconnue : %s (voir --aide)\n", a); return 1; }
    }
    if (optPort <= 0 || optPort > 65535) { fprintf(stderr, "Port invalide\n"); return 1; }
    if (optMaxGames < 1 || optMaxGames > MAX_GAMES) optMaxGames = MAX_GAMES;
    if (optGamesPerIp < 1) optGamesPerIp = 1;

    signal(SIGPIPE, SIG_IGN);      // ecrire sur une connexion fermee ne doit pas arreter le serveur
    signal(SIGINT, Quit);
    signal(SIGTERM, Quit);
    // Assez de descripteurs pour toutes les connexions
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < MAX_CONNS + 64)
    {
        rl.rlim_cur = rl.rlim_max < MAX_CONNS + 64 ? rl.rlim_max : MAX_CONNS + 64;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    SeedRandom();
    for (int i = 0; i < 10000; i++) gameOfCode[i] = -1;
    for (int i = 0; i < MAX_CONNS; i++) conns[i].fd = -1;

    udpFd = OpenUdp(optPort);
    listenFd = OpenTcp(optPort);
    if (udpFd < 0 || listenFd < 0)
    {
        fprintf(stderr, "Impossible d'ouvrir le port %d (%s deja utilise ?)\n", optPort, udpFd < 0 ? "UDP" : "TCP");
        return 1;
    }
    Log("Serveur de parties pret : port %d (UDP + TCP), site : %s%s", optPort, optWww,
        FileExists("index.html") ? " (le jeu est servi a l'adresse /)" : " (pas de index.html : / affiche la liste)");

    static struct pollfd fds[MAX_CONNS + 2];
    static int fdConn[MAX_CONNS + 2];
    double lastSweep = Now();
    for (;;)
    {
        int n = 0;
        fds[n] = (struct pollfd){ udpFd, POLLIN, 0 }; fdConn[n++] = -1;
        fds[n] = (struct pollfd){ listenFd, POLLIN, 0 }; fdConn[n++] = -2;
        for (int i = 0; i < MAX_CONNS; i++)
        {
            if (conns[i].fd < 0) continue;
            short ev = POLLIN;
            if (conns[i].outPos < conns[i].outLen) ev |= POLLOUT;
            fds[n] = (struct pollfd){ conns[i].fd, ev, 0 };
            fdConn[n++] = i;
        }
        int r = poll(fds, n, 250);
        if (r < 0 && errno != EINTR) { perror("poll"); return 1; }
        for (int i = 0; i < n && r > 0; i++)
        {
            if (!fds[i].revents) continue;
            if (fdConn[i] == -1) ReadUdp();
            else if (fdConn[i] == -2) AcceptConns();
            else
            {
                int c = fdConn[i];
                if (conns[c].fd < 0 || conns[c].dead) continue;
                if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) ReadConn(c);
                if (fds[i].revents & POLLOUT) FlushConn(c);
            }
        }
        ReapConns();
        if (Now() - lastSweep >= 1.0)
        {
            lastSweep = Now();
            Sweep();
            ReapConns();
        }
    }
}
