/*
 * netsock.c - Implementation de la couche socket UDP (voir netsock.h).
 *
 * La meme socket UDP sert aux joueurs directs (reseau local, port ouvert) et
 * au serveur de parties (liste mondiale + relais, voir online.c) : un paquet
 * qui vient de l'adresse du serveur est un message du serveur de parties.
 *
 * Deux familles d'API selon le systeme :
 *  - Windows : Winsock 2 (WSAStartup obligatoire avant tout appel, closesocket, ioctlsocket...)
 *  - Linux / macOS : sockets BSD/POSIX (close, fcntl, getifaddrs...)
 * Les petites differences sont masquees par les macros sock_t / BAD_SOCKET / CloseSock.
 */

// Version navigateur : remplacee par netsock_web.c (pas de sockets UDP dans un navigateur)
#if !defined(BR_WEB)

// Macros a definir AVANT tout #include systeme, car elles changent ce que les en-tetes exposent.
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN         // allege windows.h (exclut entre autres l'ancien winsock.h)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601     // vise Windows 7 minimum : donne acces a inet_ntop/inet_pton
    #endif
#else
    #define _DEFAULT_SOURCE             // glibc : active getifaddrs, etc. malgre -std=c99
    #define _DARWIN_C_SOURCE            // macOS : idem
#endif

#include "netsock.h"
#include "online.h"
#include "online_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET sock_t;              // sous Windows une socket est un handle (entier non signe)
    #define BAD_SOCKET INVALID_SOCKET
    #define CloseSock closesocket
    // Code de controle pas toujours declare par MinGW : on le definit a la main
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <unistd.h>
    typedef int sock_t;                 // sous POSIX une socket est un simple descripteur de fichier
    #define BAD_SOCKET (-1)
    #define CloseSock close
#endif

// L'unique socket UDP du jeu (hote ou client). BAD_SOCKET = fermee.
static sock_t sock = BAD_SOCKET;
static bool browseSock;        // socket ouverte seulement pour lire la liste des parties
static bool relayClient;       // client d'une partie du serveur de parties (rejointe par son code)
static int pendingCode;        // code donne a SockResolve, utilise par le SockOpen(0) qui suit
static char linkError[96];     // probleme a afficher (code sans serveur...)
static unsigned char raw[ZCB_MSG_MAX + 64];   // datagramme recu, avant tri serveur / direct

#if defined(_WIN32)
// Initialise Winsock (version 2.2) une seule fois pour tout le programme.
// Retourne true si Winsock est utilisable.
static bool StartupWinsock(void)
{
    static bool ready = false;          // static : garde sa valeur d'un appel a l'autre
    if (!ready)
    {
        WSADATA data;
        ready = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }
    return ready;
}
#endif

//----------------------------------------------------------------------------------
// Serveur de parties : adresse (resolue en arriere-plan) et identite
//----------------------------------------------------------------------------------
static char serverHost[128];               // nom du serveur ("" = aucun)
static unsigned short serverPort = ZCB_SERVER_PORT;
static NetAddr serverAddr;                 // adresse resolue
// 0 = a resoudre, 1 = en cours, 2 = connue, 3 = echec (nouvel essai 30 s plus tard)
static int resolveState;
static time_t resolveFailT;

static bool Lookup(const char *host, unsigned short port, NetAddr *out);

// Lu une seule fois : variable d'environnement ZCB_SERVEUR, sinon reglage de compilation
static void InitOnline(void)
{
    static bool done;
    if (done) return;
    done = true;
    const char *env = getenv("ZCB_SERVEUR");
    snprintf(serverHost, sizeof(serverHost), "%s", (env && env[0]) ? env : ONLINE_SERVER);
    char *colon = strchr(serverHost, ':');
    if (colon) { *colon = '\0'; serverPort = (unsigned short)atoi(colon + 1); }
    if (serverPort == 0) serverPort = ZCB_SERVER_PORT;
    int platform =
#if defined(_WIN32)
        ZP_WINDOWS;
#elif defined(BR_IOS)
        ZP_IOS;
#elif defined(__ANDROID__)
        ZP_ANDROID;
#elif defined(__APPLE__)
        ZP_MACOS;
#elif defined(__linux__)
        ZP_LINUX;
#else
        ZP_UNKNOWN;
#endif
    OnlineSetIdentity(platform, getenv("ZCB_PSEUDO"));   // pseudo facultatif : "Partie de <pseudo>"
}

bool SockOnlineConfigured(void) { InitOnline(); return serverHost[0] != '\0'; }

