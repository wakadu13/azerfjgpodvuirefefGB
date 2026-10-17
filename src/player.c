/*
 * player.c - Joueurs : lecture des commandes, deplacement, vie, camera.
 *
 * Attribution des peripheriques :
 *   Solo / reseau : clavier/souris (ZQSD, WASD ou fleches) + manette 1.
 *   Ecran partage : J1 = clavier/souris (ZQSD/WASD) [+ manette 2 si branchee]
 *                   J2 = manette 1, ou a defaut le pave IJKL + fleches.
 *   Mobile (tactile) : boutons et joysticks virtuels (touch.c) + manette 1.
 *
 * Principe : chaque frame, PlayersReadInput() traduit les peripheriques en
 * une structure PlayerInput neutre ; PlayerUpdate() l'utilise ensuite pour
 * tourner la vue et deplacer le joueur (acceleration, gravite, collisions).
 * Les joueurs distants (reseau) ne lisent pas de commandes : leur position
 * recue est simplement lissee (PlayerUpdateRemote).
 */
#include "player.h"
#include "map.h"
#include "audio.h"
#include "hud.h"
#include "game.h"
#include "net.h"
#include "render.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Tableau global de tous les joueurs (un emplacement par joueur possible)
Player players[MAX_PLAYERS];
int numPlayers = 1;

// Couleur de chaque joueur : J1 bleu, J2 orange, J3 vert, J4 violet, puis
// (grosses parties en ligne) des teintes calculees qui tournent autour du
// cercle des couleurs, avec un pas de 137,5 degres (l'angle d'or) : deux
// numeros proches ont toujours des couleurs bien differentes.
static Color PlayerColor(int id)
{
    static const Color first[4] = {
        { 70, 150, 255, 255 }, { 255, 140, 40, 255 }, { 90, 220, 90, 255 }, { 230, 90, 230, 255 }
    };
    if (id < 4) return first[id];
    return ColorFromHSV(fmodf(id*137.5f, 360.0f), 0.65f, 1.0f);
}

//----------------------------------------------------------------------------------
// Creation / reapparition
//----------------------------------------------------------------------------------
// Point d'apparition du joueur "id" : derriere le stand (z = 9), les joueurs
// alignes cote a cote et centres sur x = 0.
// En reseau (jusqu'a 50 joueurs) : rangees de 13 joueurs espaces de 1,1 m
// (x de -6,6 a +6,6, en partant du milieu), la premiere a z = 9 et les 3
// suivantes tous les 1,2 m
// (jusqu'a z = 12,6 : avant la caisse posee en (-4, 15)).
static Vector3 SpawnPoint(int id)
{
    if (numPlayers == 1) return V3(0, 0, 9);
    if (numPlayers == 2) return V3((id - 0.5f)*3.0f, 0, 9);   // -1,5 et +1,5
    // Places remplies depuis le centre de la rangee : 0, +1, -1, +2, -2...
    int row = id/13, k = id % 13;
    int offset = (k + 1)/2*((k % 2) ? 1 : -1);
    return V3(offset*1.1f, 0, 9 + row*1.2f);
}

// Remet un emplacement a zero (nouvelle partie). Les compteurs reseau et
// l'etat de connexion sont conserves.
static void SetupSlot(Player *p, int id)
{
    Player keep = *p;               // copie de l'ancien etat pour garder certains champs
    memset(p, 0, sizeof(*p));       // tout le reste repart de zero
    p->id = id;
    p->color = PlayerColor(id);
    p->connected = keep.connected;
    p->local = keep.local;
    p->ammoGrant = keep.ammoGrant;
    p->ammoGrantSeen = keep.ammoGrantSeen;
    p->harpoonGrant = keep.harpoonGrant;
    p->harpoonGrantSeen = keep.harpoonGrantSeen;
    p->respawnCount = keep.respawnCount + 1;   // signale au client qu'il doit se replacer
    p->respawnSeen = keep.respawnSeen;
    p->alive = true;
    p->pos = p->netPos = SpawnPoint(id);
    p->yaw = p->netYaw = PI;   // face au stand
    p->pitch = -0.08f;         // regard tres legerement vers le bas
    p->hp = 100;
    p->grounded = true;
    p->cam.fovy = FOV_HIP;     // champ de vision de depart (sans visee)
    WeaponReset(&p->w);
}

