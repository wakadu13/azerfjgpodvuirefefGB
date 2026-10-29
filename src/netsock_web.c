/*
 * netsock_web.c - "Sockets" de la version navigateur.
 *
 * Un navigateur ne peut pas ouvrir de socket UDP. Deux relais remplacent l'UDP,
 * avec le meme comportement (petits messages, pertes possibles), donc net.c
 * fonctionne sans changement :
 *
 * 1. SERVEUR DE PARTIES (partout, sauf sur claude.ai) : une WebSocket vers le
 *    serveur (server/zcb_server.c), geree par le module JavaScript ZWs de
 *    web/shell.html ; le protocole est celui de online.c, comme pour les PC.
 *    On y retrouve les parties du monde entier, y compris celles des PC et des
 *    telephones. Adresse du serveur : parametre ?serveur= de la page, sinon
 *    ONLINE_SERVER (compilation), sinon le site qui sert la page.
 *
 * 2. SALON claude.ai (sur la page claude.ai du jeu) : la capacite "room" relaie
 *    les messages entre les personnes qui ont la page ouverte (module ZNet) :
 *  - une partie = un salon nomme "zcb-<code>", le code a 4 chiffres est tire
 *    au hasard par l'hote et tape par les autres a la place d'une adresse IP ;
 *  - les "adresses" sont des numeros : 1 = l'hote (vu d'un client), 2, 3...
 *    = les clients (vus de l'hote), attribues par ZNet a chaque nouveau joueur ;
 *    0 = tous les clients (diffusion de l'etat du monde) ;
 *  - chaque hote s'annonce dans le salon commun de la page : tout le monde
 *    voit la liste des parties ouvertes (SockListGames) et en rejoint une
 *    d'un clic, sans avoir a taper le code.
 *
 * Sans l'un ni l'autre (fichier ouvert d'un double-clic sans serveur
 * configure...), le jeu cache simplement le menu reseau.
 */
#if defined(BR_WEB)

#include "netsock.h"
#include "online.h"
#include "online_proto.h"

#include <emscripten/emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Fonctions JavaScript appelees depuis le C (voir ZNet dans web/shell.html)
EM_JS(int, znet_available, (void), { return (window.ZNet && ZNet.available()) ? 1 : 0; });
EM_JS(void, znet_open, (int code, int host), { ZNet.open(code, !!host); });
EM_JS(void, znet_close, (void), { if (window.ZNet) ZNet.close(); });
EM_JS(int, znet_send, (int to, const unsigned char *data, int len), {
    return ZNet.send(to, HEAPU8.slice(data, data + len)) ? 1 : 0;
});
EM_JS(int, znet_recv, (unsigned char *buf, int cap, int *from), {
    var p = ZNet.recv();
    if (!p || p.bytes.length > cap) return 0;
    HEAPU8.set(p.bytes, buf);
    HEAP32[from >> 2] = p.from;
    return p.bytes.length;
});

EM_JS(void, znet_announce, (int code, int players, int maxPlayers, int wave, int playing), {
    ZNet.announce({ c: code, n: players, m: maxPlayers, w: wave, p: playing });
});
// Ecrit la liste des parties dans buf, une par ligne : "code|joueurs|max|vague|lancee|nom"
EM_JS(void, znet_games, (char *buf, int cap), { stringToUTF8(ZNet.games(), buf, cap); });

// WebSocket vers le serveur de parties (voir ZWs dans web/shell.html)
#if defined(EM_JS_DEPS)
// Fonctions du runtime d'Emscripten utilisees ci-dessous (sinon elles ne sont incluses
// que si une autre bibliotheque, comme la GLFW de raylib, s'en sert deja)
EM_JS_DEPS(zcb_netsock, "$UTF8ToString,$stringToUTF8");
#endif
EM_JS(void, zws_start, (const char *server), { if (window.ZWs) ZWs.start(UTF8ToString(server)); });
EM_JS(int, zws_configured, (void), { return (window.ZWs && ZWs.configured()) ? 1 : 0; });
EM_JS(int, zws_open, (void), { return (window.ZWs && ZWs.open()) ? 1 : 0; });
EM_JS(int, zws_epoch, (void), { return window.ZWs ? ZWs.epoch() : 0; });
EM_JS(int, zws_send, (const unsigned char *data, int len), { return ZWs.send(HEAPU8.slice(data, data + len)) ? 1 : 0; });
// Message suivant : sa taille, 0 s'il n'y en a pas, -1 s'il est trop gros (ignore)
EM_JS(int, zws_recv, (unsigned char *buf, int cap), {
    var m = ZWs.recv();
    if (!m) return 0;
    if (m.length > cap) return -1;
    HEAPU8.set(m, buf);
    return m.length;
});
EM_JS(void, zws_pseudo, (char *buf, int cap), { stringToUTF8(window.ZWs ? ZWs.pseudo() : '', buf, cap); });
// Parametre ?rejoindre=1234 de la page : partie a rejoindre des le lancement
EM_JS(int, web_join_param, (void), {
    var c = parseInt(new URLSearchParams(location.search).get('rejoindre'), 10);
    return (c >= 1000 && c <= 9999) ? c : 0;
});

