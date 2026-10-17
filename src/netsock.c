#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601
    #endif
#else
    #define _DEFAULT_SOURCE
    #define _DARWIN_C_SOURCE
#endif

#include "netsock.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET sock_t;
    #define BAD_SOCKET INVALID_SOCKET
    #define CloseSock closesocket
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
    typedef int sock_t;
    #define BAD_SOCKET (-1)
    #define CloseSock close
#endif

static sock_t sock = BAD_SOCKET;

#if defined(_WIN32)
static bool StartupWinsock(void)
{
    static bool ready = false;
    if (!ready)
    {
        WSADATA data;
        ready = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }
    return ready;
}
#endif

bool SockOpen(unsigned short port)
{
#if defined(_WIN32)
    if (!StartupWinsock()) return false;
#endif
    SockClose();
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == BAD_SOCKET) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        SockClose();
        return false;
    }

    // Mode non bloquant : on lit tous les paquets disponibles a chaque frame
#if defined(_WIN32)
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);
    // Sinon Windows signale une erreur des qu'un paquet est refuse par l'autre machine
    BOOL reportReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), NULL, 0, &bytes, NULL, NULL);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

void SockClose(void)
{
    if (sock != BAD_SOCKET) CloseSock(sock);
    sock = BAD_SOCKET;
}

bool SockSend(NetAddr to, const void *data, int len)
{
    if (sock == BAD_SOCKET) return false;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to.ip;
    addr.sin_port = to.port;
    return sendto(sock, (const char *)data, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == len;
}

int SockRecv(void *buf, int cap, NetAddr *from)
{
    if (sock == BAD_SOCKET) return 0;
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    int n = (int)recvfrom(sock, (char *)buf, cap, 0, (struct sockaddr *)&addr, &addrLen);
    if (n <= 0) return 0;
    from->ip = addr.sin_addr.s_addr;
    from->port = addr.sin_port;
    return n;
}

bool SockResolve(const char *host, unsigned short port, NetAddr *out)
{
#if defined(_WIN32)
    if (!StartupWinsock()) return false;
#endif
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    out->ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    out->port = htons(port);
    freeaddrinfo(res);
    return true;
}

int SockLocalIPs(char out[][40], int max)
{
    int n = 0;
#if defined(_WIN32)
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
        if ((ntohl(a.s_addr) >> 24) == 127) continue;
        inet_ntop(AF_INET, &a, out[n], 40);
        n++;
    }
    freeaddrinfo(res);
#else
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return 0;
    for (struct ifaddrs *it = list; it && n < max; it = it->ifa_next)
    {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        struct in_addr a = ((struct sockaddr_in *)it->ifa_addr)->sin_addr;
        if ((ntohl(a.s_addr) >> 24) == 127) continue;
        inet_ntop(AF_INET, &a, out[n], 40);
        n++;
    }
    freeifaddrs(list);
#endif
    if (n == 0 && max > 0)
    {
        // Repli : "connecter" une socket UDP vers une adresse externe (aucun
        // paquet n'est envoye) et lire l'adresse locale choisie par le systeme.
        sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == BAD_SOCKET) return 0;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
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

bool SockAddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port; }