// Solo ou ecran partage : "count" joueurs, tous locaux et connectes
void PlayersReset(int count)
{
    numPlayers = count;
    memset(players, 0, sizeof(players));
    for (int i = 0; i < count; i++)
    {
        players[i].connected = true;
        players[i].local = true;
        SetupSlot(&players[i], i);
    }
}

// Partie en reseau : tous les emplacements existent (pour recevoir les autres
// joueurs plus tard) mais seul celui de cette machine est occupe au depart.
void PlayersResetNet(int localId)
{
    numPlayers = MAX_PLAYERS;
    memset(players, 0, sizeof(players));
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        players[i].id = i;                    // les emplacements vides ont quand meme
        players[i].color = PlayerColor(i);    // un id et une couleur valides
    }
    players[localId].connected = true;
    players[localId].local = true;
    SetupSlot(&players[localId], localId);
}

// Relance une partie avec les memes joueurs (seuls les emplacements occupes)
void PlayersRestart(void)
{
    for (int i = 0; i < numPlayers; i++)
        if (players[i].connected) SetupSlot(&players[i], i);
}

// Cote hote : un joueur distant vient de se connecter dans l'emplacement "id"
void PlayerJoin(int id)
{
    Player *p = &players[id];
    p->connected = true;
    p->local = false;
    SetupSlot(p, id);
    FeedAdd(p->color, "%sa rejoint la partie", PlayerTag(p));
}

// Un joueur se deconnecte : l'emplacement est libere (et le joueur n'est plus
// compte comme vivant, pour ne pas bloquer la detection de fin de partie)
void PlayerLeave(int id)
{
    Player *p = &players[id];
    p->connected = false;
    p->alive = false;
}

// Remet les armes en etat : chargeur plein et minimum garanti en reserve
// (90 balles, 3 fleches). Ne retire jamais de munitions si on en a plus.
void PlayerRefillWeapon(Player *p)
{
    p->w.reloading = false;
    p->w.ammo = MAG_SIZE;
    if (p->w.reserve < 90) p->w.reserve = 90;
    p->w.harpoonLoaded = true;
    p->w.harpoonT = 0;
    if (p->w.harpoons < 3) p->w.harpoons = 3;
}

// Reapparition d'un joueur mort (en debut de vague) avec une partie de sa vie
void PlayerRespawn(Player *p)
{
    p->alive = true;
    p->hp = 60;                // pas toute la vie : elle remontera par regeneration
    p->pos = p->netPos = SpawnPoint(p->id);
    p->vel = V3(0, 0, 0);
    p->yaw = p->netYaw = PI;
    p->pitch = -0.08f;
    p->respawnCount++;         // un client distant verra le compteur changer et se replacera
    // Les armes d'un joueur distant sont gerees sur sa propre machine
    if (p->local) PlayerRefillWeapon(p);
    FeedAdd(p->color, "%sest de retour !", PlayerTag(p));
}

// Ajoute des balles de HK416 a la reserve du joueur (caisse de munitions...)
void PlayerGiveAmmo(Player *p, int amount)
{
    // Les munitions d'un joueur distant sont gerees sur sa machine : on lui
    // transmet un cumul qu'il applique lui-meme.
    if (!p->local) { p->ammoGrant += amount; return; }
    // Reserve plafonnee a MAX_RESERVE
    p->w.reserve = (p->w.reserve + amount > MAX_RESERVE) ? MAX_RESERVE : p->w.reserve + amount;
}

// Meme principe que PlayerGiveAmmo, pour les fleches (plafond HARPOON_MAX)
void PlayerGiveHarpoons(Player *p, int amount)
{
    if (!p->local) { p->harpoonGrant += amount; return; }
    p->w.harpoons = (p->w.harpoons + amount > HARPOON_MAX) ? HARPOON_MAX : p->w.harpoons + amount;
}

