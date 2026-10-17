/*
 * enemy.h - Les gloutons : apparition, IA (aller manger / attaquer),
 * degats et modele 3D.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "config.h"
#include "player.h"

#include "raylib.h"

typedef enum { E_NORMAL, E_FAST, E_TANK, E_BOSS } EnemyType;
typedef enum { ES_WALK, ES_EAT, ES_DEAD } EnemyState;

typedef struct {
    bool active;
    EnemyType type;
    EnemyState state;
    Vector3 pos;
    float yaw;
    float hp, maxHp, speed, dmg;
    float sz;      // taille globale
    float fat;     // largeur : augmente a chaque burger mange
    float phase, hitFlash, attackCd, attackAnim, eatT, deadT;
    float stuckT, sideSign;
    unsigned char shirt, skin, pants, cap;   // indices dans les palettes de couleurs
    bool hasCap;

    // Cote client reseau
    Vector3 netPos;
    float netYaw;
    float predictT;   // mort predite localement, en attente de confirmation de l'hote
} Enemy;

extern Enemy enemies[MAX_ENEMIES];

void EnemiesClear(void);
void EnemySpawn(EnemyType type, int wave);
void EnemiesUpdate(float dt);         // IA (hote / solo)
void EnemiesClientUpdate(float dt);   // client reseau : lissage + animation
void EnemiesPushPlayers(bool localOnly);
void EnemiesDraw(void);
int EnemiesAlive(void);

const char *EnemyName(EnemyType type);
float EnemyRadius(const Enemy *e);

// Ennemi le plus proche touche par le rayon (tete prioritaire), -1 sinon.
int EnemyRaycast(Ray ray, float *dist, bool *head);
void EnemyDamage(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);
// Client reseau : retour visuel immediat, l'hote confirmera les degats.
void EnemyPredictHit(Enemy *e, float dmg, bool head, Vector3 hitPos, Vector3 dir, Player *shooter);

#endif
