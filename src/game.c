/*
 * game.c - Machine a etats (titre / reseau / partie / pause / game over)
 * et rendu.
 *
 * Trois facons de jouer :
 *  - solo : un joueur, plein ecran ;
 *  - ecran partage : deux joueurs locaux, chacun rendu dans sa propre
 *    RenderTexture (moitie gauche / droite) ;
 *  - reseau : un joueur par PC. L'hote simule la partie, les clients
 *    affichent l'etat recu (voir net.c).
 *
 * Chaque frame, main.c appelle GameUpdate(dt) puis GameDraw(). Les deux
 * fonctions aiguillent selon G.state (voir l'enum GameState dans game.h).
 */
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "map.h"
#include "effects.h"
#include "pickup.h"
#include "waves.h"
#include "hud.h"
#include "net.h"
#include "harpoon.h"
#include "touch.h"
#include "ui.h"
#include "render.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Game G;   // l'etat global de la partie (declare extern dans game.h)

static Camera3D titleCam;                // camera qui tourne autour du stand derriere les menus
static RenderTexture2D views[2];         // ecran partage : une image hors ecran par joueur
static int viewW, viewH;                 // taille actuelle de ces images (pour les recreer si la fenetre change)
#if defined(BR_WEB)
static char joinAddress[64] = "";             // code de la partie saisi a l'ecran REJOINDRE (navigateur)
#else
static char joinAddress[64] = "192.168.1.";   // adresse saisie a l'ecran REJOINDRE (pre-remplie : reseau local typique)
#endif
static bool netMenu;   // menu ouvert en reseau (la partie continue)

// Souris capturee pour viser (sans objet sur ecran tactile)
// on = true : curseur cache et bloque au centre, la souris tourne la camera ;
// on = false : curseur libre pour cliquer sur les boutons des menus.
static void CaptureCursor(bool on)
{
    if (TouchMode()) return;
    if (on) DisableCursor();
    else EnableCursor();
}

// Initialisation unique au lancement : construit l'arene et affiche le menu.
void GameInit(void)
{
    MapBuild();
    G.state = ST_TITLE;
    G.chicks = START_CHICKS;   // les poussins sont visibles sur le stand derriere le menu
    // Tests automatiques (emulateurs dans GitHub Actions) : partie solo directe
    if (getenv("BR_AUTOSTART")) GameStart(MODE_SOLO);
}

// Liberation des ressources a la fermeture du jeu.
void GameClose(void)
{
    NetStop();               // ferme les sockets si une partie reseau est en cours
    EnemyModelsUnload();     // modeles 3D des mercenaires
    for (int i = 0; i < 2; i++) if (views[i].id) UnloadRenderTexture(views[i]);   // id 0 = jamais creee
}

// Vide le monde : ennemis, effets, bonus et harpons en vol.
static void ClearWorld(void)
{
    EnemiesClear();
    EffectsClear();
    PickupsClear();
    HarpoonsClear();
}

// Demarre une nouvelle partie (aussi utilise pour "REJOUER").
// En reseau, seul l'hote appelle cette fonction ; les clients le
// detectent via G.round et GameNetRoundStarted().
void GameStart(GameMode mode)
{
    ClearWorld();
    FeedClear();

    // Remise a zero complete de G, en gardant ce qui doit survivre
    // d'une partie a l'autre : meilleur score, horloge et compteur de manches
    Game keep = G;
    memset(&G, 0, sizeof(G));
    G.best = keep.best;
    G.time = keep.time;
    G.round = keep.round + 1;
    G.mode = mode;
    G.chicks = START_CHICKS;
    G.state = ST_PLAY;
    netMenu = false;

    if (mode == MODE_SOLO) PlayersReset(1);
    else if (mode == MODE_SPLIT) PlayersReset(2);
    else PlayersRestart();   // reseau : on garde les joueurs deja connectes
    WavesStart(1);           // premiere vague
    TouchReset();            // oublie les doigts poses pendant le menu
    CaptureCursor(true);
}