// Prefixe des messages du fil d'actualite ("J2 est tombe !"). Inutile en solo.
const char *PlayerTag(const Player *p)
{
    static char tags[MAX_PLAYERS][16];  // "J1 " ... "J50 ", ecrits a la demande
    if (numPlayers == 1) return "";
    snprintf(tags[p->id], sizeof(tags[p->id]), "J%d ", p->id + 1);
    return tags[p->id];
}

// Nombre de joueurs connectes encore en vie (0 = partie perdue)
int PlayersAlive(void)
{
    int n = 0;
    for (int i = 0; i < numPlayers; i++) if (players[i].connected && players[i].alive) n++;
    return n;
}

// Nombre d'emplacements occupes
int PlayersConnected(void)
{
    int n = 0;
    for (int i = 0; i < numPlayers; i++) if (players[i].connected) n++;
    return n;
}

// Premier joueur controle sur cette machine (repli sur J1 par securite)
Player *FirstLocalPlayer(void)
{
    for (int i = 0; i < numPlayers; i++) if (players[i].local) return &players[i];
    return &players[0];
}

// Joueur vivant le plus proche de "pos" (sert a l'IA des mercenaires pour
// choisir sa cible). La distance est mesuree a plat (X/Z, sans la hauteur)
// et renvoyee dans *dist si dist n'est pas NULL. Renvoie NULL si personne.
Player *NearestAlivePlayer(Vector3 pos, float *dist)
{
    Player *best = NULL;
    float bd = 1e9f;           // meilleure distance trouvee (tres grande au depart)
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->connected || !p->alive) continue;
        float dx = p->pos.x - pos.x, dz = p->pos.z - pos.z;
        float d = sqrtf(dx*dx + dz*dz);
        if (d < bd) { bd = d; best = p; }
    }
    if (dist) *dist = bd;
    return best;
}

// Les yeux sont a EYE_HEIGHT au-dessus des pieds
Vector3 PlayerEye(const Player *p) { return V3(p->pos.x, p->pos.y + EYE_HEIGHT, p->pos.z); }

// Direction du regard (vecteur unitaire), recul de l'arme compris : c'est
// donc aussi la direction ou partent les balles (avant dispersion).
// Conversion angles -> vecteur (coordonnees spheriques) :
//   yaw = 0 regarde vers +Z, pitch > 0 regarde vers le haut.
Vector3 PlayerForward(const Player *p)
{
    float pitch = p->pitch + p->w.recoilPitch, yaw = p->yaw + p->w.recoilYaw;
    return V3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
}

//----------------------------------------------------------------------------------
// Commandes
//----------------------------------------------------------------------------------
// Valeur d'un axe de stick (-1..1) avec une "zone morte" : les sticks ne
// reviennent jamais exactement a 0, sans elle la vue derivait toute seule.
static float StickAxis(int pad, int axis)
{
    float v = GetGamepadAxisMovement(pad, axis);
    return (fabsf(v) < 0.15f) ? 0.0f : v;
}

// Gachette analogique (axe) ou numerique (bouton) enfoncee ?
static bool TriggerDown(int pad, int axis, int button)
{
    // Selon le pilote, la gachette au repos vaut -1 ou 0 : un seuil a 0.3 marche dans les deux cas
    return GetGamepadAxisMovement(pad, axis) > 0.3f || IsGamepadButtonDown(pad, button);
}