// La resolution DNS peut bloquer plusieurs secondes (pas de reseau...) : elle se fait
// dans un fil d'execution a part pour ne jamais figer le jeu. Le resultat est publie
// avec des operations atomiques (lecture/ecriture vues dans le bon ordre par les deux fils).
#if defined(_WIN32)
static DWORD WINAPI ResolveThread(LPVOID arg)
#else
static void *ResolveThread(void *arg)
#endif
{
    (void)arg;
    NetAddr a = { 0 };
    bool ok = Lookup(serverHost, serverPort, &a);
    if (ok) serverAddr = a;
    __atomic_store_n(&resolveState, ok ? 2 : 3, __ATOMIC_RELEASE);
    return 0;
}

// Vrai si l'adresse du serveur est connue ; sinon lance (ou relance) sa resolution
static bool ServerReady(void)
{
    if (!SockOnlineConfigured()) return false;
    int st = __atomic_load_n(&resolveState, __ATOMIC_ACQUIRE);
    if (st == 2) return true;
    if (st == 3)
    {
        if (resolveFailT == 0) resolveFailT = time(NULL);
        if (time(NULL) - resolveFailT < 30) return false;
        resolveFailT = 0;
        st = 0;
    }
    if (st == 0)
    {
        __atomic_store_n(&resolveState, 1, __ATOMIC_RELEASE);
#if defined(_WIN32)
        HANDLE t = CreateThread(NULL, 0, ResolveThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
        else __atomic_store_n(&resolveState, 3, __ATOMIC_RELEASE);
#else
        pthread_t t;
        if (pthread_create(&t, NULL, ResolveThread, NULL) == 0) pthread_detach(t);
        else __atomic_store_n(&resolveState, 3, __ATOMIC_RELEASE);
#endif
    }
    return false;
}

static bool FromServer(const struct sockaddr_in *a)
{
    return __atomic_load_n(&resolveState, __ATOMIC_ACQUIRE) == 2 &&
           a->sin_addr.s_addr == serverAddr.ip && a->sin_port == serverAddr.port;
}

static bool RawSend(NetAddr to, const void *data, int len)
{
    if (sock == BAD_SOCKET) return false;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to.ip;
    addr.sin_port = to.port;
    // Le cast en (const char *) est exige par la signature Winsock ; sans effet sous POSIX
    return sendto(sock, (const char *)data, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == len;
}

// Transport du module online.c : un datagramme vers le serveur de parties
bool OnlineTransportReady(void) { return sock != BAD_SOCKET && ServerReady(); }
bool OnlineTransportSend(const unsigned char *data, int len)
{
    return OnlineTransportReady() && RawSend(serverAddr, data, len);
}

//----------------------------------------------------------------------------------
// Socket du jeu
//----------------------------------------------------------------------------------
// Ouvre la socket UDP et l'attache au port demande (0 = port libre choisi par le systeme).
static bool OpenUdp(unsigned short port)
{
#if defined(_WIN32)
    if (!StartupWinsock()) return false;
#endif
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);   // IPv4, datagrammes, protocole UDP
    if (sock == BAD_SOCKET) return false;

    // Adresse locale : toutes les interfaces (INADDR_ANY) sur le port voulu.
    // htonl/htons convertissent de l'ordre machine vers l'ordre reseau (big-endian).
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        // Echec typique : le port est deja utilise par un autre programme
        CloseSock(sock);
        sock = BAD_SOCKET;
        return false;
    }

    // Mode non bloquant : on lit tous les paquets disponibles a chaque frame
    // (recvfrom rend la main tout de suite au lieu d'attendre un paquet).
#if defined(_WIN32)
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);
    // Sinon Windows signale une erreur des qu'un paquet est refuse par l'autre machine
    // (message ICMP "port injoignable" transforme en WSAECONNRESET sur le recvfrom suivant).
    BOOL reportReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), NULL, 0, &bytes, NULL, NULL);
#else
    // On lit les drapeaux actuels du descripteur et on y ajoute O_NONBLOCK
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

// Hote (port != 0) : la partie est aussi declaree au serveur de parties.
// Client (port == 0) avec un code de partie : inscription aupres du serveur.
bool SockOpen(unsigned short port)
{
    InitOnline();
    int code = pendingCode;
    pendingCode = 0;
    SockClose();                        // on repart d'une socket neuve
    if (!OpenUdp(port)) return false;
    if (port != 0 && SockOnlineConfigured()) OnlineHost(50);   // 50 joueurs, corrige ensuite par SockAnnounce
    else if (port == 0 && code > 0)
    {
        relayClient = true;
        OnlineJoin(code);
    }
    return true;
}

