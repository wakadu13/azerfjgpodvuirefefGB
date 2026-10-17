/*
 * netsock.c - Implementation de la couche socket UDP (voir netsock.h).
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

#include <stdio.h>
#include <string.h>

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
    #include <sys/socket.h>
    #include <unistd.h>
    typedef int sock_t;                 // sous POSIX une socket est un simple descripteur de fichier
    #define BAD_SOCKET (-1)
    #define CloseSock close
#endif

// L'unique socket UDP du jeu (hote ou client). BAD_SOCKET = fermee.
static sock_t sock = BAD_SOCKET;

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

// Ouvre la socket UDP et l'attache au port demande (0 = port libre choisi par le systeme).
bool SockOpen(unsigned short port)
{
#if defined(_WIN32)
    if (!StartupWinsock()) return false;
#endif
    SockClose();                        // on repart d'une socket neuve
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
        SockClose();
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

// Ferme la socket (appel sans danger meme si elle est deja fermee).
void SockClose(void)
{
    if (sock != BAD_SOCKET) CloseSock(sock);
    sock = BAD_SOCKET;
}

// Envoie un datagramme. ip/port sont deja dans l'ordre reseau, donc recopies tels quels.
bool SockSend(NetAddr to, const void *data, int len)
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

// Lit un datagramme en attente. Retourne sa taille, ou 0 s'il n'y a rien (ou en cas d'erreur :
// en mode non bloquant, "rien a lire" se manifeste justement par une erreur EWOULDBLOCK).
// Si le paquet est plus grand que cap, il est tronque (le reste est perdu).
int SockRecv(void *buf, int cap, NetAddr *from)
{
    if (sock == BAD_SOCKET) return 0;
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    int n = (int)recvfrom(sock, (char *)buf, cap, 0, (struct sockaddr *)&addr, &addrLen);
    if (n <= 0) return 0;
    // On note l'expediteur pour savoir quel joueur a envoye ce paquet (et pouvoir lui repondre)
    from->ip = addr.sin_addr.s_addr;
    from->port = addr.sin_port;
    return n;
}

// Resolution d'un nom (ou d'une IP ecrite en texte) vers une adresse IPv4.
// getaddrinfo accepte les deux formes ; on ne garde que le premier resultat.
bool SockResolve(const char *host, unsigned short port, NetAddr *out)
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
    freeaddrinfo(res);                  // getaddrinfo alloue une liste qu'il faut liberer
    return true;
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
// box peuvent avoir la meme IP publique mais des ports differents).
bool SockAddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port; }

#endif   // !BR_WEB