// Clavier + souris. Les touches marchent en AZERTY (ZQSD) comme en QWERTY
// (WASD). arrowsMove = les fleches deplacent aussi le joueur (desactive en
// ecran partage, ou les fleches servent a J2).
// Les "|=" et "+=" permettent de cumuler plusieurs peripheriques.
static void ReadKeyboardMouse(PlayerInput *in, bool arrowsMove)
{
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_Z) || (arrowsMove && IsKeyDown(KEY_UP))) in->move.y += 1;
    if (IsKeyDown(KEY_S) || (arrowsMove && IsKeyDown(KEY_DOWN))) in->move.y -= 1;
    if (IsKeyDown(KEY_D) || (arrowsMove && IsKeyDown(KEY_RIGHT))) in->move.x += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_Q) || (arrowsMove && IsKeyDown(KEY_LEFT))) in->move.x -= 1;

    // Souris : deplacement en pixels depuis la frame precedente -> radians.
    // Signe moins : souris vers la droite = yaw qui diminue (on tourne a droite).
    // Pas de "* dt" ici : le deplacement de la souris est deja un total par frame.
    Vector2 md = GetMouseDelta();
    in->look.x -= md.x*MOUSE_SENS;
    in->look.y -= md.y*MOUSE_SENS;

    in->fire |= IsMouseButtonDown(MOUSE_BUTTON_LEFT);    // maintenu (tir auto)
    in->aim |= IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    in->sprint |= IsKeyDown(KEY_LEFT_SHIFT);
    in->reload |= IsKeyPressed(KEY_R);
    in->jump |= IsKeyPressed(KEY_SPACE);
    in->toggleMode |= IsKeyPressed(KEY_B);
    // Changer d'arme : E ou molette (n'importe quel sens, il n'y a que 2 armes)
    in->switchWeapon |= IsKeyPressed(KEY_E) || GetMouseWheelMove() != 0.0f;
    if (IsKeyPressed(KEY_ONE)) in->selectWeapon = WPN_RIFLE;
    if (IsKeyPressed(KEY_TWO)) in->selectWeapon = WPN_HARPOON;
}

// Manette numero "pad" (0 = premiere branchee). Stick gauche = deplacement,
// stick droit = regard, gachettes = viser / tirer.
static void ReadGamepad(PlayerInput *in, int pad, float dt)
{
    if (!IsGamepadAvailable(pad)) return;   // pas de manette : rien a lire
    // Axe Y du stick : -1 = vers le haut, d'ou le signe moins pour "avancer".
    // Le regard est une vitesse (rad/s) : on multiplie par dt pour obtenir
    // la rotation de cette frame, independamment du nombre d'images/s.
    in->move.x += StickAxis(pad, GAMEPAD_AXIS_LEFT_X);
    in->move.y -= StickAxis(pad, GAMEPAD_AXIS_LEFT_Y);
    in->look.x -= StickAxis(pad, GAMEPAD_AXIS_RIGHT_X)*PAD_LOOK_SPEED*dt;
    in->look.y -= StickAxis(pad, GAMEPAD_AXIS_RIGHT_Y)*PAD_LOOK_SPEED*dt;

    in->fire |= TriggerDown(pad, GAMEPAD_AXIS_RIGHT_TRIGGER, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);
    in->aim |= TriggerDown(pad, GAMEPAD_AXIS_LEFT_TRIGGER, GAMEPAD_BUTTON_LEFT_TRIGGER_2);
    in->sprint |= IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_THUMB);          // clic stick gauche
    in->reload |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);   // X / Carre
    in->jump |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);     // A / Croix
    in->toggleMode |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP); // Y / Triangle
    in->switchWeapon |= IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1); // RB / R1
}

// Deuxieme joueur au clavier, sans souris
// IJKL = deplacement, fleches = regard (vitesse constante KEY_LOOK_SPEED),
// Ctrl droit = tir, Maj droit = visee, H = sprint, U = recharger, O = saut,
// N = mode de tir, M = changer d'arme.
static void ReadSecondKeyboard(PlayerInput *in, float dt)
{
    if (IsKeyDown(KEY_I)) in->move.y += 1;
    if (IsKeyDown(KEY_K)) in->move.y -= 1;
    if (IsKeyDown(KEY_L)) in->move.x += 1;
    if (IsKeyDown(KEY_J)) in->move.x -= 1;
    if (IsKeyDown(KEY_LEFT)) in->look.x += KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_RIGHT)) in->look.x -= KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_UP)) in->look.y += KEY_LOOK_SPEED*dt;
    if (IsKeyDown(KEY_DOWN)) in->look.y -= KEY_LOOK_SPEED*dt;

    in->fire |= IsKeyDown(KEY_RIGHT_CONTROL);
    in->aim |= IsKeyDown(KEY_RIGHT_SHIFT);
    in->sprint |= IsKeyDown(KEY_H);
    in->reload |= IsKeyPressed(KEY_U);
    in->jump |= IsKeyPressed(KEY_O);
    in->toggleMode |= IsKeyPressed(KEY_N);
    in->switchWeapon |= IsKeyPressed(KEY_M);
}