// Fin de partie (appelee par exemple quand le dernier poussin est enleve).
// Sans effet si on n'est pas en train de jouer (evite un double game over).
void GameOver(const char *reason)
{
    if (G.state != ST_PLAY) return;
    G.state = ST_OVER;
    snprintf(G.overReason, sizeof(G.overReason), "%s", reason);
    if (G.score > G.best) G.best = G.score;
    CaptureCursor(false);   // libere la souris pour cliquer sur REJOUER / MENU
}

// Retour au menu principal depuis n'importe quel etat. Coupe le reseau.
// message : texte a afficher au menu (ex : raison d'une deconnexion), ou NULL.
static void BackToTitle(const char *message)
{
    NetStop();
    ClearWorld();
    G.state = ST_TITLE;
    G.mode = MODE_SOLO;
    G.titleT = 0;
    G.chicks = START_CHICKS;
    snprintf(G.titleMsg, sizeof(G.titleMsg), "%s", message ? message : "");
    netMenu = false;
    CaptureCursor(false);
}

//----------------------------------------------------------------------------------
// Reseau
//----------------------------------------------------------------------------------
// Choix "HEBERGER" : ouvre le socket d'ecoute et passe en salle d'attente.
// En cas d'echec (port deja pris...), on reste au menu avec le message d'erreur.
static void HostLobby(void)
{
    if (!NetHost()) { snprintf(G.titleMsg, sizeof(G.titleMsg), "%s", NetError()); return; }
    ClearWorld();
    FeedClear();
    G.mode = MODE_NET;
    G.state = ST_LOBBY;
    G.titleMsg[0] = '\0';
    PlayersResetNet(0);   // l'hote occupe toujours l'emplacement 0
}

// previous : etat avant la mise a jour recue de l'hote.
void GameNetStateChanged(GameState previous)
{
    // Cote client : l'etat de la partie vient de changer chez l'hote
    G.mode = MODE_NET;
    CaptureCursor(G.state == ST_PLAY && !netMenu);
    // Le meilleur score est tenu localement par chaque client
    if (G.state == ST_OVER && previous != ST_OVER && G.score > G.best) G.best = G.score;
}

void GameNetRoundStarted(void)
{
    // Cote client : l'hote a (re)lance une partie
    // On efface ce qui est simule localement (effets, harpons) et les
    // morts "predites" qui n'ont plus de sens dans la nouvelle manche
    EffectsClear();
    HarpoonsClear();
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].predictT = 0;
    Player *me = &players[NetLocalId()];
    WeaponReset(&me->w);   // chargeur plein, harpons au maximum
    me->alive = true;
}

//----------------------------------------------------------------------------------
// Mise a jour
//----------------------------------------------------------------------------------
// Vrai si le bouton vient d'etre appuye sur l'une des 4 manettes possibles.
static bool AnyGamepadPressed(int button)
{
    for (int i = 0; i < 4; i++) if (IsGamepadAvailable(i) && IsGamepadButtonPressed(i, button)) return true;
    return false;
}

// Touche "menu / pause" : Echap, P, Retour (Android), START de la manette
// ou bouton pause a l'ecran en mode tactile.
static bool MenuPressed(void)
{
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || IsKeyPressed(KEY_BACK) ||
           AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT) || (TouchMode() && TouchPausePressed());
}

static void UpdateTitleCamera(float dt)
{
    // Camera qui tourne lentement autour du stand
    // (cercle de rayon 16 a 7 de hauteur, regard vers le centre de l'arene)
    G.titleT += dt;
    float a = G.titleT*0.15f;   // vitesse angulaire : 0,15 rad/s
    titleCam.position = V3(sinf(a)*16.0f, 7.0f, cosf(a)*16.0f);
    titleCam.target = V3(0, 2.0f, 0);
    titleCam.up = V3(0, 1, 0);
    titleCam.fovy = 60.0f;
    titleCam.projection = CAMERA_PERSPECTIVE;
}

// Applique le choix fait au menu principal (clic, toucher ou raccourci clavier).
static void ChooseTitle(TitleChoice choice)
{
    switch (choice)
    {
        case TITLE_SOLO:  GameStart(MODE_SOLO); break;
        case TITLE_SPLIT: GameStart(MODE_SPLIT); break;
        case TITLE_HOST:  HostLobby(); break;
        case TITLE_JOIN:  G.state = ST_JOIN; G.titleMsg[0] = '\0'; break;
    }
}

