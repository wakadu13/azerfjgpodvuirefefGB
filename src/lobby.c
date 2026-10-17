/*
 * lobby.c - Liste des parties en ligne (voir lobby.h pour le principe).
 *
 * Trois morceaux :
 *  1. un petit client MQTT 3.1.1 (TCP, sans chiffrement, QoS 0) :
 *       zcb/v5/g/<id>  annonce d'une partie (message "retenu" : le serveur la
 *                      garde et la donne a chaque nouvel abonne), vide = partie finie
 *       zcb/v5/j/<id>  demande d'entree d'un joueur (son adresse publique et locale)
 *     Format des annonces (texte) :
 *       1|heure unix|joueurs|max|vague|lancee|ip:port publique|ip:port locale|nom
 *     et des demandes : 1|ip:port publique|ip:port locale|nombre au hasard
 *  2. STUN (RFC 5389) : une requete "Binding" envoyee depuis la socket UDP du
 *     jeu ; la reponse donne l'adresse et le port vus depuis Internet ;
 *  3. le percage (hole punching) : a chaque demande, l'hote envoie quelques
 *     petits paquets "ZCBP" vers le joueur. Sa box note ce trajet et laisse
 *     ensuite entrer les paquets de ce joueur, qui envoie de son cote ses HELLO
 *     habituels (voir net.c) vers l'adresse publique ET locale de l'hote.
 */
#if !defined(BR_WEB)

#include "lobby.h"
#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOPIC_GAMES   "zcb/v5/g/"   // + id
#define TOPIC_JOIN    "zcb/v5/j/"   // + id
#define LOBBY_MAX     32            // parties gardees en memoire
#define GAME_TIMEOUT  40.0f         // une annonce non rafraichie depuis 40 s est oubliee
#define STUN_TRIES    6             // requetes STUN (une par seconde) avant d'abandonner

// Reglages : config.h, ou variables d'environnement (tests sur PC)
static const char *Setting(const char *env, const char *value)
{
    const char *e = getenv(env);
    return e ? e : value;
}
static const char *Host(void) { return Setting("ZCB_LOBBY_HOST", LOBBY_HOST); }
static int Port(void) { return atoi(Setting("ZCB_LOBBY_PORT", TO_STR(LOBBY_PORT))); }
static const char *User(void) { return Setting("ZCB_LOBBY_USER", LOBBY_USER); }
static const char *Pass(void) { return Setting("ZCB_LOBBY_PASS", LOBBY_PASS); }