// Remplit p->in pour chaque joueur local, selon le mode de jeu :
//   - tactile (hors ecran partage) : commandes a l'ecran + manette 1
//   - solo / reseau : clavier/souris (fleches incluses) + manette 1
//   - ecran partage, J1 : clavier/souris (sans fleches) + manette 2
//   - ecran partage, J2 : manette 1 + pave IJKL/fleches
void PlayersReadInput(float dt)
{
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->local) continue;      // joueurs distants : commandes lues sur leur PC
        PlayerInput *in = &p->in;
        *in = (PlayerInput){ 0 };     // on repart de zero a chaque frame
        in->selectWeapon = -1;        // aucune arme choisie directement

        if (TouchMode() && G.mode != MODE_SPLIT)
        {
            TouchApplyInput(in);
            ReadGamepad(in, 0, dt);
        }
        else if (G.mode != MODE_SPLIT)
        {
            ReadKeyboardMouse(in, true);
            ReadGamepad(in, 0, dt);
        }
        else if (i == 0)
        {
            ReadKeyboardMouse(in, false);
            ReadGamepad(in, 1, dt);
        }
        else
        {
            ReadGamepad(in, 0, dt);
            ReadSecondKeyboard(in, dt);
        }

        // En diagonale (avant + droite) la longueur vaudrait ~1.41 : on la ramene
        // a 1 pour ne pas aller plus vite en biais.
        if (Vector2Length(in->move) > 1.0f) in->move = Vector2Normalize(in->move);
        // Front montant : appuye maintenant mais pas a la frame precedente
        in->firePressed = in->fire && !p->prevFire;
        p->prevFire = in->fire;
    }
}

//----------------------------------------------------------------------------------
// Mise a jour
//----------------------------------------------------------------------------------
// Inflige "dmg" points de degats au joueur. Declenche les effets (voile rouge,
// tremblement, son), bloque la regeneration 5 s, et gere la mort : fin de
// partie si plus personne n'est debout, sinon simple message.
void PlayerHurt(Player *p, float dmg)
{
    if (!p->alive || G.state != ST_PLAY) return;   // deja mort, ou partie finie / en pause
    p->hp -= dmg;
    p->hurtT = 1.0f;           // voile rouge au maximum
    p->regenDelay = 5.0f;      // la vie ne remonte qu'apres 5 s sans blessure
    p->shake = fmaxf(p->shake, 0.3f);
    if (p->local) PlaySfx(SND_HURT, Randf(0.9f, 1.1f), 0.8f);
    if (p->hp > 0) return;     // encore en vie

    // Mort
    p->hp = 0;
    p->alive = false;
    p->deadT = 0;
    if (PlayersAlive() == 0)
        GameOver(PlayersConnected() == 1 ? "Vous avez ete abattu par les mercenaires..." : "Toute l'escouade est tombee...");
    else
        FeedAdd(RED, "%sest tombe ! Retour a la prochaine vague", PlayerTag(p));
}

