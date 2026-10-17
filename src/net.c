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
 * En-tete commun a tous les paquets (5 octets) :
 *   octets 0-3 : NET_MAGIC (u32) -> permet d'ignorer tout paquet etranger au jeu
 *   octet  4   : type (u8, valeur PKT_xxx)
 *   puis le contenu propre a chaque type (detaille avant chaque fonction d'ecriture).
 *
 * Deroulement d'une connexion :
 *   client                          hote
 *     HELLO (toutes les 0.5 s) --->
 *                              <--- WELCOME (id)   ou   REFUSE (raison)
 *     STATE (60/s)            --->
 *                              <--- SNAPSHOT (30/s)
 *     BYE                     --->  (ou silence > NET_TIMEOUT = deconnexion)
 *
 * Les touches sur les mercenaires doivent arriver a coup sur : le client les
 * renvoie dans chaque paquet jusqu'a ce que l'hote en accuse reception.
 * (Chaque touche porte un numero croissant ; l'hote renvoie dans chaque SNAPSHOT le
 * numero de la derniere touche appliquee, et ignore les doublons deja traites.)
 * Les effets (sons, particules) peuvent se perdre sans gravite.
 * Toutes les cibles (x86, ARM) etant little-endian, les nombres sont copies tels quels.
 *
 * Jusqu'a 50 joueurs : pour que l'etat du monde reste petit, la plupart des
 * nombres a virgule sont "quantifies" sur 2 octets (PutQ16 : positions au
 * centimetre, angles et minuteries au millieme...). Le SNAPSHOT est le meme
 * pour tous les clients (une seule construction, envoye a chacun ou diffuse
 * d'un coup dans le navigateur) ; chaque client y trouve son propre accuse
 * de reception dans une petite table.
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
#include "harpoon.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_MAGIC    0x48535242u   // "BRSH"
// TO_TEXT(MAX_PLAYERS) -> "50" : ecrit la valeur d'une constante dans une chaine
#define TO_TEXT_(x)  #x
#define TO_TEXT(x)   TO_TEXT_(x)
#define NET_VERSION  4             // a incrementer a chaque changement du format des paquets
#define PACKET_MAX   16384         // taille max d'un paquet (assez pour un SNAPSHOT a 50 joueurs et 160 mercenaires)
#define MAX_EVENTS   64            // effets en attente d'envoi entre deux paquets (le surplus est perdu)
#if defined(BR_WEB)
#define STATE_EVENTS 24            // navigateur : effets par paquet STATE (le paquet doit tenir dans 4 Ko)
#else
#define STATE_EVENTS MAX_EVENTS
#endif
#define MAX_HITS     32            // touches non confirmees que le client garde en memoire
#define FEED_RING    8             // derniers messages du fil d'actualite renvoyes en boucle par l'hote

// Types de paquets (octet 4 de l'en-tete). On commence a 1 : un 0 signale un paquet invalide.
enum { PKT_HELLO = 1, PKT_WELCOME, PKT_REFUSE, PKT_STATE, PKT_SNAPSHOT, PKT_BYE };

// Un effet a rejouer chez les autres (voir NetEventType dans net.h pour le sens de a, b, param, f1, f2).
// Taille sur le reseau : 3 octets + 2 x 12 (Vector3) + 2 x 4 = 35 octets.
typedef struct {
    uint8_t type, origin, param;   // origin = joueur a l'origine (ne lui est pas renvoye)
    Vector3 a, b;
    float f1, f2;
} NetEvent;

// Une touche sur un mercenaire, detectee chez un client et a appliquer par l'hote.
typedef struct {
    uint32_t id;                   // numero unique et croissant (sert d'accuse de reception)
    uint8_t enemy, head, kind;     // index du mercenaire, tir a la tete (0/1), NetHitKind
    float dmg;                     // degats calcules par le client (plafonnes par l'hote)
    Vector3 pos, dir;              // point d'impact et direction du tir (effets, recul)
} NetHit;

// Message du fil d'actualite : id croissant pour que le client sache lesquels il a deja affiches.
typedef struct { uint32_t id; Color c; char text[96]; } NetFeedMsg;

// Ce que l'hote sait d'un client connecte (indexe par numero de joueur, l'entree 0 = l'hote lui-meme).
typedef struct {
    bool used;          // emplacement occupe
    NetAddr addr;       // adresse IP + port du client (pour lui repondre et le reconnaitre)
    float silence;      // secondes depuis le dernier paquet
    uint32_t lastHit;   // derniere touche appliquee
} RemoteClient;

// Etat commun hote / client
static NetRole role;            // NET_OFF, NET_HOST ou NET_CLIENT
static NetStatus status;        // etat de la connexion (voir NetStatus)
static char error[96];          // dernier message d'erreur affichable
static int localId;             // numero du joueur de cette machine (0 pour l'hote, -1 = inconnu)
// sendT   : compte a rebours avant le prochain envoi (cadence fixe)
// silenceT: client - secondes depuis le dernier paquet de l'hote
// helloT  : client - compte a rebours avant de renvoyer HELLO
static float sendT, silenceT, helloT;
static bool suppress;           // true = ne pas enregistrer les effets (rejoues ou purement locaux)
static int recordOrigin;        // hote : joueur a qui attribuer les effets en cours d'enregistrement

// Hote
static RemoteClient clients[MAX_PLAYERS];   // clients connectes (indice = numero de joueur)
static NetFeedMsg feedRing[FEED_RING];      // derniers messages, [0] = le plus recent
static uint32_t nextFeedId = 1;             // prochain id de message (0 = emplacement vide)

// Client
static NetAddr hostAddr;            // adresse de l'hote
static NetHit hits[MAX_HITS];       // touches envoyees mais pas encore confirmees par l'hote
static int hitCount;                // nombre d'entrees valides dans hits[]
static uint32_t nextHitId;          // dernier id de touche attribue
static uint32_t lastFeedSeen;       // id du plus recent message du fil deja traite
static bool firstSnapshot;          // true jusqu'au 1er SNAPSHOT (pour ne pas afficher le vieil historique)

// Evenements en attente d'envoi
// (hote : envoyes a tous les clients au prochain SNAPSHOT ; client : envoyes a l'hote au prochain STATE)
static NetEvent events[MAX_EVENTS];
static int eventCount;

//----------------------------------------------------------------------------------
// Lecture / ecriture de paquets
//----------------------------------------------------------------------------------
// Tampon de (de)serialisation : data = octets, pos = curseur courant, size = limite.
// err passe a true des qu'on deborde : on peut donc enchainer les Put/Get sans tester
// a chaque fois, et verifier err une seule fois a la fin (les lectures fautives rendent 0).
typedef struct { uint8_t *data; int pos, size; bool err; } Buf;

// Ecrit n octets bruts a la position courante (ou leve err si ca ne rentre pas)
static void Put(Buf *b, const void *p, int n)
{
    if (b->pos + n > b->size) { b->err = true; return; }
    memcpy(b->data + b->pos, p, n);
    b->pos += n;
}
// Ecritures typees : la valeur est d'abord convertie a la bonne taille puis copiee telle quelle
// (little-endian, cf. en-tete du fichier).
static void PutU8(Buf *b, int v) { uint8_t x = (uint8_t)v; Put(b, &x, 1); }      // 1 octet, 0..255
static void PutI16(Buf *b, int v) { int16_t x = (int16_t)v; Put(b, &x, 2); }     // 2 octets, -32768..32767
static void PutU32(Buf *b, uint32_t v) { Put(b, &v, 4); }
static void PutI32(Buf *b, int v) { int32_t x = v; Put(b, &x, 4); }
static void PutF32(Buf *b, float v) { Put(b, &v, 4); }                            // float IEEE 754
static void PutU16(Buf *b, int v) { uint16_t x = (uint16_t)v; Put(b, &x, 2); }   // 2 octets, 0..65535 (ou compteur qui reboucle)
// Nombre a virgule compresse sur 2 octets : v*scale arrondi, borne a -32768..32767.
// Ex. scale = 100 : precision du centimetre, de -327 a +327 metres.
static void PutQ16(Buf *b, float v, float scale) { PutI16(b, (int)Clamp(roundf(v*scale), -32768, 32767)); }
// Position au centimetre (6 octets au lieu de 12)
static void PutPos(Buf *b, Vector3 v) { PutQ16(b, v.x, 100); PutQ16(b, v.y, 100); PutQ16(b, v.z, 100); }
// Valeur de 0 a 1 sur un octet (0..255)
static void PutUnit(Buf *b, float v) { PutU8(b, (int)(Clamp(v, 0, 1)*255)); }
// Ramene un angle dans [-PI, PI] (le lissage a l'arrivee prend le chemin le plus court)
static float WrapAngle(float a)
{
    a = fmodf(a + PI, 2*PI);
    if (a < 0) a += 2*PI;
    return a - PI;
}
// Chaine : 1 octet de longueur puis les caracteres, sans le '\0' final (tronquee a 95)
static void PutStr(Buf *b, const char *s)
{
    int n = (int)strlen(s);
    if (n > 95) n = 95;
    PutU8(b, n);
    Put(b, s, n);
}

// Lit n octets bruts. En cas de debordement (paquet tronque ou malforme), err passe a true
// et la destination est remplie de zeros pour ne jamais laisser de valeur indefinie.
static void Get(Buf *b, void *p, int n)
{
    if (b->pos + n > b->size) { b->err = true; memset(p, 0, n); return; }
    memcpy(p, b->data + b->pos, n);
    b->pos += n;
}
// Lectures typees, symetriques des Put* ci-dessus
static int GetU8(Buf *b) { uint8_t x; Get(b, &x, 1); return x; }
static int GetI16(Buf *b) { int16_t x; Get(b, &x, 2); return x; }
static uint32_t GetU32(Buf *b) { uint32_t x; Get(b, &x, 4); return x; }
static int GetI32(Buf *b) { int32_t x; Get(b, &x, 4); return x; }
static float GetF32(Buf *b) { float x; Get(b, &x, 4); return x; }
static int GetU16(Buf *b) { uint16_t x; Get(b, &x, 2); return x; }
static float GetQ16(Buf *b, float scale) { return GetI16(b)/scale; }
static Vector3 GetPos(Buf *b) { Vector3 v; v.x = GetQ16(b, 100); v.y = GetQ16(b, 100); v.z = GetQ16(b, 100); return v; }
static float GetUnit(Buf *b) { return GetU8(b)/255.0f; }
// Lit une chaine ecrite par PutStr dans out (cap = taille du tableau, '\0' compris).
// Toujours terminee par '\0' ; chaine vide si le paquet etait incomplet.
static void GetStr(Buf *b, char *out, int cap)
{
    int n = GetU8(b);
    if (n >= cap) n = cap - 1;
    Get(b, out, n);
    out[b->err ? 0 : n] = '\0';
}

// Prepare un nouveau paquet dans 'storage' (PACKET_MAX octets) et ecrit l'en-tete :
// NET_MAGIC (4 octets) + type (1 octet). L'appelant peut reduire b->size si son tampon est plus petit.
static void BeginPacket(Buf *b, uint8_t *storage, int type)
{
    *b = (Buf){ storage, 0, PACKET_MAX, false };
    PutU32(b, NET_MAGIC);
    PutU8(b, type);
}

// Envoie un paquet sans contenu (juste l'en-tete), par exemple BYE.
static void SendSimple(NetAddr to, int type)
{
    uint8_t data[16];
    Buf b;
    BeginPacket(&b, data, type);
    b.size = sizeof(data);      // le vrai tampon ne fait que 16 octets
    SockSend(to, b.data, b.pos);
}

// Ajoute au paquet au plus 'max' effets en attente :
// 1 octet = nombre N, puis N x 17 octets : type, origin, param (3 x u8),
// a et b au centimetre (2 x 6 octets), f1 et f2 au centieme sur un octet
// (hauteur et volume d'un son : de 0 a 2,55).
// N tient sur un octet car MAX_EVENTS (64) < 256.
static void PutEvents(Buf *b, int max)
{
    int n = eventCount < max ? eventCount : max;
    PutU8(b, n);
    for (int i = 0; i < n; i++)
    {
        const NetEvent *e = &events[i];
        PutU8(b, e->type); PutU8(b, e->origin); PutU8(b, e->param);
        PutPos(b, e->a); PutPos(b, e->b);
        PutU8(b, (int)Clamp(roundf(e->f1*100), 0, 255)); PutU8(b, (int)Clamp(roundf(e->f2*100), 0, 255));
    }
}

// Lit un effet (meme ordre que dans PutEvents)
static NetEvent GetEvent(Buf *b)
{
    NetEvent e;
    e.type = (uint8_t)GetU8(b); e.origin = (uint8_t)GetU8(b); e.param = (uint8_t)GetU8(b);
    e.a = GetPos(b); e.b = GetPos(b);
    e.f1 = GetU8(b)/100.0f; e.f2 = GetU8(b)/100.0f;
    return e;
}

// Rejoue localement un effet venu d'une autre machine
// suppress = true pendant l'appel : les fonctions d'effets appellent elles-memes NetRecordEvent,
// et sans ce garde-fou l'effet serait re-enregistre puis renvoye en boucle sur le reseau.
static void ApplyEvent(const NetEvent *e)
{
    suppress = true;
    switch (e->type)
    {
        case EV_TRACER: AddTracer(e->a, e->b); break;
        case EV_IMPACT:  BurstImpact(e->a, e->b, e->param); break;
        case EV_SPARKS: BurstSparks(e->a, e->b); break;
        case EV_FEATHERS: BurstFeathers(e->a); break;
        case EV_HARPOON: HarpoonSpawnRemote(e->a, e->b); break;
        case EV_SOUND:
            // On verifie l'indice du son : un paquet corrompu ne doit pas lire hors du tableau
            if (e->param < SND_COUNT)   // les tirs des autres un peu moins forts que les siens
                PlaySfx((SndId)e->param, e->f1, e->param == SND_SHOT ? e->f2*0.6f : e->f2);
            break;
        default: break;             // type inconnu (version differente, paquet abime) : ignore
    }
    suppress = false;
}

// Met un effet dans la file d'envoi (silencieusement perdu si la file est pleine : pas grave)
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
// "Autorite" = la machine qui decide de l'etat du monde : tout le monde sauf un client
bool NetIsAuthority(void) { return role != NET_CLIENT; }
void NetSuppress(bool on) { suppress = on; }

// Remet a zero toutes les donnees de session avant d'heberger ou de rejoindre une partie
static void ResetState(void)
{
    memset(clients, 0, sizeof(clients));
    memset(feedRing, 0, sizeof(feedRing));
    eventCount = hitCount = 0;
    sendT = silenceT = helloT = 0;     // 0 = premier envoi (HELLO, STATE...) des la prochaine frame
    lastFeedSeen = 0;
    suppress = false;
    error[0] = '\0';
}

// Devient hote : ouvre le port fixe NET_PORT pour que les clients puissent nous joindre.
// L'hote est immediatement "connecte" et occupe l'emplacement de joueur 0.
// Le reseau est-il possible ici ? Toujours sur PC / mobile ; dans le
// navigateur, seulement sur la page claude.ai (relais "room", netsock_web.c).
bool NetSupported(void)
{
#if defined(BR_WEB)
    return SockAvailable();
#else
    return true;
#endif
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

// Rejoint un hote. Ne fait que preparer la connexion : les HELLO sont envoyes par
// ClientUpdate() et la reponse est traitee par ClientReceive(). Retourne false si
// l'adresse est invalide ou si la socket ne s'ouvre pas (status = NET_FAILED).
bool NetJoin(const char *address)
{
    NetStop();
    ResetState();

    // "ip" ou "ip:port"
    char host[64];
    snprintf(host, sizeof(host), "%s", address);     // copie modifiable (et tronquee si trop longue)
    unsigned short port = NET_PORT;
    char *colon = strchr(host, ':');
    // On coupe la chaine au ':' : host garde l'adresse, ce qui suit est le port
    if (colon) { *colon = '\0'; port = (unsigned short)atoi(colon + 1); }
    if (!host[0] || !SockResolve(host, port, &hostAddr))
    {
#if defined(BR_WEB)
        snprintf(error, sizeof(error), "Code invalide : %s (4 chiffres)", address);
#else
        snprintf(error, sizeof(error), "Adresse invalide : %s", address);
#endif
        status = NET_FAILED;
        return false;
    }
    // Port 0 : le client n'a pas besoin d'un port fixe, le systeme en choisit un libre
    if (!SockOpen(0))
    {
        snprintf(error, sizeof(error), "Impossible d'ouvrir une socket");
        status = NET_FAILED;
        return false;
    }
    role = NET_CLIENT;
    status = NET_CONNECTING;
    localId = -1;               // numero attribue plus tard par le WELCOME de l'hote
    return true;
}

// Quitte la partie reseau : envoie BYE pour que les autres le sachent tout de suite
// (sinon ils attendraient NET_TIMEOUT secondes), puis ferme la socket.
// BYE n'est envoye qu'une fois : s'il se perd, le delai d'inactivite prendra le relais.
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
// Appele par les modules d'effets et de sons chaque fois qu'ils produisent quelque chose
// de visible/audible : l'effet est mis en file pour etre rejoue chez les autres joueurs.
void NetRecordEvent(NetEventType type, Vector3 a, Vector3 b, int param, float f1, float f2)
{
    if (role == NET_OFF || suppress || status != NET_CONNECTED) return;
    // Origine : sur l'hote, c'est recordOrigin (peut etre un client dont on applique la touche,
    // pour ne pas lui renvoyer ses propres effets) ; sur un client, c'est toujours lui-meme.
    NetEvent e = { (uint8_t)type, (uint8_t)(role == NET_HOST ? recordOrigin : localId), (uint8_t)param, a, b, f1, f2 };
    QueueEvent(e);
}

// Hote : memorise un message du fil d'actualite. Les FEED_RING derniers messages sont renvoyes
// dans CHAQUE snapshot ; ainsi, meme si des paquets se perdent, le client finit par les recevoir.
void NetRecordFeed(Color c, const char *text)
{
    if (role != NET_HOST || suppress) return;
    // Decale tout d'un cran (le plus ancien, en derniere case, est ecrase) et insere en tete
    for (int i = FEED_RING - 1; i > 0; i--) feedRing[i] = feedRing[i - 1];
    feedRing[0].id = nextFeedId++;
    feedRing[0].c = c;
    snprintf(feedRing[0].text, sizeof(feedRing[0].text), "%s", text);
}

// Client : ajoute une touche a la file des touches a confirmer. Elle sera renvoyee dans
// chaque paquet STATE tant que l'hote n'aura pas accuse reception de son id.
void NetReportHit(int enemy, float dmg, bool head, Vector3 pos, Vector3 dir, NetHitKind kind)
{
    if (role != NET_CLIENT) return;
    if (hitCount == MAX_HITS)   // file pleine : on oublie la plus ancienne
    {
        // memmove (et non memcpy) car source et destination se chevauchent
        memmove(hits, hits + 1, (MAX_HITS - 1)*sizeof(NetHit));
        hitCount--;
    }
    // ++nextHitId : le premier id vaut 1, car l'hote part de lastHit = 0 ("rien recu")
    hits[hitCount++] = (NetHit){ ++nextHitId, (uint8_t)enemy, (uint8_t)head, (uint8_t)kind, dmg, pos, dir };
}

//----------------------------------------------------------------------------------
// Hote
//----------------------------------------------------------------------------------
// Retrouve le numero de joueur d'un client d'apres son adresse (-1 si inconnu).
// La boucle commence a 1 : l'emplacement 0 est celui de l'hote.
static int FindClient(NetAddr a)
{
    for (int i = 1; i < MAX_PLAYERS; i++) if (clients[i].used && SockAddrEqual(clients[i].addr, a)) return i;
    return -1;
}

// Retire un client de la partie et l'annonce dans le fil (why = "a quitte la partie"...).
// FeedAdd est retransmis a tous ; "J%d" affiche le numero de joueur en partant de 1.
static void DropClient(int id, const char *why)
{
    clients[id].used = false;
    PlayerLeave(id);
    FeedAdd(GRAY, "J%d %s", id + 1, why);
}

// Paquet REFUSE : en-tete + chaine (raison lisible affichee chez le client)
static void SendRefuse(NetAddr to, const char *reason)
{
    uint8_t data[128];
    Buf b;
    BeginPacket(&b, data, PKT_REFUSE);
    b.size = sizeof(data);
    PutStr(&b, reason);
    SockSend(to, b.data, b.pos);
}

// Paquet HELLO recu : contenu = 1 octet (NET_VERSION du client).
// Attribue un emplacement libre et repond WELCOME (contenu = 1 octet : numero de joueur).
// Si le client est deja connu (son WELCOME precedent s'est perdu et il renvoie HELLO),
// on lui renvoie simplement le meme numero.
static void HostHandleHello(Buf *b, NetAddr from)
{
    int version = GetU8(b);
    if (version != NET_VERSION) { SendRefuse(from, "Version du jeu differente de celle de l'hote"); return; }

    int id = FindClient(from);
    if (id < 0)
    {
        // Premier emplacement (1..MAX_PLAYERS-1) libre a la fois cote reseau et cote joueurs
        for (int i = 1; i < MAX_PLAYERS && id < 0; i++) if (!clients[i].used && !players[i].connected) id = i;
        if (id < 0) { SendRefuse(from, "La partie est pleine (" TO_TEXT(MAX_PLAYERS) " joueurs)"); return; }
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

// Applique sur l'hote une touche signalee par le client 'id'.
// Le client n'est pas cru sur parole a 100% : indice verifie, mercenaire encore vivant,
// et degats plafonnes au maximum possible (balle * multiplicateur de tir a la tete).
static void HostApplyHit(int id, const NetHit *h)
{
    if (h->enemy >= MAX_ENEMIES) return;
    Enemy *e = &enemies[h->enemy];
    if (!e->active || e->state == ES_DEAD) return;
    recordOrigin = id;   // les effets produits ne seront pas renvoyes au tireur
    if (h->kind == HIT_HARPOON) EnemyHarpoon(e, h->dir, &players[id]);
    else EnemyDamage(e, fminf(h->dmg, BULLET_DAMAGE*HEADSHOT_MULT), h->head, h->pos, h->dir, &players[id]);
    recordOrigin = localId;     // retour a l'origine normale (l'hote lui-meme)
}

// Paquet STATE recu du client 'id'. Contenu (dans l'ordre) :
//   u8  round           manche que le client croit jouer (8 bits de poids faible de G.round)
//   u8  respawnSeen     derniere reapparition vue par le client (8 bits de poids faible)
//   pos, vel            position (cm) et vitesse (cm/s), 6 + 6 octets
//   q16 yaw, pitch ; f32 bob   orientation de la camera et balancement de la marche
//   u8  flags           bit0 au sol, bit1 sprint, bit2 rechargement, bit3 harpon en main, bit4 harpon charge
//   q16 muzzleT, reloadT, reloadDur ; u8 ads ; q16 sprintT, harpoonT   minuteries/animations de l'arme
//   i16 ammo, reserve, harpoons                              munitions
//   u8  nombre de touches, puis par touche : u32 id, u8 enemy, u8 head, u8 kind, f32 dmg, pos (cm), q16 x 3 dir
//   u8  nombre d'effets, puis les effets (voir PutEvents)
static void HostHandleState(Buf *b, int id)
{
    Player *p = &players[id];
    int round = GetU8(b), respawnSeen = GetU8(b);
    Vector3 pos = GetPos(b), vel = GetPos(b);
    float yaw = GetQ16(b, 10000), pitch = GetQ16(b, 10000), bob = GetF32(b);
    int flags = GetU8(b);
    float muzzleT = GetQ16(b, 1000), reloadT = GetQ16(b, 1000), reloadDur = GetQ16(b, 1000);
    float ads = GetUnit(b), sprintT = GetQ16(b, 1000);
    float harpoonT = GetQ16(b, 1000);
    int ammo = GetI16(b), reserve = GetI16(b), harpoonCount = GetI16(b);
    if (b->err) return;

    // Paquet d'une partie precedente ou d'avant la reapparition : position perimee
    // (& 0xFF : on ne compare que l'octet de poids faible, seul transmis sur le reseau)
    bool fresh = (round == (G.round & 0xFF)) && (respawnSeen == (p->respawnCount & 0xFF));
    if (fresh)
    {
        // netPos / netYaw / netPitch sont des cibles : le rendu y glisse en douceur (lissage)
        p->netPos = pos;
        p->vel = vel;
        p->netYaw = yaw;
        p->netPitch = pitch;
        p->bob = bob;
        p->grounded = flags & 1;
        p->sprinting = flags & 2;
        p->w.reloading = flags & 4;
        p->w.active = (flags & 8) ? WPN_HARPOON : WPN_RIFLE;
        p->w.harpoonLoaded = flags & 16;
        p->w.harpoonT = harpoonT;
        p->w.harpoons = harpoonCount;
        p->w.muzzleT = muzzleT;
        p->w.reloadT = reloadT;
        p->w.reloadDur = reloadDur;
        p->w.ads = ads;
        p->w.sprintT = sprintT;
        p->w.ammo = ammo;
        p->w.reserve = reserve;
    }

    // Touches : on lit toujours tout (pour rester aligne dans le paquet), mais on n'applique
    // que celles plus recentes que la derniere deja traitee -> chaque touche compte une seule fois
    // meme si le client la renvoie dans plusieurs paquets.
    int nh = GetU8(b);
    for (int i = 0; i < nh && !b->err; i++)
    {
        NetHit h;
        h.id = GetU32(b); h.enemy = (uint8_t)GetU8(b); h.head = (uint8_t)GetU8(b); h.kind = (uint8_t)GetU8(b); h.dmg = GetF32(b);
        h.pos = GetPos(b); h.dir.x = GetQ16(b, 10000); h.dir.y = GetQ16(b, 10000); h.dir.z = GetQ16(b, 10000);
        if (b->err || h.id <= clients[id].lastHit) continue;
        clients[id].lastHit = h.id;     // sera renvoye comme accuse de reception dans le SNAPSHOT
        // Une touche perimee ou hors phase de jeu est consideree comme recue mais ignoree
        if (fresh && G.state == ST_PLAY) HostApplyHit(id, &h);
    }

    // Effets du client : rejoues chez l'hote puis relayes aux autres clients.
    // L'origine est forcee a l'id reel (on ne fait pas confiance a celle du paquet).
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

// Aiguillage d'un paquet recu par l'hote selon son type.
// Tout paquet d'un client connu remet son compteur d'inactivite a zero.
static void HostReceive(int type, Buf *b, NetAddr from)
{
    int id = FindClient(from);
    if (id >= 0) clients[id].silence = 0;
    switch (type)
    {
        case PKT_HELLO: HostHandleHello(b, from); break;
        case PKT_STATE: if (id >= 0) HostHandleState(b, id); break;   // inconnu = ignore
        case PKT_BYE:   if (id >= 0) DropClient(id, "a quitte la partie"); break;
        default: break;
    }
}

// Ecrit un joueur connecte dans le SNAPSHOT (meme ordre que ClientReadPlayer), 41 octets :
//   u8  numero du joueur
//   u8  flags   bit0 connecte, bit1 vivant, bit2 au sol, bit3 rechargement, bit4 sprint,
//               bit5 harpon en main, bit6 harpon charge
//   u8  hp (arrondi au superieur : 0,3 PV ne doit pas devenir 0 = mort) ; i16 kills, headshots
//   u16 ammoGrant, harpoonGrant   compteurs cumules de munitions/harpons (16 bits qui rebouclent)
//   q16 harpoonT ; u8 respawnCount
//   pos (cm) ; q16 vel.x, vel.z (cm/s ; vel.y inutile a l'affichage)
//   q16 yaw, pitch (millieme de radian) ; f32 bob (phase qui grandit sans fin : pas compressee)
//   q16 muzzleT, reloadT, reloadDur (millieme de seconde) ; u8 ads (0..1) ; q16 sprintT
static void PutPlayer(Buf *b, int idx, const Player *p)
{
    int flags = (p->connected ? 1 : 0) | (p->alive ? 2 : 0) | (p->grounded ? 4 : 0) | (p->w.reloading ? 8 : 0) | (p->sprinting ? 16 : 0)
              | (p->w.active == WPN_HARPOON ? 32 : 0) | (p->w.harpoonLoaded ? 64 : 0);
    PutU8(b, idx);
    PutU8(b, flags);
    PutU8(b, (int)Clamp(ceilf(p->hp), 0, 255));
    PutI16(b, p->kills); PutI16(b, p->headshots);
    PutU16(b, p->ammoGrant); PutU16(b, p->harpoonGrant);
    PutQ16(b, p->w.harpoonT, 1000);
    PutU8(b, p->respawnCount);
    PutPos(b, p->pos);
    PutQ16(b, p->vel.x, 100); PutQ16(b, p->vel.z, 100);
    PutQ16(b, WrapAngle(p->yaw), 10000); PutQ16(b, p->pitch, 10000); PutF32(b, p->bob);
    PutQ16(b, p->w.muzzleT, 1000); PutQ16(b, p->w.reloadT, 1000); PutQ16(b, p->w.reloadDur, 1000);
    PutUnit(b, p->w.ads); PutQ16(b, p->w.sprintT, 1000);
}

// Ecrit un mercenaire actif (meme ordre que ClientReadEnemies), 32 octets :
//   u8 idx, type, state, flags (bit1 projete, bit2 en l'air)
//   u8 camo, skin, paint, gear    apparence (tirage aleatoire fait par l'hote)
//   pos (cm) ; q16 yaw ; f32 spin (saltos en vol : peut depasser plusieurs tours)
//   u16 hp ; q16 sz, fat (millieme) ; q16 grabT, deadT (centieme de seconde)
//   u8 hitFlash, attackAnim       valeurs 0..1 sur un octet
static void PutEnemy(Buf *b, int idx, const Enemy *e)
{
    PutU8(b, idx); PutU8(b, e->type); PutU8(b, e->state);
    PutU8(b, (e->launched ? 2 : 0) | (e->airborne ? 4 : 0));
    PutU8(b, e->camo); PutU8(b, e->skin); PutU8(b, e->paint); PutU8(b, e->gear);
    PutPos(b, e->pos); PutQ16(b, WrapAngle(e->yaw), 10000); PutF32(b, e->spin);
    PutU16(b, (int)Clamp(ceilf(e->hp), 0, 65535));
    PutQ16(b, e->sz, 1000); PutQ16(b, e->fat, 1000);
    PutQ16(b, e->grabT, 100); PutQ16(b, e->deadT, 100);
    PutUnit(b, e->hitFlash); PutUnit(b, e->attackAnim);
}

// Construit le SNAPSHOT, le meme pour tous les clients, dans 'data' (PACKET_MAX octets).
// Renvoie sa taille, ou 0 s'il deborde. Contenu :
//   u8 N + N accuses de reception : u8 numero du client, u32 derniere touche appliquee
//   etat de la partie : u8 state, u8 round, i16 wave, toSpawn, alive, u8 flags (bit0 boss attendu,
//                       bit1 entracte), f32 interT, bannerT, i32 score, u8 chicks, chaine overReason
//   u8 N + N joueurs connectes (PutPlayer)
//   u8 N + N mercenaires actifs (PutEnemy)
//   u8 N + N bonus : u8 idx, u8 type, f32 x, f32 z, f32 t
//   u8 N + N messages du fil : u32 id, u8 r, g, b, chaine
//   effets (PutEvents)
// L'etat est complet a chaque fois : un paquet perdu est simplement remplace par le suivant.
static int BuildSnapshot(uint8_t *data)
{
    Buf b;
    BeginPacket(&b, data, PKT_SNAPSHOT);
    // Pour chaque liste : un octet de nombre d'elements, complete apres coup
    // (on reserve l'octet a countPos, on ecrit les elements, puis on y reporte le total)
    int countPos = b.pos, count = 0;
    PutU8(&b, 0);
    for (int i = 1; i < MAX_PLAYERS; i++)
    {
        if (!clients[i].used) continue;
        PutU8(&b, i);
        PutU32(&b, clients[i].lastHit);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    // Partie
    PutU8(&b, G.state); PutU8(&b, G.round);
    PutI16(&b, G.wave); PutI16(&b, G.toSpawn); PutI16(&b, G.alive);
    PutU8(&b, (G.bossPending ? 1 : 0) | (G.inIntermission ? 2 : 0));
    PutF32(&b, G.interT); PutF32(&b, G.bannerT);
    PutI32(&b, G.score); PutU8(&b, G.chicks);
    PutStr(&b, G.overReason);

    countPos = b.pos;
    count = 0;
    PutU8(&b, 0);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!players[i].connected) continue;   // emplacements libres : pas envoyes
        PutPlayer(&b, i, &players[i]);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    countPos = b.pos;
    count = 0;
    PutU8(&b, 0);
    for (int i = 0; i < MAX_ENEMIES; i++)
    {
        if (!enemies[i].active) continue;   // seuls les mercenaires actifs sont envoyes
        PutEnemy(&b, i, &enemies[i]);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    // Bonus au sol (y non transmis : ils sont toujours poses au niveau du sol)
    countPos = b.pos;
    PutU8(&b, 0);
    count = 0;
    for (int i = 0; i < MAX_PICKUPS; i++)
    {
        int type;
        Vector3 pos;
        float t;
        if (!PickupGet(i, &type, &pos, &t)) continue;
        PutU8(&b, i); PutU8(&b, type); PutQ16(&b, pos.x, 100); PutQ16(&b, pos.z, 100); PutQ16(&b, t, 100);
        count++;
    }
    b.data[countPos] = (uint8_t)count;

    // Fil d'actualite : les FEED_RING derniers messages (id 0 = case vide)
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

    PutEvents(&b, MAX_EVENTS);
    // Si le paquet a deborde (err), on prefere ne rien envoyer plutot qu'un paquet tronque
    return b.err ? 0 : b.pos;
}

// Travail de l'hote a chaque fin de frame : detecte les clients muets puis, NET_SNAPSHOT_RATE
// fois par seconde, envoie le SNAPSHOT a tous les clients.
static void HostUpdate(float dt)
{
    for (int i = 1; i < MAX_PLAYERS; i++)
    {
        if (!clients[i].used) continue;
        clients[i].silence += dt;
        if (clients[i].silence > NET_TIMEOUT) DropClient(i, "a perdu la connexion");
    }

#if defined(BR_WEB)
    // Navigateur : la partie s'annonce dans la liste des parties de la page
    // claude.ai (2 fois par seconde ; rien n'est renvoye si rien n'a change)
    static float announceT;
    announceT -= dt;
    if (announceT <= 0)
    {
        announceT = 0.5f;
        SockAnnounce(PlayersConnected(), MAX_PLAYERS, G.wave, G.state != ST_LOBBY);
    }
#endif

    // Cadence fixe : on decompte le temps et on n'envoie que quand il est ecoule
    sendT -= dt;
    if (sendT > 0) return;
    sendT = 1.0f/NET_SNAPSHOT_RATE;
    // static : tampon de 16 Ko reutilise, evite de le poser sur la pile a chaque envoi
    static uint8_t data[PACKET_MAX];
    static NetAddr to[MAX_PLAYERS];
    int n = 0, len = BuildSnapshot(data);
    for (int i = 1; i < MAX_PLAYERS; i++) if (clients[i].used) to[n++] = clients[i].addr;
    if (len > 0 && n > 0) SockSendMany(to, n, data, len);   // navigateur : un seul envoi pour tous
    eventCount = 0;     // effets envoyes a tous : la file est videe
}

//----------------------------------------------------------------------------------
// Client
//----------------------------------------------------------------------------------
// Applique a NOTRE joueur les informations dont l'hote est seul juge (vie, mort, reapparition,
// munitions ramassees). La position et la visee, elles, restent gerees localement.
static void ClientApplyLocalPlayer(Player *p, int flags, float hp, int ammoGrant, int harpoonGrant, int respawnCount, Vector3 pos)
{
    bool alive = flags & 2;
    // Marge de 0.5 : evite de declencher l'effet "blesse" sur de minuscules ecarts
    if (alive && p->alive && hp < p->hp - 0.5f)   // degats subis : retour visuel et sonore
    {
        p->hurtT = 1.0f;
        p->shake = fmaxf(p->shake, 0.3f);
        p->regenDelay = 5.0f;
        PlaySfx(SND_HURT, 1.0f, 0.8f);
    }
    if (p->alive && !alive) p->deadT = 0;      // on vient de mourir : lance l'animation de mort
    p->hp = hp;
    p->alive = alive;

    if (respawnCount != (p->respawnSeen & 0xFF))   // l'hote nous a fait reapparaitre
    {
        // On note cette reapparition ; elle est renvoyee dans STATE pour que l'hote sache
        // que nos positions suivantes sont de nouveau valables (cf. "fresh" dans HostHandleState)
        p->respawnSeen = respawnCount;
        p->pos = pos;
        p->vel = V3(0, 0, 0);
        p->yaw = PI;
        p->pitch = -0.08f;
        PlayerRefillWeapon(p);
    }
    // Les dons de munitions sont des compteurs cumules : on applique la difference avec la
    // derniere valeur vue. Un paquet perdu ne fait donc rien rater (la difference s'accumule)
    // et un paquet repete ne donne rien deux fois. Les compteurs voyagent sur 16 bits :
    // (int16_t)(nouveau - ancien) donne le bon ecart meme quand le compteur reboucle.
    int ammoDiff = (int16_t)(uint16_t)(ammoGrant - p->ammoGrantSeen);
    if (ammoDiff != 0)
    {
        int d = ammoDiff;
        p->ammoGrantSeen += d;
        if (d > 0)
        {
            p->w.reserve = (p->w.reserve + d > MAX_RESERVE) ? MAX_RESERVE : p->w.reserve + d;
            PlaySfx(SND_PICK, 1.0f, 0.6f);
        }
    }
    int harpoonDiff = (int16_t)(uint16_t)(harpoonGrant - p->harpoonGrantSeen);
    if (harpoonDiff != 0)
    {
        int d = harpoonDiff;
        p->harpoonGrantSeen += d;
        if (d > 0) p->w.harpoons = (p->w.harpoons + d > HARPOON_MAX) ? HARPOON_MAX : p->w.harpoons + d;
    }
}

// Lit un joueur du SNAPSHOT (format : voir PutPlayer) ; renvoie son numero (-1 si erreur).
// Pour le joueur local, seules les infos decidees par l'hote sont prises (ClientApplyLocalPlayer) ;
// pour les autres, tout est recopie et sera affiche avec lissage.
static int ClientReadPlayer(Buf *b)
{
    int i = GetU8(b);
    int flags = GetU8(b);
    float hp = (float)GetU8(b);
    int kills = GetI16(b), headshots = GetI16(b);
    int ammoGrant = GetU16(b), harpoonGrant = GetU16(b);
    float harpoonT = GetQ16(b, 1000);
    int respawnCount = GetU8(b);
    Vector3 pos = GetPos(b);
    float vx = GetQ16(b, 100), vz = GetQ16(b, 100);
    float yaw = GetQ16(b, 10000), pitch = GetQ16(b, 10000), bob = GetF32(b);
    float muzzleT = GetQ16(b, 1000), reloadT = GetQ16(b, 1000), reloadDur = GetQ16(b, 1000);
    float ads = GetUnit(b), sprintT = GetQ16(b, 1000);
    if (b->err || i >= MAX_PLAYERS) { b->err = true; return -1; }
    Player *p = &players[i];

    bool connected = flags & 1;
    if (i == localId)
    {
        p->kills = kills;
        p->headshots = headshots;
        ClientApplyLocalPlayer(p, flags, hp, ammoGrant, harpoonGrant, respawnCount, pos);
        return i;
    }
    if (connected && !p->connected) p->pos = pos;   // nouveau joueur : pas de lissage
    p->connected = connected;
    p->local = false;
    p->id = i;
    p->alive = flags & 2;
    p->grounded = flags & 4;
    p->w.reloading = flags & 8;
    p->sprinting = flags & 16;
    p->w.active = (flags & 32) ? WPN_HARPOON : WPN_RIFLE;
    p->w.harpoonLoaded = flags & 64;
    p->w.harpoonT = harpoonT;
    p->hp = hp;
    p->kills = kills;
    p->headshots = headshots;
    p->netPos = pos;                // cible du lissage (p->pos s'en rapproche a l'affichage)
    p->vel = V3(vx, 0, vz);
    p->netYaw = yaw;
    p->netPitch = pitch;
    p->bob = bob;
    // Le flash de bouche n'est pris que s'il est actif : un tir tres bref entre deux snapshots
    // ne doit pas etre efface par une valeur 0 (le flash s'eteint ensuite tout seul localement)
    if (muzzleT > 0) p->w.muzzleT = muzzleT;
    p->w.reloadT = reloadT;
    p->w.reloadDur = reloadDur;
    p->w.ads = ads;
    p->w.sprintT = sprintT;
    return i;
}

// Lit la liste des mercenaires du SNAPSHOT (format : voir PutEnemy). Ceux absents de la
// liste sont desactives (l'hote ne les envoie que s'ils sont actifs).
static void ClientReadEnemies(Buf *b)
{
    bool seen[MAX_ENEMIES] = { 0 };     // seen[i] = le mercenaire i figure dans ce snapshot
    int count = GetU8(b);
    for (int n = 0; n < count && !b->err; n++)
    {
        int idx = GetU8(b), type = GetU8(b), state = GetU8(b), eflags = GetU8(b);
        int camo = GetU8(b), skin = GetU8(b), paint = GetU8(b), gear = GetU8(b);
        Vector3 pos = GetPos(b);
        float yaw = GetQ16(b, 10000), spin = GetF32(b);
        float hp = (float)GetU16(b), sz = GetQ16(b, 1000), fat = GetQ16(b, 1000);
        float grabT = GetQ16(b, 100), deadT = GetQ16(b, 100);
        float hitFlash = GetUnit(b), attackAnim = GetUnit(b);   // octet -> 0..1
        if (b->err || idx >= MAX_ENEMIES) break;

        Enemy *e = &enemies[idx];
        seen[idx] = true;
        // "fresh" = ce n'est plus le meme mercenaire qu'avant dans cette case (nouvelle apparition,
        // autre type, ou ressuscite) : on le place directement sans lissage depuis l'ancien.
        bool fresh = !e->active || e->type != (EnemyType)type || (e->state == ES_DEAD && state != ES_DEAD && e->predictT <= 0);
        if (e->predictT > 0) continue;   // on garde la mort predite (et le vol) en attendant l'hote

        if (state == ES_DEAD && e->state != ES_DEAD) e->deadT = deadT;   // mort annoncee par l'hote
        // Saisie : on ne recale le chrono que s'il vient de commencer ou s'il a trop derive (> 0.3 s)
        if (state == ES_GRAB && (e->state != ES_GRAB || fabsf(e->grabT - grabT) > 0.3f)) e->grabT = grabT;
        e->active = true;
        e->type = (EnemyType)type;
        e->state = (EnemyState)state;
        e->launched = eflags & 2;
        e->airborne = eflags & 4;
        e->netSpin = spin;
        e->camo = (unsigned char)camo; e->skin = (unsigned char)skin;
        e->paint = (unsigned char)paint; e->gear = (unsigned char)gear;
        e->hp = hp; e->sz = sz; e->fat = fat;
        // fmaxf : ne pas couper un flash/une animation deja en cours localement
        e->hitFlash = fmaxf(e->hitFlash, hitFlash);
        e->attackAnim = fmaxf(e->attackAnim, attackAnim);
        e->netPos = pos;            // cible du lissage
        e->netYaw = yaw;
        if (fresh)
        {
            e->pos = e->netPos;
            e->yaw = yaw;
            e->spin = spin;
            e->deadT = deadT;
            e->predictT = 0;
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) if (!seen[i]) enemies[i].active = false;
}

// Lit la liste des bonus (u8 idx, u8 type, q16 x, z au cm, q16 t au centieme) ; les absents sont retires.
static void ClientReadPickups(Buf *b)
{
    bool seen[MAX_PICKUPS] = { 0 };
    int count = GetU8(b);
    for (int n = 0; n < count && !b->err; n++)
    {
        int idx = GetU8(b), type = GetU8(b);
        float x = GetQ16(b, 100), z = GetQ16(b, 100), t = GetQ16(b, 100);
        if (b->err || idx >= MAX_PICKUPS) break;
        seen[idx] = true;
        PickupSet(idx, true, type, V3(x, 0, z), t);
    }
    for (int i = 0; i < MAX_PICKUPS; i++) if (!seen[i]) PickupSet(i, false, 0, V3(0, 0, 0), 0);
}

// Lit les messages du fil d'actualite et affiche seulement ceux pas encore vus (id > lastFeedSeen).
static void ClientReadFeed(Buf *b)
{
    NetFeedMsg msgs[FEED_RING];
    int count = GetU8(b);
    if (count > FEED_RING) { b->err = true; return; }   // impossible sauf paquet corrompu : evite le debordement
    for (int n = 0; n < count; n++)
    {
        msgs[n].id = GetU32(b);
        // Alpha non transmis : toujours opaque (255)
        msgs[n].c = (Color){ (unsigned char)GetU8(b), (unsigned char)GetU8(b), (unsigned char)GetU8(b), 255 };
        GetStr(b, msgs[n].text, sizeof(msgs[n].text));
    }
    if (b->err) return;

    // Les messages arrivent du plus recent au plus ancien : on affiche les nouveaux dans l'ordre
    // (d'ou le parcours a l'envers, du plus ancien au plus recent)
    uint32_t newest = lastFeedSeen;
    for (int n = count - 1; n >= 0; n--)
    {
        if (msgs[n].id <= lastFeedSeen) continue;
        if (!firstSnapshot) FeedPush(msgs[n].c, msgs[n].text);   // pas d'historique a l'arrivee
        if (msgs[n].id > newest) newest = msgs[n].id;
    }
    lastFeedSeen = newest;
}

// Traite un SNAPSHOT complet (format : voir BuildSnapshot). L'ordre de lecture doit etre
// exactement celui de l'ecriture, sinon toutes les valeurs suivantes seraient decalees.
static void ClientHandleSnapshot(Buf *b)
{
    // Table des accuses de reception : on cherche notre ligne. Absente = l'hote ne
    // nous compte pas (encore) parmi ses clients : snapshot ignore.
    int nacks = GetU8(b);
    bool found = false;
    uint32_t ack = 0;
    for (int i = 0; i < nacks && !b->err; i++)
    {
        int id = GetU8(b);
        uint32_t a = GetU32(b);
        if (id == localId) { found = true; ack = a; }
    }
    if (b->err || !found) return;

    // Touches confirmees par l'hote
    // On garde seulement les touches d'id > ack (compactage du tableau sur place) :
    // celles-ci seront renvoyees au prochain STATE.
    int keep = 0;
    for (int i = 0; i < hitCount; i++) if (hits[i].id > ack) hits[keep++] = hits[i];
    hitCount = keep;

    GameState previous = G.state;           // pour detecter un changement d'ecran/phase
    int state = GetU8(b), round = GetU8(b);
    int wave = GetI16(b), toSpawn = GetI16(b), alive = GetI16(b);
    int flags = GetU8(b);
    float interT = GetF32(b), bannerT = GetF32(b);
    int score = GetI32(b), chicks = GetU8(b);
    char reason[96];
    GetStr(b, reason, sizeof(reason));
    if (b->err) return;

    // round n'est transmis que sur 8 bits : comparaison sur l'octet de poids faible
    bool newRound = (round != (G.round & 0xFF));
    G.state = (GameState)state;
    G.round = round;
    G.wave = wave; G.toSpawn = toSpawn; G.alive = alive;
    G.bossPending = flags & 1;
    G.inIntermission = flags & 2;
    G.interT = interT;
    G.bannerT = bannerT;
    G.score = score;
    G.chicks = chicks;
    snprintf(G.overReason, sizeof(G.overReason), "%s", reason);
    if (newRound) GameNetRoundStarted();     // le jeu remet a zero ce qui doit l'etre

    // Joueurs : ceux absents de la liste sont deconnectes (sauf nous-memes)
    bool seen[MAX_PLAYERS] = { 0 };
    int np = GetU8(b);
    for (int n = 0; n < np && !b->err; n++)
    {
        int i = ClientReadPlayer(b);
        if (i >= 0) seen[i] = true;
    }
    if (b->err) return;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!seen[i] && i != localId) { players[i].connected = false; players[i].alive = false; }
    ClientReadEnemies(b);
    ClientReadPickups(b);
    ClientReadFeed(b);

    // Effets : on rejoue ceux des autres (les notres ont deja ete joues localement)
    int ne = GetU8(b);
    for (int i = 0; i < ne && !b->err; i++)
    {
        NetEvent e = GetEvent(b);
        if (!b->err && e.origin != localId) ApplyEvent(&e);
    }
    firstSnapshot = false;
    if (G.state != previous) GameNetStateChanged(previous);
}

// Aiguillage d'un paquet recu par le client. Tout paquet ne venant pas de l'hote est ignore.
static void ClientReceive(int type, Buf *b, NetAddr from)
{
    if (!SockAddrEqual(from, hostAddr)) return;
    silenceT = 0;                           // l'hote est vivant
    switch (type)
    {
        case PKT_WELCOME:
            // Ignore si deja connecte (WELCOME en double, car HELLO est envoye plusieurs fois)
            if (status != NET_CONNECTING) break;
            localId = GetU8(b);
            // Un client ne peut pas etre le joueur 0 (reserve a l'hote)
            if (b->err || localId <= 0 || localId >= MAX_PLAYERS) { localId = -1; break; }
            status = NET_CONNECTED;
            firstSnapshot = true;
            PlayersResetNet(localId);
            break;
        case PKT_REFUSE:
            GetStr(b, error, sizeof(error));   // la raison du refus devient le message d'erreur
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

// Construit et envoie le paquet STATE (format detaille au-dessus de HostHandleState).
// Les touches non confirmees sont toutes recopiees a chaque fois (envoi fiable par repetition).
static void ClientSendState(void)
{
    static uint8_t data[PACKET_MAX];
    const Player *p = &players[localId];
    Buf b;
    BeginPacket(&b, data, PKT_STATE);
    // Ces deux octets permettent a l'hote d'ignorer une position d'avant une nouvelle manche
    // ou d'avant une reapparition (le client n'a peut-etre pas encore recu l'info)
    PutU8(&b, G.round); PutU8(&b, p->respawnSeen);
    PutPos(&b, p->pos); PutPos(&b, p->vel);
    PutQ16(&b, WrapAngle(p->yaw), 10000); PutQ16(&b, p->pitch, 10000); PutF32(&b, p->bob);
    PutU8(&b, (p->grounded ? 1 : 0) | (p->sprinting ? 2 : 0) | (p->w.reloading ? 4 : 0)
              | (p->w.active == WPN_HARPOON ? 8 : 0) | (p->w.harpoonLoaded ? 16 : 0));
    PutQ16(&b, p->w.muzzleT, 1000); PutQ16(&b, p->w.reloadT, 1000); PutQ16(&b, p->w.reloadDur, 1000);
    PutUnit(&b, p->w.ads); PutQ16(&b, p->w.sprintT, 1000); PutQ16(&b, p->w.harpoonT, 1000);
    PutI16(&b, p->w.ammo); PutI16(&b, p->w.reserve); PutI16(&b, p->w.harpoons);

    PutU8(&b, hitCount);
    for (int i = 0; i < hitCount; i++)
    {
        PutU32(&b, hits[i].id); PutU8(&b, hits[i].enemy); PutU8(&b, hits[i].head); PutU8(&b, hits[i].kind); PutF32(&b, hits[i].dmg);
        PutPos(&b, hits[i].pos); PutQ16(&b, hits[i].dir.x, 10000); PutQ16(&b, hits[i].dir.y, 10000); PutQ16(&b, hits[i].dir.z, 10000);
    }
    PutEvents(&b, STATE_EVENTS);
    if (!b.err) SockSend(hostAddr, b.data, b.pos);
    eventCount = 0;     // les effets ne sont envoyes qu'une fois (perte acceptable)
}

// Travail du client a chaque fin de frame : tentative de connexion, detection de perte
// de l'hote, puis envoi de STATE NET_STATE_RATE fois par seconde.
static void ClientUpdate(float dt)
{
    silenceT += dt;
    if (status == NET_CONNECTING)
    {
        // Pas de connexion en UDP : on repete HELLO toutes les 0.5 s tant que l'hote ne repond pas
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
        // Abandon apres 8 secondes sans aucune reponse
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
// A appeler au debut de chaque frame : vide la file de reception de la socket et traite
// chaque paquet (la socket etant non bloquante, la boucle s'arrete des qu'il n'y a plus rien).
void NetReceive(float dt)
{
    (void)dt;                   // parametre inutilise (garde pour la symetrie avec NetSend)
    if (role == NET_OFF) return;
    static uint8_t data[PACKET_MAX];
    NetAddr from;
    int n;
    while ((n = SockRecv(data, sizeof(data), &from)) > 0)
    {
        Buf b = { data, 0, n, false };
        uint32_t magic = GetU32(&b);
        int type = GetU8(&b);
        // Paquet trop court ou etranger au jeu (mauvais "magic") : ignore
        if (b.err || magic != NET_MAGIC) continue;
        if (role == NET_HOST) HostReceive(type, &b, from);
        else ClientReceive(type, &b, from);
        if (role == NET_OFF) break;     // le traitement a pu arreter le reseau (socket fermee)
    }
}

// A appeler a la fin de chaque frame : envoie ce qu'il faut selon le role
void NetSend(float dt)
{
    if (role == NET_HOST) HostUpdate(dt);
    else if (role == NET_CLIENT) ClientUpdate(dt);
}