// Ferme la socket (appel sans danger meme si elle est deja fermee).
void SockClose(void)
{
    OnlineLeave();                      // previent le serveur de parties (tant que la socket est ouverte)
    relayClient = browseSock = false;
    if (sock != BAD_SOCKET) CloseSock(sock);
    sock = BAD_SOCKET;
}

// Envoie un datagramme : directement, ou par le serveur de parties pour un joueur relaye.
bool SockSend(NetAddr to, const void *data, int len)
{
    if (to.relay) return OnlineSendTo(to.relay, data, len);
    return RawSend(to, data, len);
}

// Meme paquet pour plusieurs destinataires : un envoi par adresse directe, et un seul
// pour tous les joueurs relayes (le serveur le diffuse).
bool SockSendMany(const NetAddr *to, int n, const void *data, int len)
{
    bool ok = true, relayed = false;
    for (int i = 0; i < n; i++)
    {
        if (to[i].relay) relayed = true;
        else ok = RawSend(to[i], data, len) && ok;
    }
    if (relayed) ok = OnlineSendAll(data, len) && ok;
    return ok;
}

// Lit un paquet du jeu en attente. Retourne sa taille, ou 0 s'il n'y a rien (ou en cas
// d'erreur : en mode non bloquant, "rien a lire" se manifeste justement par une erreur
// EWOULDBLOCK). Les messages du serveur de parties sont traites au passage par online.c ;
// seuls les paquets du jeu qu'ils transportent sont rendus.
// Si le paquet est plus grand que cap, il est tronque (le reste est perdu).
int SockRecv(void *buf, int cap, NetAddr *from)
{
    while (sock != BAD_SOCKET)
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);
        int n = (int)recvfrom(sock, (char *)raw, sizeof(raw), 0, (struct sockaddr *)&addr, &addrLen);
        if (n <= 0) return 0;
        if (FromServer(&addr))
        {
            unsigned short relay = 0;
            int m = OnlineReceive(raw, n, (unsigned char *)buf, cap, &relay);
            if (m > 0) { *from = (NetAddr){ 0, 0, relay }; return m; }
            continue;                   // message de service (liste, accuse...) : deja traite
        }
        if (relayClient || browseSock) continue;   // ces sockets n'attendent que le serveur
        // On note l'expediteur pour savoir quel joueur a envoye ce paquet (et pouvoir lui repondre)
        *from = (NetAddr){ addr.sin_addr.s_addr, addr.sin_port, 0 };
        if (n > cap) n = cap;
        memcpy(buf, raw, n);
        return n;
    }
    return 0;
}

// A chaque image : liste des parties, repetitions et nouvelles pour le serveur de parties
void SockUpdate(float dt, float listEvery, bool drain)
{
    if (!SockOnlineConfigured()) return;
    // Socket ouverte juste pour la liste quand on n'est dans aucune partie
    if (listEvery > 0 && sock == BAD_SOCKET && OpenUdp(0)) browseSock = true;
    else if (listEvery <= 0 && browseSock) SockClose();
    OnlineTick(dt, sock != BAD_SOCKET ? listEvery : 0);
    if (drain && sock != BAD_SOCKET)
    {
        // Personne d'autre ne lit la socket : on traite les reponses du serveur ici
        static unsigned char pkt[ZCB_MSG_MAX];
        NetAddr from;
        while (SockRecv(pkt, sizeof(pkt), &from) > 0) {}
    }
}

SockLink SockLinkKind(void) { return relayClient ? LINK_SERVER : LINK_DIRECT; }

bool SockPeerGone(NetAddr *who)
{
    unsigned short relay;
    if (!OnlinePopGone(&relay)) return false;
    *who = (NetAddr){ 0, 0, relay };
    return true;
}

const char *SockLinkError(void)
{
    if (relayClient && OnlineGetState() == OL_FAILED) return OnlineError();
    return linkError;
}

int SockListGames(SockGame *out, int max) { return OnlineGames(out, max); }
int SockOnlineTotals(int *players) { return OnlineTotals(players); }
void SockAnnounce(int players, int maxPlayers, int wave, bool playing) { OnlineStatus(players, maxPlayers, wave, playing); }
int SockOnlineCode(void) { return OnlineGetState() == OL_HOSTING ? OnlineCode() : 0; }
void SockSetListed(bool on) { OnlineSetListed(on); }
bool SockListed(void) { return OnlineListed(); }
bool SockAvailable(void) { return true; }

int SockAutoJoinCode(void)
{
    const char *env = getenv("ZCB_REJOINDRE");
    int code = env ? atoi(env) : 0;
    return (code >= 1000 && code <= 9999) ? code : 0;
}

