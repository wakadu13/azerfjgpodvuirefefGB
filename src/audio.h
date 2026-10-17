/*
 * audio.h - Effets sonores generes proceduralement (aucun fichier audio).
 */
#ifndef AUDIO_H
#define AUDIO_H

// Identifiants des bruitages (SND_COUNT = nombre de sons, pas un vrai son)
typedef enum {
    SND_SHOT,      // tir du HK416
    SND_CLICK,     // manipulation de l'arme (chargeur, culasse)
    SND_EMPTY,     // "clic" de gachette a vide
    SND_HIT,       // balle qui touche un mercenaire
    SND_CHIRP,     // piaillement d'un poussin
    SND_HURT,      // le joueur est blesse
    SND_KILL,      // "ding" de confirmation d'elimination
    SND_HORN,      // corne de debut de vague
    SND_PICK,      // ramassage d'un bonus
    SND_HARPOON,   // tir de harpon
    SND_THUD,      // choc sourd (corps projete)
    SND_COUNT
} SndId;

void AudioInit(void);    // ouvre le peripherique audio et genere tous les sons
void AudioClose(void);   // libere les sons et ferme le peripherique
// Joue un son. pitch : 1 = normal, 2 = une octave plus aigu (et 2x plus court) ;
// volume : 0 a 1.
void PlaySfx(SndId id, float pitch, float volume);

#endif
