/*
 * audio.c - Synthese des bruitages au demarrage.
 *
 * Chaque son est calcule echantillon par echantillon (bruit filtre, sinus,
 * enveloppes exponentielles) puis charge comme Sound raylib. Sans
 * peripherique audio, le jeu tourne simplement en silence.
 */
#include "audio.h"
#include "util.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>

#define SHOT_VOICES 8   // tirs superposes (alias du meme son)

static Sound sounds[SND_COUNT];
static Sound shotVoices[SHOT_VOICES];
static int shotVoice;
static bool audioOk;

static float SoundDuration(SndId id)
{
    switch (id)
    {
        case SND_SHOT:  return 0.45f;
        case SND_CLICK: return 0.08f;
        case SND_EMPTY: return 0.05f;
        case SND_HIT:   return 0.2f;
        case SND_NOM:   return 0.5f;
        case SND_HURT:  return 0.35f;
        case SND_KILL:  return 0.35f;
        case SND_HORN:  return 1.4f;
        case SND_PICK:  return 0.25f;
        default:        return 0.3f;
    }
}

// Valeur d'un echantillon au temps t. nz = bruit blanc, lp/lp2 = bruit filtre.
static float SoundSample(SndId id, float t, float dur, float nz, float lp, float lp2)
{
    switch (id)
    {
        case SND_SHOT:
            return nz*expf(-t*70.0f)*0.9f + lp*expf(-t*13.0f)*1.7f + sinf(2*PI*55.0f*t)*expf(-t*10.0f)*0.8f;
        case SND_CLICK:
        {
            float v = nz*expf(-t*400.0f)*0.6f + sinf(2*PI*2400.0f*t)*expf(-t*150.0f)*0.4f;
            if (t > 0.035f) v += nz*expf(-(t - 0.035f)*500.0f)*0.5f;
            return v;
        }
        case SND_EMPTY:
            return sinf(2*PI*3000.0f*t)*expf(-t*300.0f)*0.5f + nz*expf(-t*500.0f)*0.3f;
        case SND_HIT:
            return lp2*expf(-t*20.0f)*2.2f + sinf(2*PI*(220.0f - 500.0f*t)*t)*expf(-t*18.0f)*0.6f;
        case SND_NOM:
        {
            float chew = 0.5f + 0.5f*sinf(2*PI*7.0f*t);
            return (sinf(2*PI*140.0f*t) + 0.5f*sinf(2*PI*283.0f*t))*chew*0.35f*(1.0f - t/dur) + lp2*chew*0.8f;
        }
        case SND_HURT:
            return sinf(2*PI*(140.0f - 120.0f*t)*t)*expf(-t*7.0f)*0.8f + lp2*expf(-t*15.0f)*0.9f;
        case SND_KILL:
            return (sinf(2*PI*880.0f*t)*0.5f + sinf(2*PI*1320.0f*t)*0.3f)*expf(-t*9.0f);
        case SND_HORN:
        {
            float f0 = 196.0f + 6.0f*sinf(2*PI*5.0f*t);
            float saw = 2.0f*fmodf(t*f0, 1.0f) - 1.0f;
            float saw2 = 2.0f*fmodf(t*f0*1.5f, 1.0f) - 1.0f;
            float env = fminf(t*10.0f, 1.0f)*fminf((dur - t)*4.0f, 1.0f);
            return (saw*0.22f + saw2*0.14f)*env;
        }
        case SND_PICK:
            return sinf(2*PI*(600.0f + 900.0f*t/dur)*t)*expf(-t*6.0f)*0.4f;
        default:
            return 0.0f;
    }
}

static Sound GenSound(SndId id)
{
    const int sampleRate = 44100;
    float dur = SoundDuration(id);
    int n = (int)(dur*sampleRate);
    short *data = (short *)MemAlloc(n*sizeof(short));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i/sampleRate;
        float nz = Randf(-1.0f, 1.0f);
        lp += (nz - lp)*0.2f;
        lp2 += (nz - lp2)*0.05f;
        data[i] = (short)(Clamp(SoundSample(id, t, dur, nz, lp, lp2), -1.0f, 1.0f)*32000.0f);
    }
    Wave w = { .frameCount = (unsigned int)n, .sampleRate = sampleRate, .sampleSize = 16, .channels = 1, .data = data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

void AudioInit(void)
{
    InitAudioDevice();
    audioOk = IsAudioDeviceReady();
    if (!audioOk) return;
    for (int i = 0; i < SND_COUNT; i++) sounds[i] = GenSound((SndId)i);
    audioOk = IsSoundValid(sounds[SND_SHOT]);
    if (audioOk) for (int i = 0; i < SHOT_VOICES; i++) shotVoices[i] = LoadSoundAlias(sounds[SND_SHOT]);
}

void AudioClose(void)
{
    if (audioOk)
    {
        for (int i = 0; i < SHOT_VOICES; i++) UnloadSoundAlias(shotVoices[i]);
        for (int i = 0; i < SND_COUNT; i++) UnloadSound(sounds[i]);
    }
    CloseAudioDevice();
}

void PlaySfx(SndId id, float pitch, float volume)
{
    Sound s = sounds[id];
    if (id == SND_SHOT)
    {
        s = shotVoices[shotVoice];
        shotVoice = (shotVoice + 1) % SHOT_VOICES;
    }
    if (!IsSoundValid(s)) return;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, volume);
    PlaySound(s);
}
