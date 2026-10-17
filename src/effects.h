/*
 * effects.h - Particules (impacts, etincelles, plumes), balles tracantes et
 * sons "du monde".
 */
#ifndef EFFECTS_H
#define EFFECTS_H

#include "audio.h"

#include "raylib.h"

void EffectsClear(void);          // supprime toutes les particules et traceuses (nouvelle partie)
void EffectsUpdate(float dt);     // fait avancer la simulation de dt secondes
void EffectsDraw(void);           // dessine (a appeler pendant la passe 3D)

// Cree une particule : position p, vitesse v (m/s), duree de vie (s), taille
// du cube (m), gravite (m/s^2, negative = elle monte, comme de la fumee), couleur.
void SpawnParticle(Vector3 p, Vector3 v, float life, float size, float gravity, Color c);
void BurstImpact(Vector3 p, Vector3 dir, int count);  // impact sur un mercenaire
void BurstSparks(Vector3 p, Vector3 normal);          // impact sur le decor
void BurstFeathers(Vector3 p);                        // un mercenaire enleve un poussin
void AddTracer(Vector3 from, Vector3 to);             // trainee lumineuse d'une balle
void SfxWorld(SndId id, float pitch, float volume);   // son entendu par tous les joueurs

// Ces effets sont aussi transmis aux autres machines en reseau (voir net.h).

#endif
