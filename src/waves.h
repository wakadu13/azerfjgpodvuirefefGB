/*
 * waves.h - Enchainement des vagues de mercenaires et pauses entre elles.
 */
#ifndef WAVES_H
#define WAVES_H

int WaveSize(int wave);        // nombre de mercenaires normaux de la vague (selon le nombre de joueurs)
void WavesStart(int wave);     // lance la vague numero "wave" (1 = premiere)
void WavesUpdate(float dt);    // a appeler a chaque frame cote simulation (solo ou hote)

#endif
