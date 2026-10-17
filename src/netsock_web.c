/*
 * netsock_web.c - "Sockets" de la version navigateur.
 *
 * Un navigateur ne peut pas ouvrir de socket UDP. Sur la page claude.ai du
 * jeu, la capacite "room" relaie de petits messages (4 Ko au plus, ~40 par
 * seconde, pertes possibles) entre les personnes qui ont la page ouverte :
 * exactement le comportement de l'UDP, donc net.c fonctionne sans changement.
 *
 * Le travail est fait par le module JavaScript ZNet (web/shell.html) ; ce
 * fichier relie l'interface de netsock.h a ce module :
 *  - une partie = un salon nomme "zcb-<code>", le code a 4 chiffres est tire
 *    au hasard par l'hote et tape par les autres a la place d'une adresse IP ;
 *  - les "adresses" sont des numeros : 1 = l'hote (vu d'un client), 2, 3...
 *    = les clients (vus de l'hote), attribues par ZNet a chaque nouveau joueur ;
 *    0 = tous les clients (diffusion de l'etat du monde) ;
 *  - chaque hote s'annonce dans le salon commun de la page : tout le monde
 *    voit la liste des parties ouvertes (SockListGames) et en rejoint une
 *    d'un clic, sans avoir a taper le code.
 * Hors de claude.ai (GitHub Pages, fichier zip...), ZNet n'est pas disponible
 * et le jeu cache simplement le menu reseau.
 */
#if defined(BR_WEB)

#include "netsock.h"

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

static int gameCode;        // code de la partie (4 chiffres)
static bool opened;

bool SockAvailable(void) { return znet_available() != 0; }

// port != 0 : hote (on tire un code) ; port == 0 : client (code donne par SockResolve)
bool SockOpen(unsigned short port)
{
    if (!SockAvailable()) return false;
    if (port != 0)
    {
        srand((unsigned)time(NULL));
        gameCode = 1000 + rand() % 9000;
    }
    znet_open(gameCode, port != 0);
    opened = true;
    return true;
}

void SockClose(void)
{
    if (opened) znet_close();
    opened = false;
}

bool SockSend(NetAddr to, const void *data, int len)
{
    return opened && znet_send((int)to.ip, (const unsigned char *)data, len) != 0;
}

// Adresse 0 = tous les clients : ZNet diffuse le paquet en un seul message
bool SockSendMany(const NetAddr *to, int n, const void *data, int len)
{
    (void)to;
    return opened && n > 0 && znet_send(0, (const unsigned char *)data, len) != 0;
}

int SockRecv(void *buf, int cap, NetAddr *from)
{
    if (!opened) return 0;
    int idx = 0;
    int n = znet_recv((unsigned char *)buf, cap, &idx);
    if (n > 0) { from->ip = (unsigned int)idx; from->port = 0; }
    return n;
}

// "Adresse" tapee par le joueur = code de la partie (4 chiffres) ; l'hote a le numero 1
bool SockResolve(const char *host, unsigned short port, NetAddr *out)
{
    (void)port;
    int code = atoi(host);
    if (strlen(host) != 4 || code < 1000 || code > 9999) return false;
    gameCode = code;
    out->ip = 1;
    out->port = 0;
    return true;
}

// Ce que l'hote affiche aux autres joueurs : le code de la partie
int SockLocalIPs(char out[][40], int max)
{
    if (max < 1 || !opened) return 0;
    snprintf(out[0], 40, "%04d", gameCode);
    return 1;
}

bool SockAddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port; }

void SockAnnounce(int players, int maxPlayers, int wave, bool playing)
{
    if (opened) znet_announce(gameCode, players, maxPlayers, wave, playing ? 1 : 0);
}

int SockListGames(SockGame *out, int max)
{
    static char text[4096];
    if (!SockAvailable()) return 0;
    znet_games(text, sizeof(text));
    int n = 0;
    // Une ligne par partie ; le nom (dernier champ) peut contenir des espaces
    for (char *line = strtok(text, "\n"); line && n < max; line = strtok(NULL, "\n"))
    {
        SockGame g = { 0 };
        int playing = 0, used = 0;
        if (sscanf(line, "%d|%d|%d|%d|%d|%n", &g.code, &g.players, &g.maxPlayers, &g.wave, &playing, &used) < 5 || used == 0) continue;
        g.playing = playing != 0;
        snprintf(g.name, sizeof(g.name), "%s", line + used);
        out[n++] = g;
    }
    return n;
}

#endif   // BR_WEB