// Physique de deplacement d'un joueur local : vitesse voulue d'apres les
// commandes, acceleration, saut, collisions horizontales puis gravite.
static void MovePlayer(Player *p, float dt)
{
    const PlayerInput *in = &p->in;
    // Vecteurs "devant" et "droite" a plat (sans le pitch) : on se deplace au
    // sol meme en regardant vers le haut ou le bas.
    Vector3 fwd = V3(sinf(p->yaw), 0, cosf(p->yaw));
    Vector3 right = V3(-cosf(p->yaw), 0, sinf(p->yaw));

    // Sprint seulement en avancant, et pas en visant, tirant ou rechargeant
    p->sprinting = in->sprint && in->move.y > 0.3f && !in->aim && !in->fire && !p->w.reloading;
    float speed = (p->sprinting ? SPRINT_SPEED : WALK_SPEED)*(1.0f - p->w.ads*0.4f);   // -40% en visant

    // Acceleration progressive (forte au sol, faible en l'air)
    // wish = vitesse voulue. La vitesse actuelle s'en rapproche d'une fraction
    // (dt*accel) a chaque frame : le joueur demarre et freine en douceur, et
    // garde son elan en l'air (faible controle aerien).
    Vector3 wish = Vector3Scale(Vector3Add(Vector3Scale(fwd, in->move.y), Vector3Scale(right, in->move.x)), speed);
    float accel = p->grounded ? 12.0f : 2.5f;
    p->vel.x += (wish.x - p->vel.x)*fminf(1.0f, dt*accel);
    p->vel.z += (wish.z - p->vel.z)*fminf(1.0f, dt*accel);
    // Saut : vitesse verticale initiale, seulement si on touche le sol
    if (p->grounded && in->jump) { p->vel.y = JUMP_SPEED; p->grounded = false; }

    // Horizontal : collisions avec le decor et limites de l'arene
    p->pos.x += p->vel.x*dt;
    p->pos.z += p->vel.z*dt;
    // Le joueur est un cylindre (rayon PLAYER_RADIUS, hauteur 1.8 m) repousse hors des obstacles
    ResolveCircle(&p->pos, PLAYER_RADIUS, p->pos.y, 1.8f);
    p->pos.x = Clamp(p->pos.x, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);
    p->pos.z = Clamp(p->pos.z, -ARENA + PLAYER_RADIUS, ARENA - PLAYER_RADIUS);

    // Vertical : gravite, atterrissage au sol ou sur une caisse
    // ground = hauteur du sol ou du dessus de caisse sous le joueur
    float ground = GroundHeightAt(p->pos, PLAYER_RADIUS, p->pos.y);
    p->vel.y -= GRAVITY*dt;
    p->pos.y += p->vel.y*dt;
    if (p->pos.y <= ground)
    {
        p->pos.y = ground;
        p->vel.y = 0;
        p->grounded = true;
    }
    // Au-dessus du sol (petite marge pour ne pas "decoller" sur une bosse) :
    // on tombe, par exemple en marchant hors d'une caisse
    else if (p->pos.y > ground + 0.05f) p->grounded = false;

    // Phase du balancement de marche : avance avec la vitesse horizontale
    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    if (p->grounded) p->bob += dt*hspeed*1.6f;
}

// Mise a jour d'un joueur local : minuteurs, rotation de la vue, deplacement
// et regeneration de la vie.
void PlayerUpdate(Player *p, float dt)
{
    // Minuteurs d'effets (hitmarker, voile rouge, tremblement)
    p->hitT -= dt;
    p->hurtT = fmaxf(0, p->hurtT - dt*1.5f);
    p->shake = fmaxf(0, p->shake - dt*1.5f);
    if (!p->alive) { p->deadT += dt; return; }

    float sens = 1.0f - p->w.ads*0.35f;   // plus precis en visant
    p->yaw += p->in.look.x*sens;
    // Pitch limite a +/-1.5 rad (~86 deg) : impossible de "passer par-dessus" la verticale
    p->pitch = Clamp(p->pitch + p->in.look.y*sens, -1.5f, 1.5f);

    MovePlayer(p, dt);   // physique : deplacement, saut, collisions

    // La vie est geree par l'hote : un client se contente d'afficher celle recue
    p->regenDelay -= dt;
    if (NetIsAuthority() && p->regenDelay <= 0 && p->hp < 100) p->hp = fminf(100, p->hp + dt*6.0f);
}

// Interpolation entre deux angles par le chemin le plus court : de 350 deg
// a 10 deg on passe par 0 (20 deg), pas par 180 (340 deg). La difference
// est ramenee dans [-PI, PI] avant d'interpoler.
static float LerpAngle(float a, float b, float t)
{
    float d = b - a;
    while (d > PI) d -= 2*PI;
    while (d < -PI) d += 2*PI;
    return a + d*t;
}