// Menu principal : boutons cliquables, puis raccourcis clavier / manette.
static void UpdateTitle(void)
{
    // Boutons : la liste depend de la plateforme (pas d'ecran partage sur telephone)
    TitleChoice choices[4];
    int n = HudTitleChoices(choices);
    for (int i = 0; i < n; i++) if (UiButtonHit(HudTitleButton(i))) { ChooseTitle(choices[i]); return; }

    // Raccourcis : Entree / 1 / A = solo, 2 / START = ecran partage (START permet au
    // joueur 2 de rejoindre a la manette), 3 = heberger, 4 = rejoindre, Echap = quitter
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) ||
        AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) ChooseTitle(TITLE_SOLO);
    else if (!TouchMode() && (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2) || AnyGamepadPressed(GAMEPAD_BUTTON_MIDDLE_RIGHT))) ChooseTitle(TITLE_SPLIT);
    else if (NetSupported() && (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3))) ChooseTitle(TITLE_HOST);
    else if (NetSupported() && (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4))) ChooseTitle(TITLE_JOIN);
#if !defined(BR_WEB)
    // (dans le navigateur, pas de "quitter" : on ferme simplement l'onglet)
    else if (IsKeyPressed(KEY_ESCAPE)) G.quit = true;
#endif
}

// Ecran REJOINDRE. Une fois connecte, c'est l'hote qui fait passer le
// client en ST_LOBBY (etat recu par le reseau), pas cette fonction.
static void UpdateJoin(void)
{
    // Saisie de l'adresse puis attente de la reponse de l'hote
    // (pendant la tentative de connexion, la saisie est bloquee)
    bool waiting = NetGetRole() == NET_CLIENT && NetGetStatus() != NET_FAILED;
    if (!waiting)
    {
        int len = (int)strlen(joinAddress);
        int cap = (int)sizeof(joinAddress) - 1;   // place pour le '\0' final
        // Clavier physique : tous les caracteres imprimables sauf l'espace
        // (une adresse peut aussi etre un nom d'hote)
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
            if (c > 32 && c < 127 && len < cap) { joinAddress[len++] = (char)c; joinAddress[len] = '\0'; }
        // Retour arriere, avec repetition si la touche reste enfoncee
        bool erase = (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE));
        for (int i = 0; i < 12; i++)   // pave numerique (indispensable sur telephone)
        {
            if (!UiButtonHit(HudJoinKey(i))) continue;
            const char *key = HudJoinKeyLabel(i);
            if (key[0] == '<') erase = true;   // touche "<" = effacer
            else if (len < cap) { joinAddress[len++] = key[0]; joinAddress[len] = '\0'; }
        }
        if (erase && len > 0) joinAddress[len - 1] = '\0';
        // Entree ou bouton SE CONNECTER : lance la connexion (resultat lu plus tard via NetGetStatus)
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || UiButtonHit(HudJoinButton(0))) NetJoin(joinAddress);
    }
    // Retour au menu possible a tout moment (annule aussi une connexion en cours)
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudJoinButton(1))) BackToTitle(NULL);
}

// Salle d'attente : seul l'hote peut lancer la partie ; tout le monde peut partir.
static void UpdateLobby(void)
{
    bool host = NetGetRole() == NET_HOST;
    if (host && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || UiButtonHit(HudLobbyButton(0)))) GameStart(MODE_NET);
    else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudLobbyButton(1))) BackToTitle(NULL);
}