enum { BACK_NONE, BACK_ROOM, BACK_SERVER };
static int backend;         // relais de la partie en cours
static int gameCode;        // salon : code de la partie (4 chiffres)
static int pendingCode;     // serveur : code donne a SockResolve, utilise par SockOpen(0)
static bool opened;
static bool wsWasOpen;
static int wsEpoch;
static char linkError[96];
static unsigned char raw[ZCB_MSG_MAX + 64];

static void InitOnline(void)
{
    static bool done;
    if (done) return;
    done = true;
    char pseudo[64];
    zws_pseudo(pseudo, sizeof(pseudo));
    OnlineSetIdentity(ZP_WEB, pseudo);
    zws_start(ONLINE_SERVER);
}

// Sur claude.ai, le salon "room" ; ailleurs, le serveur de parties
static bool UseRoom(void) { return znet_available() != 0; }

bool SockOnlineConfigured(void) { InitOnline(); return zws_configured() != 0; }
bool SockAvailable(void) { InitOnline(); return UseRoom() || zws_open(); }

// Transport du module online.c : un message WebSocket
bool OnlineTransportReady(void) { return zws_open() != 0; }
bool OnlineTransportSend(const unsigned char *data, int len) { return zws_open() && zws_send(data, len) != 0; }

int SockAutoJoinCode(void) { return web_join_param(); }

// port != 0 : hote ; port == 0 : client (code donne par SockResolve)
bool SockOpen(unsigned short port)
{
    InitOnline();
    int code = pendingCode;
    pendingCode = 0;
    SockClose();
    if (UseRoom())
    {
        // Salon claude.ai : l'hote tire le code de la partie
        if (port != 0)
        {
            srand((unsigned)time(NULL));
            gameCode = 1000 + rand() % 9000;
        }
        znet_open(gameCode, port != 0);
        backend = BACK_ROOM;
    }
    else
    {
        // Serveur de parties : il attribue le code a l'hote
        if (!zws_open())
        {
            snprintf(linkError, sizeof(linkError), "Serveur de parties injoignable pour l'instant");
            return false;
        }
        if (port != 0) OnlineHost(50);          // 50 joueurs, corrige ensuite par SockAnnounce
        else if (code > 0) OnlineJoin(code);
        else return false;
        backend = BACK_SERVER;
    }
    opened = true;
    return true;
}

void SockClose(void)
{
    if (opened && backend == BACK_ROOM) znet_close();
    OnlineLeave();
    opened = false;
    backend = BACK_NONE;
}

bool SockSend(NetAddr to, const void *data, int len)
{
    if (!opened) return false;
    if (backend == BACK_SERVER) return OnlineSendTo(to.relay, data, len);
    return znet_send((int)to.ip, (const unsigned char *)data, len) != 0;
}

// Un seul message pour tous les clients : diffuse par le salon (adresse 0) ou par le serveur
bool SockSendMany(const NetAddr *to, int n, const void *data, int len)
{
    (void)to;
    if (!opened || n <= 0) return false;
    if (backend == BACK_SERVER) return OnlineSendAll(data, len);
    return znet_send(0, (const unsigned char *)data, len) != 0;
}

int SockRecv(void *buf, int cap, NetAddr *from)
{
    if (backend == BACK_SERVER || !opened)
    {
        // Messages du serveur de parties : ceux de service sont traites par online.c,
        // les paquets du jeu sont rendus a net.c
        for (;;)
        {
            int n = zws_recv(raw, sizeof(raw));
            if (n == 0) return 0;
            if (n < 0) continue;
            unsigned short relay = 0;
            int m = OnlineReceive(raw, n, (unsigned char *)buf, cap, &relay);
            if (m > 0 && opened) { *from = (NetAddr){ 0, 0, relay }; return m; }
        }
    }
    int idx = 0;
    int n = znet_recv((unsigned char *)buf, cap, &idx);
    if (n > 0) *from = (NetAddr){ (unsigned int)idx, 0, 0 };
    return n;
}

