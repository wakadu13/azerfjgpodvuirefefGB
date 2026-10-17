/*
 * enemy.h - Les mercenaires : apparition, IA (enlever les poussins /
 * attaquer), degats et modele 3D (enemymodel.c).
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "config.h"
#include "player.h"

#include "raylib.h"

typedef enum { E_NORMAL, E_FAST, E_TANK, E_BOSS } EnemyType;
typedef enum { ES_WALK, ES_GRAB, ES_DEAD } EnemyState;   // ES_GRAB : attrape un poussin au comptoir
typedef enum { GEAR_NONE, GEAR_BERET, GEAR_SCARF, GEAR_COUNT } EnemyGear;   // beret glisse sous l'epaulette, foulard

#define CAMO_COUNT   4   // teintes de treillis olive
#define SKIN_COUNT   4
#define PAINT_COUNT  3   // styles de peinture du visage

#define GRAB_TIME    4.0f               // secondes pour enlever un poussin
#define GRAB_HOLD    (GRAB_TIME*0.6f)   // ensuite il le tient en main (le tuer le sauve)

typedef struct {
    bool active;
    EnemyType type;
    EnemyState state;
    Vector3 pos;
    float yaw;
    float hp, maxHp, speed, dmg;
    float sz;      // taille globale
    float fat;     // carrure : augmente a chaque poussin enleve
    float phase, hitFlash, attackCd, attackAnim, grabT, deadT;
    float stuckT, sideSign;
    unsigned char camo, skin, paint, gear;   // apparence (indices, voir enemymodel.c)

    // Projete par le harpon
    bool launched, airborne;
    Vector3 vel;
    float spin, restSpin;      // rotation en vol, puis position couche au sol
    int killer;                // joueur a crediter pour les mercenaires percutes
    unsigned long long bowled; // mercenaires deja percutes pendant ce vol (1 bit chacun)

    // Cote client reseau
    Vector3 netPos;
    float netYaw, netSpin;
    float predictT;   // mort predite localement, en attente de confirmation de l'hote
} Enemy;

extern Enemy enemies[MAX_ENEMIES];

void EnemiesClear(void);
void EnemySpawn(EnemyType type, int wave);
void EnemiesUpdate(float dt);         // IA (hote / solo)
void EnemiesClientUpdate(float dt);   // client reseau : lissage + animation
void EnemiesPushPlayers(bool localOnly);
int EnemiesAlive(void);
int EnemiesHoldingChicks(void);   // poussins en train d'etre emportes (plus sur le comptoir)
bool EnemyHoldsChick(const Enemy *e);

// enemymodel.c
void EnemiesDraw(void);
void EnemyModelsUnload(void);

const char *EnemyName(EnemyType type);
float EnemyRadius(const Enemy *e);

// Ennemi le plus proche touche par le rayon (tete prioritaire), -1 sinon.
int EnemyRaycast(Ray ray, float *dist, bool *head);
void EnemyDamage(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);
// Client reseau : retour visuel immediat, l'hote confirmera les degats.
void EnemyPredictHit(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);
// Harpon : tue en un coup et projette le mercenaire dans la direction du tir.
void EnemyHarpoon(Enemy *e, Vector3 dir, Player *shooter);
void EnemyPredictHarpoon(Enemy *e, Vector3 dir, Player *shooter);

#endif