// Fait avancer le monde d'un pas de temps dt pendant la partie.
static void UpdateWorld(float dt)
{
    // 1. Joueurs : entrees, deplacement et arme des joueurs locaux ;
    //    les joueurs distants (reseau) sont seulement interpoles
    PlayersReadInput(dt);
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->connected) continue;
        if (p->local)
        {
            if (netMenu) p->in = (PlayerInput){ 0 };   // menu ouvert : le joueur ne bouge plus
            PlayerUpdate(p, dt);
            WeaponUpdate(p, dt);
        }
        else PlayerUpdateRemote(p, dt);
    }

    // 2. Simulation : seule la machine qui fait autorite (solo, ecran partage
    //    ou hote) decide des ennemis, bonus et vagues. Un client se contente
    //    d'animer ce que l'hote lui envoie, pour que tout le monde voie la meme partie.
    if (NetIsAuthority())
    {
        PickupsUpdate(dt);
        EnemiesUpdate(dt);
        WavesUpdate(dt);
    }
    else
    {
        PickupsAnimate(dt);
        EnemiesClientUpdate(dt);
    }
    // 3. Elements purement visuels / locaux, mis a jour partout
    HarpoonsUpdate(dt);
    EnemiesTalkUpdate(dt);   // les mercenaires parlent (bulles locales a chaque machine)
    EffectsUpdate(dt);
    FeedUpdate(dt);
    // 4. Cameras en dernier, pour suivre la position finale des joueurs
    for (int i = 0; i < numPlayers; i++) if (players[i].local) PlayerUpdateCamera(&players[i], dt);
    G.bannerT -= dt;   // la banniere "VAGUE N" s'estompe
}

// Etat ST_PLAY : gestion du menu / de la pause, puis mise a jour du monde.
static void UpdatePlay(float dt)
{
    if (TouchMode()) TouchUpdate();   // lit les doigts (joystick virtuel, boutons a l'ecran)
    if (G.mode == MODE_NET)
    {
        // En reseau, le menu ne met pas la partie en pause
        // (impossible d'arreter le temps pour les autres joueurs) : c'est un
        // simple calque par-dessus le jeu, et on reste dans l'etat ST_PLAY
        bool toggle = MenuPressed() || (netMenu && UiButtonHit(HudPauseButton(0)));
        if (toggle)
        {
            netMenu = !netMenu;
            TouchReset();
            CaptureCursor(!netMenu);
        }
        if (netMenu && (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A) || UiButtonHit(HudPauseButton(1)))) { BackToTitle(NULL); return; }
    }
    // Solo / ecran partage : vraie pause (le monde n'est plus mis a jour)
    // (Q ou A pour quitter : raylib repere les touches par leur position QWERTY,
    //  accepter les deux fait marcher la touche marquee Q en AZERTY comme en QWERTY)
    else if (MenuPressed()) { G.state = ST_PAUSE; CaptureCursor(false); return; }
#if defined(BR_WEB)
    // Navigateur : Echap libere d'abord la souris (le jeu ne voit pas la
    // touche). Quand la souris vient d'etre liberee, on met en pause.
    else if (!TouchMode())
    {
        static bool locked = false;
        if (IsCursorHidden()) locked = true;
        else if (locked) { locked = false; G.state = ST_PAUSE; CaptureCursor(false); return; }
    }
#endif

    UpdateWorld(dt);
}

// Une frame de logique : reseau en entree, machine a etats, reseau en sortie.
void GameUpdate(float dt)
{
    G.time += dt;
    UiUpdate();       // suivi des doigts (boutons au toucher dans le navigateur)
    NetReceive(dt);   // applique d'abord ce qui est arrive par le reseau

    // Client : connexion perdue ou refusee
    // (pendant ST_JOIN, l'echec est affiche sur l'ecran de connexion)
    if (NetIsClient() && G.state != ST_JOIN && (NetGetStatus() == NET_LOST || NetGetStatus() == NET_FAILED))
    {
        BackToTitle(NetError());
        return;
    }

    switch (G.state)
    {
        // Les trois ecrans de menu ont la camera tournante en fond
        case ST_TITLE:
            UpdateTitleCamera(dt);
            UpdateTitle();
            break;
        case ST_JOIN:
            UpdateTitleCamera(dt);
            UpdateJoin();
            break;
        case ST_LOBBY:
            UpdateTitleCamera(dt);
            UpdateLobby();
            break;
        case ST_PLAY:
            UpdatePlay(dt);
            break;
        case ST_PAUSE:
            // Le monde est fige : on ne regarde que les boutons REPRENDRE / MENU PRINCIPAL
            if (TouchMode()) TouchUpdate();
            if (MenuPressed() || UiButtonHit(HudPauseButton(0))) { G.state = ST_PLAY; TouchReset(); CaptureCursor(true); }
            else if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_A) || UiButtonHit(HudPauseButton(1))) BackToTitle(NULL);
            break;
        case ST_OVER:
            // Decor encore vivant derriere l'ecran de fin
            EffectsUpdate(dt);
            for (int i = 0; i < MAX_ENEMIES; i++)   // les mercenaires continuent de bouger
                if (enemies[i].active && enemies[i].state == ES_WALK) enemies[i].phase += dt*3.0f;
            // Rejouer : reserve a la machine qui fait autorite (un client attend l'hote)
            if (NetIsAuthority() && (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER) ||
                                     AnyGamepadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || UiButtonHit(HudOverButton(0))))
                GameStart(G.mode);
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACK) || UiButtonHit(HudOverButton(1))) BackToTitle(NULL);
            break;
    }

    NetSend(dt);   // envoie l'etat (hote) ou les commandes du joueur (client)
}

