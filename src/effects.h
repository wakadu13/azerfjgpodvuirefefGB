/*
 * effects.h - Particules (impacts, etincelles, plumes), balles tracantes et
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
void BurstImpact(Vector3 p, Vector3 dir, int count);  // impact sur un mercenaire
void BurstSparks(Vector3 p, Vector3 normal);          // impact sur le decor
void BurstFeathers(Vector3 p);                        // un mercenaire enleve un poussin
void AddTracer(Vector3 from, Vector3 to);
void SfxWorld(SndId id, float pitch, float volume);   // son entendu par tous les joueurs

// Ces effets sont aussi transmis aux autres machines en reseau (voir net.h).

#endif
