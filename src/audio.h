/*
 * audio.h - Effets sonores generes procéduralement (aucun fichier audio).
 */
#ifndef AUDIO_H
#define AUDIO_H

typedef enum {
    SND_SHOT, SND_CLICK, SND_EMPTY, SND_HIT, SND_NOM,
    SND_HURT, SND_KILL, SND_HORN, SND_PICK, SND_HARPOON, SND_THUD, SND_COUNT
} SndId;

void AudioInit(void);
void AudioClose(void);
void PlaySfx(SndId id, float pitch, float volume);

#endif