// "Adresse" tapee par le joueur = code de la partie (4 chiffres) ; l'hote a le numero 1
bool SockResolve(const char *host, unsigned short port, NetAddr *out)
{
    (void)port;
    linkError[0] = '\0';
    pendingCode = 0;
    int code = atoi(host);
    if (strlen(host) != 4 || code < 1000 || code > 9999) return false;
    if (UseRoom()) { gameCode = code; *out = (NetAddr){ 1, 0, 0 }; }
    else { pendingCode = code; *out = (NetAddr){ 0, 0, 1 }; }
    return true;
}

// Ce que l'hote affiche aux autres joueurs : le code de la partie
int SockLocalIPs(char out[][40], int max)
{
    int code = backend == BACK_ROOM ? gameCode : SockOnlineCode();
    if (max < 1 || !opened || code == 0) return 0;
    snprintf(out[0], 40, "%04d", code);
    return 1;
}

bool SockAddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port && a.relay == b.relay; }

void SockAnnounce(int players, int maxPlayers, int wave, bool playing)
{
    if (!opened) return;
    if (backend == BACK_ROOM) znet_announce(gameCode, players, maxPlayers, wave, playing ? 1 : 0);
    else OnlineStatus(players, maxPlayers, wave, playing);
}

// A chaque image : connexion au serveur (perdue ?), repetitions, liste des parties
void SockUpdate(float dt, float listEvery, bool drain)
{
    InitOnline();
    if (UseRoom()) return;
    bool open = zws_open() != 0;
    int epoch = zws_epoch();
    // WebSocket fermee (ou rouverte entre deux images) : la partie relayee est perdue
    if ((wsWasOpen && !open) || (wsEpoch != 0 && epoch != wsEpoch)) OnlineLinkLost();
    wsWasOpen = open;
    wsEpoch = epoch;
    OnlineTick(dt, listEvery);
    if (drain)
    {
        static unsigned char pkt[ZCB_MSG_MAX];
        NetAddr from;
        while (SockRecv(pkt, sizeof(pkt), &from) > 0) {}
    }
}

SockLink SockLinkKind(void) { return (backend == BACK_ROOM || (backend == BACK_NONE && UseRoom())) ? LINK_ROOM : LINK_SERVER; }

bool SockPeerGone(NetAddr *who)
{
    unsigned short relay;
    if (backend != BACK_SERVER || !OnlinePopGone(&relay)) return false;
    *who = (NetAddr){ 0, 0, relay };
    return true;
}

const char *SockLinkError(void)
{
    if (backend == BACK_SERVER && OnlineGetState() == OL_FAILED) return OnlineError();
    return linkError;
}

int SockOnlineCode(void)
{
    if (backend == BACK_ROOM) return opened ? gameCode : 0;
    return OnlineGetState() == OL_HOSTING ? OnlineCode() : 0;
}

const char *SockOnlineInfo(void)
{
    if (backend != BACK_SERVER) return "";
    switch (OnlineGetState())
    {
        case OL_HOST_WAIT: return OnlineError()[0] ? OnlineError() : "Connexion au serveur de parties...";
        case OL_HOSTING:
        case OL_HOST_REFUSED: return OnlineError();
        default: return "";
    }
}

void SockSetListed(bool on) { OnlineSetListed(on); }
bool SockListed(void) { return UseRoom() ? true : OnlineListed(); }

int SockOnlineTotals(int *players)
{
    if (!UseRoom()) return OnlineTotals(players);
    SockGame games[40];
    int n = SockListGames(games, 40), total = 0;
    for (int i = 0; i < n; i++) total += games[i].players;
    if (players) *players = total;
    return n;
}

int SockListGames(SockGame *out, int max)
{
    static char text[4096];
    if (!UseRoom()) return OnlineGames(out, max);
    znet_games(text, sizeof(text));
    int n = 0;
    // Une ligne par partie ; le nom (dernier champ) peut contenir des espaces
    for (char *line = strtok(text, "\n"); line && n < max; line = strtok(NULL, "\n"))
    {
        SockGame g = { 0 };
        int playing = 0, used = 0;
        if (sscanf(line, "%d|%d|%d|%d|%d|%n", &g.code, &g.players, &g.maxPlayers, &g.wave, &playing, &used) < 5 || used == 0) continue;
        g.playing = playing != 0;
        g.version = NET_VERSION;    // meme page, donc meme version du jeu
        g.platform = ZP_WEB;
        snprintf(g.name, sizeof(g.name), "%s", line + used);
        out[n++] = g;
    }
    return n;
}

#endif   // BR_WEB