// Tirage au hasard (xorshift), initialise avec l'heure
static uint32_t Random32(void)
{
    static uint32_t s;
    if (!s) s = (uint32_t)time(NULL)*2654435761u ^ (uint32_t)(uintptr_t)&s;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

//----------------------------------------------------------------------------------
// Etat
//----------------------------------------------------------------------------------
typedef enum { M_OFF, M_TCP, M_CONNACK, M_READY } MqttState;
static MqttState mstate;
static float retryT, connT, pingT, idleT;
static uint8_t rx[16384];          // octets recus du serveur pas encore traites
static int rxLen;
static uint16_t packetId;
static char status[96];            // derniere erreur ou etape en cours
static bool watching, hosting, joining;
static double now;                 // temps ecoule (secondes), pour l'age des annonces

static LobbyGame games[LOBBY_MAX];
static double gameSeen[LOBBY_MAX]; // derniere annonce recue (now)
static int gameCount;

// STUN
static NetAddr stunAddr, myPub;
static bool stunResolved, stunDone;
static uint8_t stunTx[12];         // identifiant de la requete en cours
static int stunTries;
static float stunT;

// Hote
static char hostId[12], hostName[40];
static int hPlayers, hMax, hWave;
static bool hPlaying, announceDirty;
static float announceT, sinceAnnounce;

// Percage : paquets a envoyer vers les joueurs qui veulent entrer
typedef struct { NetAddr to; int left; float t; } Punch;
static Punch punches[16];

// Joueur qui rejoint
static char joinId[12];
static float joinT;

//----------------------------------------------------------------------------------
// MQTT
//----------------------------------------------------------------------------------
// Longueur "restante" : 7 bits par octet, bit 8 = il y a un octet de plus
static int PutLen(uint8_t *p, int len)
{
    int n = 0;
    do
    {
        uint8_t b = len % 128;
        len /= 128;
        if (len) b |= 128;
        p[n++] = b;
    } while (len);
    return n;
}

// Chaine MQTT : longueur sur 2 octets (gros-boutiste) puis les caracteres
static int PutStr(uint8_t *p, const char *s)
{
    int len = (int)strlen(s);
    p[0] = (uint8_t)(len >> 8);
    p[1] = (uint8_t)(len & 255);
    memcpy(p + 2, s, len);
    return 2 + len;
}

static void Fail(const char *why)
{
    TcpClose();
    mstate = M_OFF;
    retryT = 5;                    // nouvel essai dans 5 s
    rxLen = 0;
    snprintf(status, sizeof(status), "%s", why);
}

// Envoie tout le paquet (les paquets sont petits : le tampon d'envoi suffit presque toujours)
static bool SendAll(const uint8_t *d, int len)
{
    int off = 0, spins = 0;
    while (off < len)
    {
        int n = TcpSend(d + off, len - off);
        if (n < 0) { Fail("Connexion au serveur perdue"); return false; }
        if (n == 0 && ++spins > 2000) { Fail("Serveur trop lent"); return false; }
        off += n;
    }
    return true;
}

// En-tete (type + longueur) puis contenu
static bool SendPacket(uint8_t type, const uint8_t *body, int len)
{
    uint8_t buf[1200];
    if (len + 5 > (int)sizeof(buf)) return false;
    buf[0] = type;
    int h = 1 + PutLen(buf + 1, len);
    if (len) memcpy(buf + h, body, len);
    return SendAll(buf, h + len);
}

// CONNECT : protocole "MQTT" niveau 4 (3.1.1), session neuve, 60 s de silence maximum
static void MqttConnect(void)
{
    uint8_t b[600];
    int n = PutStr(b, "MQTT");
    b[n++] = 4;
    uint8_t flags = 0x02;          // clean session
    if (User()[0]) flags |= 0x80;
    if (Pass()[0]) flags |= 0x40;
    b[n++] = flags;
    b[n++] = 0; b[n++] = 60;       // keep-alive : 60 s
    char cid[24];
    snprintf(cid, sizeof(cid), "zcb-%08x", (unsigned)Random32());
    n += PutStr(b + n, cid);
    if (User()[0]) n += PutStr(b + n, User());
    if (Pass()[0]) n += PutStr(b + n, Pass());
    SendPacket(0x10, b, n);
}

static void MqttSubscribe(const char *topic)
{
    uint8_t b[160];
    int n = 0;
    if (++packetId == 0) packetId = 1;
    b[n++] = (uint8_t)(packetId >> 8);
    b[n++] = (uint8_t)(packetId & 255);
    n += PutStr(b + n, topic);
    b[n++] = 0;                    // QoS 0
    SendPacket(0x82, b, n);
}

static void MqttPublish(const char *topic, const char *payload, bool retain)
{
    uint8_t b[700];
    int n = PutStr(b, topic);
    int len = (int)strlen(payload);
    memcpy(b + n, payload, len);
    SendPacket((uint8_t)(0x30 | (retain ? 1 : 0)), b, n + len);
}

//----------------------------------------------------------------------------------
// Messages recus
//----------------------------------------------------------------------------------
// "a.b.c.d:port" -> adresse ; "-" ou texte invalide -> false
static bool ParseAddr(const char *s, NetAddr *out)
{
    char ip[32];
    unsigned port = 0;
    if (sscanf(s, "%31[0-9.]:%u", ip, &port) != 2 || port == 0 || port > 65535) return false;
    return SockResolve(ip, (unsigned short)port, out);
}

static int FindGame(const char *id)
{
    for (int i = 0; i < gameCount; i++) if (strcmp(games[i].id, id) == 0) return i;
    return -1;
}

static void RemoveGame(int i)
{
    games[i] = games[gameCount - 1];
    gameSeen[i] = gameSeen[gameCount - 1];
    gameCount--;
}

// Annonce d'une partie (ou effacement si le message est vide)
static void OnGame(const char *id, const char *text, int len)
{
    int i = FindGame(id);
    if (len == 0) { if (i >= 0) RemoveGame(i); return; }
    if (hosting && strcmp(id, hostId) == 0) return;   // notre propre partie

    long t = 0;
    int p = 0, m = 0, w = 0, playing = 0;
    char pub[32] = "", lan[32] = "", name[40] = "";
    if (sscanf(text, "1|%ld|%d|%d|%d|%d|%31[^|]|%31[^|]|%39[^\n]", &t, &p, &m, &w, &playing, pub, lan, name) != 8) return;
    // Annonce trop ancienne : l'hote a disparu sans pouvoir l'effacer
    long age = (long)time(NULL) - t;
    if (age > 60 || age < -300) { if (i >= 0) RemoveGame(i); return; }
    if (p < 0 || p > 999 || m < 1 || m > 999 || w < 0 || w > 9999) return;

    LobbyGame g = { 0 };
    snprintf(g.id, sizeof(g.id), "%s", id);
    // Nom : uniquement des caracteres affichables par la police du jeu
    for (int k = 0, o = 0; name[k] && o < (int)sizeof(g.name) - 1; k++)
        if (name[k] >= 32 && name[k] < 127) g.name[o++] = name[k];
    g.players = p; g.maxPlayers = m; g.wave = w; g.playing = playing != 0;
    ParseAddr(pub, &g.pub);
    ParseAddr(lan, &g.lan);
    if (i < 0)
    {
        if (gameCount == LOBBY_MAX) return;
        i = gameCount++;
    }
    games[i] = g;
    gameSeen[i] = now;
}

// Demande d'entree dans notre partie : on perce la box vers ce joueur
static void OnJoin(const char *text)
{
    char pub[32] = "", lan[32] = "";
    unsigned nonce = 0;
    if (sscanf(text, "1|%31[^|]|%31[^|]|%u", pub, lan, &nonce) < 2) return;
    NetAddr a[2];
    int n = 0;
    if (ParseAddr(pub, &a[n])) n++;
    if (ParseAddr(lan, &a[n])) n++;
    for (int k = 0; k < n; k++)
    {
        // Emplacement libre (ou deja utilise pour la meme adresse)
        int slot = -1;
        for (int j = 0; j < 16; j++)
            if (punches[j].left == 0 || SockAddrEqual(punches[j].to, a[k])) { slot = j; break; }
        if (slot >= 0) punches[slot] = (Punch){ a[k], 6, 0 };
    }
}

static void OnPublish(const char *topic, int topicLen, const char *payload, int len)
{
    char t[96], text[600];
    if (topicLen >= (int)sizeof(t) || len >= (int)sizeof(text)) return;
    memcpy(t, topic, topicLen); t[topicLen] = '\0';
    memcpy(text, payload, len); text[len] = '\0';
    size_t ng = strlen(TOPIC_GAMES), nj = strlen(TOPIC_JOIN);
    if (strncmp(t, TOPIC_GAMES, ng) == 0 && strlen(t + ng) < 12) OnGame(t + ng, text, len);
    else if (hosting && strncmp(t, TOPIC_JOIN, nj) == 0 && strcmp(t + nj, hostId) == 0) OnJoin(text);
}

static void OnConnected(void)
{
    mstate = M_READY;
    status[0] = '\0';
    pingT = 0;
    MqttSubscribe(TOPIC_GAMES "+");
    if (hosting)
    {
        char topic[40];
        snprintf(topic, sizeof(topic), TOPIC_JOIN "%s", hostId);
        MqttSubscribe(topic);
        announceDirty = true;
        announceT = 0;
    }
}

static void HandlePacket(uint8_t type, const uint8_t *b, int len)
{
    switch (type >> 4)
    {
        case 2:   // CONNACK : octet 2 = code retour (0 = accepte)
            if (len < 2) { Fail("Reponse du serveur invalide"); return; }
            if (b[1] == 0) OnConnected();
            else if (b[1] == 4 || b[1] == 5) Fail("Serveur : identifiants refuses (config.h)");
            else Fail("Serveur : connexion refusee");
            break;
        case 3:   // PUBLISH
        {
            if (len < 2) return;
            int topicLen = (b[0] << 8) | b[1];
            int p = 2 + topicLen;
            if ((type >> 1) & 3) p += 2;   // QoS 1 ou 2 : identifiant de paquet
            if (p > len) return;
            OnPublish((const char *)b + 2, topicLen, (const char *)b + p, len - p);
        } break;
        default: break;   // SUBACK, PINGRESP... : rien a faire
    }
}

// Lit ce qui est arrive et traite chaque paquet complet
static void MqttPoll(void)
{
    for (;;)
    {
        if (rxLen == (int)sizeof(rx)) break;
        int n = TcpRecv(rx + rxLen, (int)sizeof(rx) - rxLen);
        if (n < 0) { Fail("Connexion au serveur perdue"); return; }
        if (n == 0) break;
        rxLen += n;
    }
    int off = 0;
    while (rxLen - off >= 2)
    {
        // Longueur restante : 1 a 4 octets apres l'octet de type
        int len = 0, mult = 1, i = 1;
        bool complete = false;
        while (i <= 4 && off + i < rxLen)
        {
            uint8_t c = rx[off + i++];
            len += (c & 127)*mult;
            mult *= 128;
            if (!(c & 128)) { complete = true; break; }
        }
        if (!complete)
        {
            if (i > 4) { Fail("Reponse du serveur invalide"); return; }
            break;                 // longueur pas encore entierement recue
        }
        if (i + len > (int)sizeof(rx)) { Fail("Message du serveur trop long"); return; }
        if (off + i + len > rxLen) break;   // paquet incomplet : on attend la suite
        HandlePacket(rx[off], rx + off + i, len);
        if (mstate == M_OFF) return;         // la connexion a pu etre fermee
        off += i + len;
    }
    memmove(rx, rx + off, rxLen - off);
    rxLen -= off;
}

//----------------------------------------------------------------------------------
// STUN
//----------------------------------------------------------------------------------
static const uint8_t STUN_COOKIE[4] = { 0x21, 0x12, 0xA4, 0x42 };

static void StunReset(void)
{
    stunDone = false;
    stunTries = 0;
    stunT = 0;
    memset(&myPub, 0, sizeof(myPub));
}

// Requete "Binding" : type 0x0001, longueur 0, cookie magique, identifiant au hasard
static void StunSend(void)
{
    if (!stunResolved)
        stunResolved = SockResolve(Setting("ZCB_STUN_HOST", STUN_HOST), (unsigned short)atoi(Setting("ZCB_STUN_PORT", TO_STR(STUN_PORT))), &stunAddr);
    if (!stunResolved) return;
    uint8_t req[20] = { 0x00, 0x01, 0x00, 0x00 };
    memcpy(req + 4, STUN_COOKIE, 4);
    for (int i = 0; i < 12; i++) stunTx[i] = (uint8_t)Random32();
    memcpy(req + 8, stunTx, 12);
    SockSend(stunAddr, req, 20);
}

// Reponse : on cherche l'attribut XOR-MAPPED-ADDRESS (ou MAPPED-ADDRESS)
static bool StunReceive(const uint8_t *d, int len)
{
    if (len < 20 || d[0] != 0x01 || d[1] != 0x01 || memcmp(d + 4, STUN_COOKIE, 4) != 0 || memcmp(d + 8, stunTx, 12) != 0)
        return false;
    int end = 20 + ((d[2] << 8) | d[3]);
    if (end > len) end = len;
    for (int off = 20; off + 4 <= end;)
    {
        int type = (d[off] << 8) | d[off + 1], alen = (d[off + 2] << 8) | d[off + 3];
        const uint8_t *v = d + off + 4;
        if (off + 4 + alen > end) break;
        if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01)   // IPv4
        {
            bool x = (type == 0x0020);   // XOR : brouille avec le cookie (port et adresse)
            uint8_t port[2] = { (uint8_t)(v[2] ^ (x ? 0x21 : 0)), (uint8_t)(v[3] ^ (x ? 0x12 : 0)) };
            uint8_t ip[4];
            for (int i = 0; i < 4; i++) ip[i] = (uint8_t)(v[4 + i] ^ (x ? STUN_COOKIE[i] : 0));
            memcpy(&myPub.ip, ip, 4);        // deja dans l'ordre reseau
            memcpy(&myPub.port, port, 2);
            stunDone = true;
            return true;
        }
        off += 4 + ((alen + 3) & ~3);         // attributs alignes sur 4 octets
    }
    return true;
}