// Mise a jour d'un joueur distant : on ne simule pas sa physique, on glisse
// en douceur vers la derniere position/orientation recue du reseau.
void PlayerUpdateRemote(Player *p, float dt)
{
    // Lissage vers la derniere position recue (les paquets arrivent 30 a 60 fois/s)
    float k = fminf(1.0f, dt*15.0f);   // fraction du chemin restant parcourue cette frame
    if (Vector3Distance(p->pos, p->netPos) > 4.0f) p->pos = p->netPos;   // teleportation (reapparition)
    else p->pos = Vector3Lerp(p->pos, p->netPos, k);
    p->yaw = LerpAngle(p->yaw, p->netYaw, k);
    p->pitch += (p->netPitch - p->pitch)*k;
    p->w.muzzleT -= dt;        // flash de bouche de son arme (vu par nous)
    p->hurtT = fmaxf(0, p->hurtT - dt*1.5f);
    if (!p->alive) p->deadT += dt;
    // L'hote gere aussi la regeneration de la vie des joueurs distants
    p->regenDelay -= dt;
    if (NetIsAuthority() && p->alive && p->regenDelay <= 0 && p->hp < 100) p->hp = fminf(100, p->hp + dt*6.0f);
}

// Place la camera 3D du joueur : aux yeux, avec balancement de marche,
// tremblement, recul, et champ de vision (FOV) qui depend de la visee/du sprint.
void PlayerUpdateCamera(Player *p, float dt)
{
    Vector3 eye = PlayerEye(p);
    if (!p->alive)
    {
        // Mort : la camera tombe au sol
        // (en une demi-seconde, jusqu'a 35 cm du sol, la tete penchee sur le cote)
        eye.y = Lerp(eye.y, p->pos.y + 0.35f, fminf(p->deadT*2.0f, 1.0f));
        p->cam.position = eye;
        p->cam.target = Vector3Add(eye, V3(sinf(p->yaw), -0.3f, cosf(p->yaw)));
        p->cam.up = Vector3Normalize(V3(cosf(p->yaw)*0.6f, 1.0f, 0));
        return;
    }
    if (p->grounded)
    {
        // Balancement vertical de la tete a la marche : proportionnel a la vitesse,
        // fortement attenue en visant pour garder la mire stable
        float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
        eye.y += sinf(p->bob*2.0f)*0.03f*fminf(hspeed/WALK_SPEED, 1.4f)*(1.0f - p->w.ads*0.8f);
    }
    // Tremblement : on decale aleatoirement le point vise (pas la position)
    Vector3 shake = V3(Randf(-1, 1), Randf(-1, 1), Randf(-1, 1));
    p->cam.position = eye;
    p->cam.target = Vector3Add(Vector3Add(eye, PlayerForward(p)), Vector3Scale(shake, p->shake*0.05f));
    p->cam.up = V3(0, 1, 0);
    p->cam.projection = CAMERA_PERSPECTIVE;

    // FOV : plus serre en visant (effet de zoom), plus large en sprint (vitesse).
    // La valeur reelle rejoint la cible en douceur pour eviter les sauts.
    float fov = Lerp(FOV_HIP, FOV_ADS, p->w.ads);
    fov = Lerp(fov, FOV_SPRINT, p->w.sprintT);
    p->cam.fovy += (fov - p->cam.fovy)*fminf(1.0f, dt*14.0f);
}

