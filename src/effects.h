/*
 * effects.h - Particules (ketchup, etincelles, miettes), balles tracantes et
 * sons "du monde".
 */
#ifndef EFFECTS_H
#define EFFECTS_H

#include "audio.h"

#include "raylib.h"

void EffectsClear(void);
void EffectsUpdate(float dt);
void EffectsDraw(void);

void SpawnParticle(Vector3 p, Vector3 v, float life, float size, float gravity, Color c);
void BurstSauce(Vector3 p, Vector3 dir, int count);   // impact sur un glouton
void BurstSparks(Vector3 p, Vector3 normal);          // impact sur le decor
void BurstCrumbs(Vector3 p);                          // un glouton mange
void AddTracer(Vector3 from, Vector3 to);
void SfxWorld(SndId id, float pitch, float volume);   // son entendu par tous les joueurs

// Ces effets sont aussi transmis aux autres machines en reseau (voir net.h).

#endif