bool LobbyOnUdp(const unsigned char *data, int len, NetAddr from)
{
    if (len == 4 && memcmp(data, "ZCBP", 4) == 0) return true;   // paquet de percage
    if (stunResolved && SockAddrEqual(from, stunAddr)) return StunReceive(data, len);
    return false;
}

//----------------------------------------------------------------------------------
// Adresses de cette machine
//----------------------------------------------------------------------------------
static void LocalAddr(unsigned short port, char *out, int cap)
{
    char ips[4][40];
    if (SockLocalIPs(ips, 4) > 0) snprintf(out, cap, "%s:%u", ips[0], (unsigned)port);
    else snprintf(out, cap, "-");
}

static void PublicAddr(char *out, int cap)
{
    if (stunDone) SockAddrText(myPub, out, cap);
    else snprintf(out, cap, "-");
}

//----------------------------------------------------------------------------------
// Hote
//----------------------------------------------------------------------------------
void LobbyHostStart(void)
{
    hosting = true;
    snprintf(hostId, sizeof(hostId), "%08x", (unsigned)Random32());
    // Nom de la partie : nom de la machine, sans domaine ni caracteres speciaux
    char machine[64];
    SockHostName(machine, sizeof(machine));
    char *dot = strchr(machine, '.');
    if (dot) *dot = '\0';
    char clean[24] = "";
    for (int k = 0, o = 0; machine[k] && o < 20; k++)
        if ((machine[k] >= 'A' && machine[k] <= 'Z') || (machine[k] >= 'a' && machine[k] <= 'z') ||
            (machine[k] >= '0' && machine[k] <= '9') || machine[k] == '-' || machine[k] == '_') clean[o++] = machine[k];
    if (!clean[0] || strcmp(clean, "localhost") == 0) snprintf(hostName, sizeof(hostName), "Partie %.4s", hostId);
    else snprintf(hostName, sizeof(hostName), "Partie de %s", clean);
    hPlayers = 1; hMax = 1; hWave = 0; hPlaying = false;
    announceDirty = true;
    announceT = 0;
    sinceAnnounce = 100;
    memset(punches, 0, sizeof(punches));
    StunReset();
    if (mstate == M_READY)         // deja connecte (liste ouverte avant) : on s'abonne aux demandes
    {
        char topic[40];
        snprintf(topic, sizeof(topic), TOPIC_JOIN "%s", hostId);
        MqttSubscribe(topic);
    }
}

