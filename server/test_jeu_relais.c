/*
 * test_jeu_relais.c - Teste la couche reseau du JEU (src/netsock.c + src/online.c,
 * exactement le code des versions PC / mobiles) contre un vrai serveur de parties.
 *
 * Deux processus : le parent heberge une partie, l'enfant la trouve dans la liste,
 * la rejoint par son code et echange des paquets avec lui a travers le serveur.
 *
 * Compilation : cc -std=c99 -Isrc -o test_jeu_relais server/test_jeu_relais.c src/netsock.c src/online.c -lpthread
 * Usage       : ./test_jeu_relais <port du serveur>   (serveur deja lance sur 127.0.0.1)
 */
#define _DEFAULT_SOURCE
#include "netsock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DT 0.01f

static int failures;
static const char *who = "";

static void Check(int ok, const char *what)
{
    printf("  [%s] %s %s\n", who, ok ? "ok   " : "ECHEC", what);
    fflush(stdout);
    if (!ok) failures++;
}

static void Sleep10ms(void) { struct timespec t = { 0, 10*1000*1000 }; nanosleep(&t, NULL); }

// Attend un paquet du jeu (en faisant tourner la couche reseau) ; renvoie sa taille
static int WaitPacket(char *buf, int cap, NetAddr *from, float seconds, int drain)
{
    for (float t = 0; t < seconds; t += DT)
    {
        SockUpdate(DT, 0, drain);
        int n = SockRecv(buf, cap - 1, from);
        if (n > 0) { buf[n] = '\0'; return n; }
        Sleep10ms();
    }
    return 0;
}

static int Host(int toChild, int fromChild)
{
    who = "hote";
    if (!SockOpen((unsigned short)(40000 + getpid() % 20000))) { Check(0, "ouverture de la socket"); return 1; }
    int code = 0;
    for (float t = 0; t < 5 && !code; t += DT) { SockUpdate(DT, 0, 0); char b[64]; NetAddr f; SockRecv(b, sizeof(b), &f); code = SockOnlineCode(); Sleep10ms(); }
    Check(code >= 1000, "partie declaree au serveur, code recu");
    if (!write(toChild, &code, sizeof(code))) return 1;

    char buf[256];
    NetAddr from;
    int n = WaitPacket(buf, sizeof(buf), &from, 8, 0);
    Check(n > 0 && !strcmp(buf, "HELLO") && from.relay == 2, "paquet du client relaye (joueur relaye n. 2)");
    SockSend(from, "WELCOME", 8);

    // Diffusion : une adresse directe bidon + le client relaye (un seul envoi au serveur pour lui)
    NetAddr to[2] = { { 0x0100007Fu, 9, 0 }, from };
    int ack = 0;
    if (read(fromChild, &ack, sizeof(ack)) != sizeof(ack)) return 1;
    SockSendMany(to, 2, "SNAPSHOT", 9);
    SockAnnounce(2, 50, 3, 1);
    for (float t = 0; t < 1.5f; t += DT) { SockUpdate(DT, 0, 0); SockRecv(buf, sizeof(buf), &from); Sleep10ms(); }
    if (!write(toChild, &code, sizeof(code))) return 1;   // l'etat est a jour : l'enfant peut lire la liste

    // Le client s'en va : le serveur previent tout de suite
    NetAddr gone;
    int left = 0;
    for (float t = 0; t < 6 && !left; t += DT)
    {
        SockUpdate(DT, 0, 0);
        SockRecv(buf, sizeof(buf), &from);
        if (SockPeerGone(&gone)) left = gone.relay;
        Sleep10ms();
    }
    Check(left == 2, "depart du client signale (SockPeerGone)");
    SockClose();
    return 0;
}

