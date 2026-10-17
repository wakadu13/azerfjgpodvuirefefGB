/*
 * enemy.h - Les mercenaires : apparition, IA (enlever les poussins /
 * attaquer), degats et modele 3D (enemymodel.c).
 *
 * Repartition du code :
 *   - enemy.c      : logique (apparition, IA, degats, harpon, reseau client)
 *   - enemymodel.c : dessin 3D procedural (aucun fichier de modele externe)
 *
 * Tous les mercenaires vivent dans un tableau fixe `enemies[MAX_ENEMIES]` :
 * pas d'allocation dynamique, une case est "libre" quand active == false.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "config.h"
#include "player.h"

#include "raylib.h"

// Type de mercenaire : determine ses statistiques (voir SetStats dans enemy.c)
//   E_NORMAL : mercenaire de base
//   E_FAST   : "L'Eclaireur", petit et rapide, fonce toujours sur le joueur
//   E_TANK   : "Le Colosse", lent mais tres resistant
//   E_BOSS   : "LE CHEF DE GUERRE", enorme, apparait en fin de vague
typedef enum { E_NORMAL, E_FAST, E_TANK, E_BOSS } EnemyType;

// Etat de la machine a etats de l'IA :
//   ES_WALK : se deplace (vers le comptoir ou vers un joueur)
//   ES_GRAB : penche sur le comptoir, en train d'attraper un poussin
//   ES_DEAD : mort (animation de chute / vol du harpon, puis disparition)
typedef enum { ES_WALK, ES_GRAB, ES_DEAD } EnemyState;   // ES_GRAB : attrape un poussin au comptoir

// Accessoire porte (purement visuel). GEAR_COUNT sert de borne pour le tirage aleatoire.
typedef enum { GEAR_NONE, GEAR_BERET, GEAR_SCARF, GEAR_COUNT } EnemyGear;   // beret glisse sous l'epaulette, foulard

// Nombre de variantes d'apparence (tailles des tableaux de couleurs de enemymodel.c)
#define CAMO_COUNT   4   // teintes de treillis olive
#define SKIN_COUNT   4   // teintes de peau (oreilles, avant-bras, mains)
#define PAINT_COUNT  3   // styles de peinture du visage

// Chronologie d'un enlevement (grabT va de 0 a GRAB_TIME) :
//   0 .. GRAB_HOLD         : il fouille le comptoir (le poussin est encore sur le comptoir)
//   GRAB_HOLD .. GRAB_TIME : il brandit le poussin ; le tuer a ce moment sauve le poussin
//   GRAB_TIME              : le poussin est perdu (StealChick)
#define GRAB_TIME    4.0f               // secondes pour enlever un poussin
#define GRAB_HOLD    (GRAB_TIME*0.6f)   // ensuite il le tient en main (le tuer le sauve)

// Un mercenaire. Positions en metres dans le repere du monde (y = hauteur, sol a y = 0).
typedef struct {
    bool active;          // la case du tableau est utilisee
    EnemyType type;
    EnemyState state;
    Vector3 pos;          // position des pieds (y = 0 sauf quand il vole apres un harpon)
    float yaw;            // orientation autour de l'axe vertical (radians, 0 = regarde vers +z)
    float hp, maxHp, speed, dmg;   // points de vie, vitesse (m/s), degats par coup au corps a corps
    float sz;      // taille globale
    float fat;     // carrure : augmente a chaque poussin enleve
    // phase      : phase de l'animation de marche (avance avec la distance parcourue)
    // hitFlash   : 1 au moment ou il est touche, decroit vers 0 (flash blanc)
    // attackCd   : temps restant avant de pouvoir frapper a nouveau
    // attackAnim : 1 au moment du coup, decroit vers 0 (animation de frappe / bouche ouverte)
    // grabT      : temps passe a enlever le poussin (voir GRAB_TIME / GRAB_HOLD)
    // deadT      : temps depuis la mort (ou depuis l'atterrissage s'il a ete projete)
    float phase, hitFlash, attackCd, attackAnim, grabT, deadT;
    // stuckT   : temps passe bloque contre un obstacle (declenche le contournement)
    // sideSign : +1 / -1, cote choisi pour contourner les obstacles
    float stuckT, sideSign;
    unsigned char camo, skin, paint, gear;   // apparence (indices, voir enemymodel.c)

    // Paroles ("On va croquer les poussins !"), gerees sur chaque machine
    // sans passer par le reseau : talkT = temps restant d'affichage de la
    // bulle, talkCd = attente avant la prochaine phrase.
    float talkT, talkCd;

    // Projete par le harpon
    bool launched, airborne;   // a ete harponne / est encore en l'air
    Vector3 vel;               // vitesse du corps en vol (m/s)
    float spin, restSpin;      // rotation en vol, puis position couche au sol
    int killer;                // joueur a crediter pour les mercenaires percutes
    unsigned long long bowled; // mercenaires deja percutes pendant ce vol (1 bit chacun)

    // Cote client reseau : dernieres valeurs recues de l'hote, vers lesquelles on lisse
    Vector3 netPos;
    float netYaw, netSpin;
    float predictT;   // mort predite localement, en attente de confirmation de l'hote
} Enemy;

// Le tableau de tous les mercenaires (defini dans enemy.c, lu aussi par le reseau et le rendu)
extern Enemy enemies[MAX_ENEMIES];

void EnemiesClear(void);                      // vide le tableau (nouvelle partie)
void EnemySpawn(EnemyType type, int wave);    // fait apparaitre un mercenaire derriere une porte
void EnemiesUpdate(float dt);         // IA (hote / solo)
void EnemiesClientUpdate(float dt);   // client reseau : lissage + animation
void EnemiesPushPlayers(bool localOnly);   // empeche les joueurs de traverser les mercenaires
int EnemiesAlive(void);                    // nombre de mercenaires vivants
int EnemiesHoldingChicks(void);   // poussins en train d'etre emportes (plus sur le comptoir)
bool EnemyHoldsChick(const Enemy *e);      // ce mercenaire tient-il un poussin en main ?

// Paroles des mercenaires : minuteries (a appeler a chaque image, hote comme client)
void EnemiesTalkUpdate(float dt);
#define ENEMY_TAUNT "On va croquer les poussins !"

// enemymodel.c
void EnemiesDraw(void);          // dessine tous les mercenaires actifs
// Bulles de paroles (2D) au-dessus des mercenaires qui parlent, vus depuis cam
// dans une zone de dessin de w x h pixels. A appeler apres EndMode3D.
void EnemiesDrawSpeech(Camera3D cam, int w, int h);
void EnemyModelsUnload(void);    // libere les meshes de chaines mis en cache

const char *EnemyName(EnemyType type);   // nom affiche dans le fil d'evenements
float EnemyRadius(const Enemy *e);       // rayon de collision au sol (depend de la taille et de la carrure)

// Ennemi le plus proche touche par le rayon (tete prioritaire), -1 sinon.
// *dist doit contenir la distance max au depart ; elle est remplacee par la distance du coup.
// *head est mis a true si le coup touche la tete.
int EnemyRaycast(Ray ray, float *dist, bool *head);
// Inflige des degats (hote / solo). hitPos et dir servent aux effets d'impact, shooter est credite.
void EnemyDamage(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);
// Client reseau : retour visuel immediat, l'hote confirmera les degats.
void EnemyPredictHit(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);
// Harpon : tue en un coup et projette le mercenaire dans la direction du tir.
void EnemyHarpoon(Enemy *e, Vector3 dir, Player *shooter);
// Version client reseau du harpon (prediction locale, l'hote confirmera).
void EnemyPredictHarpoon(Enemy *e, Vector3 dir, Player *shooter);

#endif