void LobbyHostInfo(int players, int maxPlayers, int wave, bool playing)
{
    if (players != hPlayers || maxPlayers != hMax || wave != hWave || playing != hPlaying) announceDirty = true;
    hPlayers = players; hMax = maxPlayers; hWave = wave; hPlaying = playing;
}

static void Announce(void)
{
    char pub[48], lan[48], text[200], topic[40];
    PublicAddr(pub, sizeof(pub));
    LocalAddr(NET_PORT, lan, sizeof(lan));
    snprintf(text, sizeof(text), "1|%ld|%d|%d|%d|%d|%s|%s|%s", (long)time(NULL), hPlayers, hMax, hWave,
             hPlaying ? 1 : 0, pub, lan, hostName);
    snprintf(topic, sizeof(topic), TOPIC_GAMES "%s", hostId);
    MqttPublish(topic, text, true);
    announceDirty = false;
    announceT = 10;                // annonce rafraichie toutes les 10 s
    sinceAnnounce = 0;
}

void LobbyHostStop(void)
{
    if (!hosting) return;
    if (mstate == M_READY)
    {
        char topic[40];
        snprintf(topic, sizeof(topic), TOPIC_GAMES "%s", hostId);
        MqttPublish(topic, "", true);   // message retenu vide = annonce effacee
    }
    hosting = false;
    memset(punches, 0, sizeof(punches));
}