const char *SockOnlineInfo(void)
{
    if (!SockOnlineConfigured()) return "";
    switch (OnlineGetState())
    {
        case OL_HOST_WAIT: return OnlineError()[0] ? OnlineError() : "Connexion au serveur de parties...";
        case OL_HOSTING:
        case OL_HOST_REFUSED: return OnlineError();
        default: return "";
    }
}

// Resolution d'un nom (ou d'une IP ecrite en texte) vers une adresse IPv4.
// getaddrinfo accepte les deux formes ; on ne garde que le premier resultat.
static bool Lookup(const char *host, unsigned short port, NetAddr *out)
{
#if defined(_WIN32)
    if (!StartupWinsock()) return false;
#endif
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4 uniquement (le protocole du jeu ne gere pas IPv6)
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    out->ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    out->port = htons(port);            // le port donne par l'appelant est en ordre machine
    out->relay = 0;
    freeaddrinfo(res);                  // getaddrinfo alloue une liste qu'il faut liberer
    return true;
}

// Adresse tapee par le joueur. 4 chiffres sans point = code d'une partie du serveur
// de parties (getaddrinfo, lui, y verrait l'adresse 0.0.x.y !).
bool SockResolve(const char *host, unsigned short port, NetAddr *out)
{
    linkError[0] = '\0';
    pendingCode = 0;
    bool isCode = strlen(host) == 4;
    for (int i = 0; host[i]; i++) if (host[i] < '0' || host[i] > '9') isCode = false;
    if (isCode)
    {
        if (!SockOnlineConfigured())
        {
            snprintf(linkError, sizeof(linkError), "Pas de serveur de parties : tapez l'adresse IP de l'hote");
            return false;
        }
        pendingCode = atoi(host);
        *out = (NetAddr){ 0, 0, 1 };    // l'hote, relaye par le serveur
        return pendingCode >= 1000;
    }
    return Lookup(host, port, out);
}

// Liste les adresses IPv4 de cette machine (affichees par l'hote pour que les amis puissent
// le rejoindre). Les adresses de bouclage 127.x.x.x sont ignorees : inutiles pour un autre PC.
int SockLocalIPs(char out[][40], int max)
{
    int n = 0;
#if defined(_WIN32)
    // Windows : on resout le nom de la machine, ce qui donne ses adresses IP
    if (!StartupWinsock()) return 0;
    char name[256];
    if (gethostname(name, sizeof(name)) != 0) return 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    if (getaddrinfo(name, NULL, &hints, &res) != 0) return 0;
    for (struct addrinfo *it = res; it && n < max; it = it->ai_next)
    {
        struct in_addr a = ((struct sockaddr_in *)it->ai_addr)->sin_addr;
        // ntohl remet l'adresse en ordre machine ; >> 24 garde le premier octet (le "127" de 127.0.0.1)
        if ((ntohl(a.s_addr) >> 24) == 127) continue;
        inet_ntop(AF_INET, &a, out[n], 40);   // conversion binaire -> texte "a.b.c.d"
        n++;
    }
    freeaddrinfo(res);
#else
    // Linux / macOS : getifaddrs donne la liste chainee de toutes les interfaces reseau
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return 0;
    for (struct ifaddrs *it = list; it && n < max; it = it->ifa_next)
    {
        // Certaines interfaces n'ont pas d'adresse, ou une adresse IPv6 / materielle : on les saute
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        struct in_addr a = ((struct sockaddr_in *)it->ifa_addr)->sin_addr;
        if ((ntohl(a.s_addr) >> 24) == 127) continue;   // bouclage 127.x.x.x
        inet_ntop(AF_INET, &a, out[n], 40);
        n++;
    }
    freeifaddrs(list);
#endif
    if (n == 0 && max > 0)
    {
        // Repli : "connecter" une socket UDP vers une adresse externe (aucun
        // paquet n'est envoye) et lire l'adresse locale choisie par le systeme.
        // 8.8.8.8:53 (DNS public de Google) sert seulement de destination plausible
        // pour que le systeme choisisse l'interface de sortie par defaut.
        sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == BAD_SOCKET) return 0;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        // getsockname renvoie l'adresse locale que le systeme a associee a la socket
        if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            getsockname(s, (struct sockaddr *)&local, &len) == 0 && local.sin_addr.s_addr != 0)
        {
            inet_ntop(AF_INET, &local.sin_addr, out[0], 40);
            n = 1;
        }
        CloseSock(s);
    }
    return n;
}

// Deux adresses sont egales si l'IP et le port correspondent (deux clients derriere la meme
// box peuvent avoir la meme IP publique mais des ports differents), ou si c'est le meme
// joueur relaye par le serveur de parties.
bool SockAddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port && a.relay == b.relay; }

#endif   // !BR_WEB