//----------------------------------------------------------------------------------
// Rendu
//----------------------------------------------------------------------------------
// Scene 3D vue depuis cam. viewer = joueur dont c'est la vue (pas dessine),
// withWeapon = ajoute ses mains et son arme au premier plan.
// w, h : taille en pixels de la zone de dessin (ecran ou moitie d'ecran).
static void RenderView(Camera3D cam, const Player *viewer, bool withWeapon, int w, int h)
{
    // Hors partie (menus), les joueurs ne sont pas dessines
    bool inGame = (G.state == ST_PLAY || G.state == ST_PAUSE || G.state == ST_OVER);
    DrawSky(cam, w, h);   // ciel de nuit en fond (degrade, etoiles, lune)

    // Le flash de bouche d'un joueur qui tire eclaire la scene
    // (une seule lumiere ponctuelle : le premier tireur trouve ; sinon
    // lumiere placee sous le sol avec une puissance nulle = eteinte)
    Vector3 flashPos = V3(0, -100, 0);
    float flashPower = 0.0f;
    for (int i = 0; i < numPlayers && inGame; i++)
    {
        const Player *p = &players[i];
        if (p->connected && p->alive && p->w.muzzleT > 0)
        {
            flashPos = WeaponMuzzleWorld(p);
            flashPower = 6.0f;
            break;
        }
    }

    BeginMode3D(cam);
        // Parametres du shader pour cette vue : position de l'oeil, soleil, brouillard, flash
        RenderSetFrame(cam.position, RenderSunDir(), 0.022f, flashPos, flashPower);
        MapDraw(G.time, G.chicks - EnemiesHoldingChicks());   // poussins encore sur le comptoir
        PickupsDraw();
        EnemiesDraw();
        HarpoonsDraw();
        // Corps des autres joueurs (on ne dessine pas le sien : la camera est dans sa tete)
        for (int i = 0; i < numPlayers && inGame; i++)
            if (&players[i] != viewer && players[i].connected) PlayerDrawBody(&players[i]);
        EffectsDraw();   // en dernier : particules transparentes par-dessus le reste
    EndMode3D();
    if (inGame) EnemiesDrawSpeech(cam, w, h);   // bulles de paroles, en 2D par-dessus la scene

    // Arme en vue a la premiere personne, dessinee apres la scene pour ne
    // jamais rentrer dans les murs
    if (withWeapon && viewer && viewer->alive) WeaponRenderViewModel(viewer, cam);
}

// Ecrans dessines par-dessus le jeu : pause, menu reseau, game over.
// w, h en unites virtuelles (appele entre UiBegin et UiEnd).
static void DrawOverlays(int w, int h)
{
    if (G.state == ST_PAUSE) HudDrawPause(w, h, false);
    if (G.state == ST_PLAY && netMenu) HudDrawPause(w, h, true);
    if (G.state == ST_OVER) HudDrawGameOver(w, h);
}

