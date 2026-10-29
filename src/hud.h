/*
 * hud.h - Interface : HUD de chaque joueur, infos communes, fil
 * d'actualite et ecrans (titre, pause, game over).
 *
 * Toutes les fonctions de dessin de ce module travaillent en unites
 * virtuelles : elles doivent etre appelees entre UiBegin() et UiEnd()
 * (voir ui.h), ce qui rend l'interface independante de la resolution.
 */
#ifndef HUD_H
#define HUD_H

#include "player.h"

#include "raylib.h"

// Fil d'actualite : petits messages en haut a droite qui s'estompent
// au bout de quelques secondes (eliminations, bonus, poussins enleves...)
void FeedClear(void);                               // vide le fil (nouvelle partie / retour au menu)
void FeedAdd(Color c, const char *fmt, ...);        // visible par tous (retransmis par l'hote)
void FeedAddLocal(Color c, const char *fmt, ...);   // seulement sur cette machine
void FeedPush(Color c, const char *text);           // message deja formate, non retransmis
void FeedUpdate(float dt);                          // fait vieillir les messages

// HUD personnel, dessine dans la vue (w x h) du joueur.
// showName : affiche "JOUEUR N" au lieu de "SANTE" (ecran partage, reseau).
void HudDrawPlayer(const Player *p, int w, int h, bool showName);
// Infos communes (poussins, vague, score, annonces) sur tout l'ecran.
// split : disposition compacte centree, adaptee a l'ecran partage.
void HudDrawShared(int w, int h, bool split);

// Ecrans (dessines en unites virtuelles, voir ui.h). Les rectangles des
// boutons servent a la fois au dessin et a la detection des clics.
typedef enum { TITLE_SOLO, TITLE_SPLIT, TITLE_HOST, TITLE_JOIN } TitleChoice;   // boutons du menu principal
int HudTitleChoices(TitleChoice *out);   // remplit out (4 cases) avec les choix disponibles, renvoie leur nombre
Rectangle HudTitleButton(int i);    // i-eme bouton du menu principal (dans l'ordre de HudTitleChoices)
Rectangle HudPauseButton(int i);    // 0 reprendre, 1 quitter
Rectangle HudOverButton(int i);     // 0 rejouer, 1 menu
Rectangle HudLobbyButton(int i);    // 0 lancer (hote), 1 retour
bool HudLobbyHasListToggle(void);   // l'hote peut choisir si sa partie est visible dans la liste en ligne
Rectangle HudLobbyListToggle(void); // bouton "VISIBLE DANS LA LISTE : OUI / NON"
Rectangle HudJoinButton(int i);     // 0 se connecter, 1 retour
Rectangle HudJoinKey(int i);        // pave numerique (12 touches)
const char *HudJoinKeyLabel(int i); // texte de la touche i du pave ("0".."9", "." ou "<" pour effacer)

// Liste des parties en ligne (serveur de parties, ou page claude.ai dans le navigateur)
#include "netsock.h"
#define HUD_MAX_GAMES 12
bool HudJoinShowsList(void);        // l'ecran REJOINDRE affiche la liste (sinon : adresse IP seulement)
Rectangle HudJoinGame(int i);       // ligne i de la liste des parties en ligne
int HudJoinGames(SockGame *out);    // parties affichees (out : HUD_MAX_GAMES cases), renvoie leur nombre

void HudDrawTitle(int w, int h);                      // menu principal
void HudDrawPause(int w, int h, bool network);        // pause (ou menu non bloquant en reseau)
void HudDrawGameOver(int w, int h);                   // ecran de fin de partie
void HudDrawJoin(int w, int h, const char *address);  // liste des parties, saisie du code / de l'adresse IP
void HudDrawLobby(int w, int h);                      // salle d'attente reseau

#endif