const char *LobbyHostState(void)
{
    static char text[120];
    if (!LobbyConfigured()) return "Liste en ligne : serveur non configure (config.h)";
    if (mstate != M_READY) snprintf(text, sizeof(text), "Liste en ligne : %s", status[0] ? status : "connexion au serveur...");
    else if (!stunDone && stunTries < STUN_TRIES) snprintf(text, sizeof(text), "Liste en ligne : recherche de l'adresse Internet...");
    else if (!stunDone) snprintf(text, sizeof(text), "Visible dans la liste en ligne (adresse Internet inconnue : reseau local seulement)");
    else
    {
        char pub[32];
        SockAddrText(myPub, pub, sizeof(pub));
        snprintf(text, sizeof(text), "Visible dans la liste en ligne de tous les joueurs (%s)", pub);
    }
    return text;
}

//----------------------------------------------------------------------------------
// Joueur qui rejoint
//----------------------------------------------------------------------------------
void LobbyJoinStart(const char *gameId)
{
    joining = true;
    snprintf(joinId, sizeof(joinId), "%s", gameId);
    joinT = 0;
    StunReset();
}

void LobbyJoinStop(void) { joining = false; }

//----------------------------------------------------------------------------------
// Boucle
//----------------------------------------------------------------------------------
bool LobbyConfigured(void) { return Host()[0] != '\0'; }
void LobbyWatch(bool on) { watching = on; }