// (Re)cree les deux RenderTexture de l'ecran partage si leur taille ne
// correspond plus (premier appel ou fenetre redimensionnee).
static void EnsureViewTargets(int w, int h)
{
    if (w == viewW && h == viewH && views[0].id) return;
    for (int i = 0; i < 2; i++)
    {
        if (views[i].id) UnloadRenderTexture(views[i]);
        views[i] = LoadRenderTexture(w, h);
    }
    viewW = w;
    viewH = h;
}

// Ecran partage : chaque joueur est rendu dans sa propre texture hors
// ecran (avec son HUD), puis les deux textures sont collees cote a cote.
// Passer par des textures evite de jongler avec des viewports et garde
// un rendu identique au plein ecran pour chaque moitie.
// sw, sh : taille de l'ecran en pixels ; inGame : partie en cours ou en pause.
static void DrawSplitScreen(int sw, int sh, bool inGame)
{
    int w = sw/2;              // largeur d'une moitie, en pixels
    float s = UiScale();       // pixels par unite virtuelle
    EnsureViewTargets(w, sh);
    // 1. Rendu de chaque vue dans sa texture
    for (int i = 0; i < 2; i++)
    {
        BeginTextureMode(views[i]);
            ClearBackground(SKY_HORIZON);
            RenderView(players[i].cam, &players[i], inGame, w, sh);
            if (inGame)
            {
                // HUD personnel : la taille de la vue est convertie en unites virtuelles
                UiBegin();
                HudDrawPlayer(&players[i], (int)(w/s), (int)(sh/s), true);
                UiEnd();
            }
        EndTextureMode();
    }

    // 2. Composition finale a l'ecran
    BeginDrawing();
        ClearBackground(BLACK);
        for (int i = 0; i < 2; i++)   // les RenderTexture sont a l'envers en Y
            DrawTextureRec(views[i].texture, (Rectangle){ 0, 0, (float)w, (float)-sh }, (Vector2){ (float)(i*w), 0 }, WHITE);
        DrawRectangle(w - 2, 0, 4, sh, (Color){ 20, 20, 20, 255 });   // trait de separation au milieu
        // HUD commun et ecrans (pause, game over) sur tout l'ecran
        UiBegin();
            if (inGame) HudDrawShared(UiWidth(), UiHeight(), true);
            DrawOverlays(UiWidth(), UiHeight());
        UiEnd();
    EndDrawing();
}

// Une frame de rendu complete, selon l'etat et le mode de jeu.
void GameDraw(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();   // taille reelle en pixels (pour la 3D)
    int vw = UiWidth(), vh = UiHeight();   // interface en unites virtuelles
    bool menuScreen = (G.state == ST_TITLE || G.state == ST_JOIN || G.state == ST_LOBBY);
    // Le HUD n'est affiche qu'en jeu / pause (pas derriere l'ecran de game over)
    bool inGame = (G.state == ST_PLAY || G.state == ST_PAUSE);

    if (!menuScreen && G.mode == MODE_SPLIT)
    {
        DrawSplitScreen(sw, sh, inGame);
        return;
    }

    // Solo, reseau et menus : une seule vue plein ecran
    BeginDrawing();
        ClearBackground(SKY_HORIZON);
        if (menuScreen)
        {
            // Menus : arene vue par la camera tournante, sans arme
            RenderView(titleCam, NULL, false, sw, sh);
            UiBegin();
                if (G.state == ST_TITLE) HudDrawTitle(vw, vh);
                else if (G.state == ST_JOIN) HudDrawJoin(vw, vh, joinAddress);
                else HudDrawLobby(vw, vh);
            UiEnd();
        }
        else
        {
            // Partie : vue du (premier) joueur local
            Player *me = FirstLocalPlayer();
            RenderView(me->cam, me, inGame, sw, sh);
            UiBegin();
                if (inGame)
                {
                    HudDrawPlayer(me, vw, vh, G.mode == MODE_NET);   // nom affiche en reseau
                    HudDrawShared(vw, vh, false);
                    // Commandes tactiles (joystick, boutons) seulement quand on peut jouer
                    if (TouchMode() && G.state == ST_PLAY && !netMenu && me->alive) TouchDraw(me);
                }
                DrawOverlays(vw, vh);
            UiEnd();
        }
    EndDrawing();
}