//----------------------------------------------------------------------------------
// Modele du soldat (vu par les autres joueurs)
//----------------------------------------------------------------------------------
// Dessine le soldat d'un autre joueur (coequipier en ecran partage ou en
// reseau). Le modele est construit avec des formes simples (boites, boules,
// cylindres) dans un repere local : origine aux pieds, +Z devant, +Y en haut.
// Box3(repere, centre, rotation, taille, couleur) ; Ball(repere, centre, rayons, couleur) ;
// Limb(repere, debut, fin, rayon, couleur) = membre cylindrique.
// far = joueur lointain : son arme devient une simple silhouette noire.
void PlayerDrawBody(const Player *p, bool far)
{
    const Color uniform = { 92, 98, 66, 255 }, vest = { 125, 115, 82, 255 };
    const Color skin = { 225, 180, 140, 255 }, boots = { 45, 40, 32, 255 };

    // Repere racine : tourne selon le yaw puis place aux pieds du joueur.
    // Mort : le corps est couche (rotation de -90 deg autour de X) au sol.
    Matrix root;
    if (p->alive) root = MatrixMultiply(MatrixRotateY(p->yaw), MatrixTranslate(p->pos.x, p->pos.y, p->pos.z));
    else root = MatrixMultiply(MatrixMultiply(MatrixRotateX(-PI*0.5f), MatrixRotateY(p->yaw)), MatrixTranslate(p->pos.x, 0.2f, p->pos.z));

    DrawBlobShadow(p->pos, 0.4f);   // ombre ronde sous le joueur

    // Jambes (balancement a la marche)
    float hspeed = sqrtf(p->vel.x*p->vel.x + p->vel.z*p->vel.z);
    float swing = p->alive ? sinf(p->bob*2.0f)*0.3f*fminf(hspeed/WALK_SPEED, 1.0f) : 0.0f;
    for (int k = -1; k <= 1; k += 2)   // k = -1 : jambe gauche, k = +1 : jambe droite
    {
        // Les pieds avancent/reculent en opposition (swing*k)
        Vector3 foot = V3(0.13f*k, 0.12f, swing*k);
        Limb(root, V3(0.12f*k, 0.85f, 0), foot, 0.09f, uniform);   // de la hanche au pied
        Box3(root, V3(foot.x, 0.06f, foot.z + 0.05f), NOROT, V3(0.14f, 0.12f, 0.28f), boots);
    }

    // Torse, gilet tactique, tete et casque (bande a la couleur du joueur)
    Box3(root, V3(0, 1.2f, 0), NOROT, V3(0.42f, 0.62f, 0.25f), uniform);                      // torse
    Box3(root, V3(0, 1.22f, 0.02f), NOROT, V3(0.46f, 0.45f, 0.31f), vest);                    // gilet
    Ball(root, V3(0, 1.62f, 0), V3(0.13f, 0.14f, 0.13f), skin);                               // tete
    Ball(root, V3(0, 1.69f, -0.01f), V3(0.16f, 0.11f, 0.17f), (Color){ 75, 80, 55, 255 });  // casque
    Box3(root, V3(0, 1.67f, 0), NOROT, V3(0.33f, 0.035f, 0.35f), p->color);                   // bande coloree
    Box3(root, V3(-0.23f, 1.38f, 0), NOROT, V3(0.03f, 0.08f, 0.14f), p->color);   // brassard

    // HK416 tenu a l'epaule, incline selon le regard
    // Le modele d'arme pointe vers -Z alors que le soldat regarde vers +Z :
    // demi-tour (RotateY(PI)), puis placement a l'epaule droite.
    Matrix gunLocal = MatrixMultiply(MatrixMultiply(MatrixRotateX(p->alive ? p->pitch : 0.0f), MatrixRotateY(PI)),
                                     MatrixTranslate(0.12f, 1.4f, 0.28f));
    if (far) Box3(MatrixMultiply(gunLocal, root), V3(0, 0, -0.2f), NOROT, V3(0.06f, 0.12f, 0.8f), (Color){ 25, 25, 25, 255 });
    else if (p->w.active == WPN_HARPOON) DrawHarpoonGun(MatrixMultiply(gunLocal, root), &p->w);
    else DrawHK416(MatrixMultiply(gunLocal, root), &p->w);

    // Bras vers la poignee et le garde-main
    // On calcule ou se trouvent la poignee et le garde-main dans le repere du
    // corps, puis on tend un bras de chaque epaule vers ces points.
    Vector3 grip = Vector3Transform(V3(0, -0.1f, 0.09f), gunLocal);
    Vector3 fore = Vector3Transform(V3(0, -0.04f, -0.33f), gunLocal);
    Limb(root, V3(0.24f, 1.42f, 0), grip, 0.055f, uniform);
    Limb(root, V3(-0.24f, 1.42f, 0), fore, 0.055f, uniform);
}