static int Client(int fromHost, int toHost)
{
    who = "client";
    int code = 0;
    if (read(fromHost, &code, sizeof(code)) != sizeof(code)) return 1;

    // 1. La partie apparait dans la liste (socket ouverte rien que pour la liste)
    SockGame games[16];
    int found = 0;
    for (float t = 0; t < 5 && !found; t += DT)
    {
        SockUpdate(DT, 0.2f, 1);
        int n = SockListGames(games, 16);
        for (int i = 0; i < n; i++) if (games[i].code == code) found = 1;
        Sleep10ms();
    }
    Check(found, "partie trouvee dans la liste mondiale");
    int idx = -1, n = SockListGames(games, 16);
    for (int i = 0; i < n; i++) if (games[i].code == code) idx = i;
    Check(idx >= 0 && !strcmp(games[idx].name, "Partie de Testeur") && games[idx].version == NET_VERSION,
          "nom \"Partie de Testeur\" et version du jeu");

    // 2. Code inconnu : erreur claire
    NetAddr host;
    char other[16];
    snprintf(other, sizeof(other), "%04d", code == 9999 ? 1000 : code + 1);
    SockResolve(other, 27016, &host);
    SockOpen(0);
    int failed = 0;
    for (float t = 0; t < 3 && !failed; t += DT) { SockUpdate(DT, 0, 0); char b[64]; NetAddr f; SockRecv(b, sizeof(b), &f); failed = SockLinkError()[0] != 0; Sleep10ms(); }
    Check(failed, "code inconnu : SockLinkError() explique le probleme");
    printf("         (message : \"%s\")\n", SockLinkError());

    // 3. Rejoindre par le code et parler a l'hote a travers le serveur
    char text[8];
    snprintf(text, sizeof(text), "%04d", code);
    Check(SockResolve(text, 27016, &host) && host.relay == 1, "code a 4 chiffres -> hote relaye");
    SockOpen(0);
    Check(SockLinkKind() == LINK_SERVER, "lien = serveur de parties");
    char buf[256];
    NetAddr from;
    int welcomed = 0;
    for (float t = 0; t < 8 && !welcomed; t += 0.25f)
    {
        SockSend(host, "HELLO", 6);       // comme net.c : repete tant que l'hote ne repond pas
        if (WaitPacket(buf, sizeof(buf), &from, 0.25f, 0) && !strcmp(buf, "WELCOME")) welcomed = 1;
    }
    Check(welcomed && SockAddrEqual(from, host), "reponse de l'hote recue");
    int ack = 1;
    if (!write(toHost, &ack, sizeof(ack))) return 1;
    int got = WaitPacket(buf, sizeof(buf), &from, 4, 0) && !strcmp(buf, "SNAPSHOT");
    Check(got, "diffusion de l'hote recue (SockSendMany)");

    // 4. La liste suit l'etat de la partie (2 joueurs, vague 3)
    if (read(fromHost, &code, sizeof(code)) != sizeof(code)) return 1;
    SockClose();
    int updated = 0;
    for (float t = 0; t < 4 && !updated; t += DT)
    {
        SockUpdate(DT, 0.2f, 1);
        n = SockListGames(games, 16);
        for (int i = 0; i < n; i++) if (games[i].code == code && games[i].players == 2 && games[i].wave == 3 && games[i].playing) updated = 1;
        Sleep10ms();
    }
    Check(updated, "liste a jour : 2 joueurs, vague 3, partie lancee");
    SockUpdate(DT, 0, 1);    // ferme la socket de la liste
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage : %s <port du serveur>\n", argv[0]); return 2; }
    char server[64];
    snprintf(server, sizeof(server), "127.0.0.1:%s", argv[1]);
    setenv("ZCB_SERVEUR", server, 1);
    int p2c[2], c2p[2];
    if (pipe(p2c) || pipe(c2p)) return 2;
    pid_t pid = fork();
    if (pid == 0)
    {
        setenv("ZCB_PSEUDO", "Joueur", 1);
        int r = Client(p2c[0], c2p[1]);
        return r ? r : (failures ? 1 : 0);
    }
    setenv("ZCB_PSEUDO", "Testeur", 1);
    int r = Host(p2c[1], c2p[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    int ok = !r && !failures && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    printf("RESULTAT : %s\n", ok ? "tout est bon" : "ECHEC");
    return ok ? 0 : 1;
}