const char *LobbyStatus(void)
{
    if (!LobbyConfigured()) return "Liste en ligne non configuree (voir config.h)";
    if (mstate != M_READY) return status[0] ? status : "Connexion au serveur...";
    return "";
}

int LobbyGames(LobbyGame *out, int max)
{
    int n = 0;
    for (int i = 0; i < gameCount && n < max; i++)
    {
        // Tri par nom (insertion) : la liste ne bouge pas sous le doigt
        int j = n++;
        while (j > 0 && strcmp(out[j - 1].name, games[i].name) > 0) { out[j] = out[j - 1]; j--; }
        out[j] = games[i];
    }
    return n;
}

void LobbyUpdate(float dt)
{
    now += dt;
    for (int i = gameCount - 1; i >= 0; i--) if (now - gameSeen[i] > GAME_TIMEOUT) RemoveGame(i);

    bool need = LobbyConfigured() && (watching || hosting || joining);
    if (!need)
    {
        // Plus besoin du serveur : on se deconnecte apres 10 s (on peut revenir vite au menu)
        if (mstate != M_OFF && (idleT += dt) > 10) { TcpClose(); mstate = M_OFF; status[0] = '\0'; rxLen = 0; }
        if (mstate == M_READY) MqttPoll();
    }
    else
    {
        idleT = 0;
        switch (mstate)
        {
            case M_OFF:
                if ((retryT -= dt) > 0) break;
                snprintf(status, sizeof(status), "Connexion au serveur...");
                if (TcpOpen(Host(), (unsigned short)Port())) { mstate = M_TCP; connT = 0; }
                else Fail("Serveur introuvable (pas d'Internet ?)");
                break;
            case M_TCP:
            {
                int st = TcpState();
                if (st == 2) { MqttConnect(); mstate = M_CONNACK; connT = 0; rxLen = 0; }
                else if (st == 0 || (connT += dt) > 10) Fail("Serveur injoignable");
            } break;
            case M_CONNACK:
                MqttPoll();
                if (mstate == M_CONNACK && (connT += dt) > 10) Fail("Pas de reponse du serveur");
                break;
            case M_READY:
                MqttPoll();
                if (mstate == M_READY && (pingT += dt) > 25) { pingT = 0; SendPacket(0xC0, NULL, 0); }   // PINGREQ
                break;
        }
    }

    // Adresse publique (hote ou joueur qui rejoint) : une requete STUN par seconde
    if ((hosting || joining) && !stunDone && stunTries < STUN_TRIES && (stunT -= dt) <= 0)
    {
        StunSend();
        stunTries++;
        stunT = 1.0f;
    }
    bool stunFinished = stunDone || stunTries >= STUN_TRIES;

    // Hote : annonce de la partie, et percage vers les joueurs qui veulent entrer
    if (hosting)
    {
        sinceAnnounce += dt;
        announceT -= dt;
        if (mstate == M_READY && stunFinished && (announceT <= 0 || (announceDirty && sinceAnnounce > 2))) Announce();
        for (int i = 0; i < 16; i++)
        {
            Punch *p = &punches[i];
            if (p->left <= 0 || (p->t -= dt) > 0) continue;
            SockSend(p->to, "ZCBP", 4);
            p->left--;
            p->t = 0.25f;
        }
    }

    // Joueur qui rejoint : demande d'entree toutes les 2 s (l'hote perce sa box a chaque fois)
    if (joining && mstate == M_READY && stunFinished && (joinT -= dt) <= 0)
    {
        char pub[48], lan[48], text[120], topic[40];
        PublicAddr(pub, sizeof(pub));
        LocalAddr(SockLocalPort(), lan, sizeof(lan));
        snprintf(text, sizeof(text), "1|%s|%s|%u", pub, lan, (unsigned)(Random32() % 1000000));
        snprintf(topic, sizeof(topic), TOPIC_JOIN "%s", joinId);
        MqttPublish(topic, text, false);
        joinT = 2.0f;
    }
}

#endif   // !BR_WEB
