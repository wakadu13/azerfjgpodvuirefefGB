/*
 * audio.c - Synthese des bruitages au demarrage.
 *
 * Chaque son est calcule echantillon par echantillon (bruit filtre, sinus,
 * enveloppes exponentielles) puis charge comme Sound raylib. Sans
 * peripherique audio, le jeu tourne simplement en silence.
 *
 * Rappels de synthese sonore utilises ci-dessous :
 *  - sinf(2*PI*f*t) : son pur de frequence f (en Hz) au temps t (en secondes) ;
 *  - si la frequence varie, ex. sinf(2*PI*(f0 - k*t)*t), le son "glisse"
 *    vers le grave (effet de chute, d'impact) ;
 *  - expf(-t*k) : enveloppe qui decroit exponentiellement ; plus k est grand,
 *    plus le son s'eteint vite (k = 70 -> quasi eteint en ~0,05 s) ;
 *  - bruit blanc (nz) : valeurs aleatoires -> "chhh", base des explosions ;
 *  - bruit filtre passe-bas (lp, lp2) : bruit adouci, plus grave et "sourd" ;
 *  - dent de scie : 2*fract(t*f) - 1, son riche et nasillard (corne).
 */
#include "audio.h"
#include "util.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>

#define SHOT_VOICES 8   // tirs superposes (alias du meme son)

static Sound sounds[SND_COUNT];
// Un meme Sound ne peut pas etre joue deux fois en meme temps (il redemarre).
// Pour le tir automatique, on utilise donc 8 "alias" (copies qui partagent
// les memes donnees audio) joues a tour de role.
static Sound shotVoices[SHOT_VOICES];
static int shotVoice;    // prochain alias a utiliser
static bool audioOk;     // faux si pas de carte son : tout devient muet

// Duree (en secondes) de chaque son genere
static float SoundDuration(SndId id)
{
    switch (id)
    {
        case SND_SHOT:  return 0.45f;
        case SND_CLICK: return 0.08f;
        case SND_EMPTY: return 0.05f;
        case SND_HIT:   return 0.2f;
        case SND_CHIRP: return 0.45f;
        case SND_HURT:  return 0.35f;
        case SND_KILL:  return 0.35f;
        case SND_HORN:  return 1.4f;
        case SND_PICK:  return 0.25f;
        case SND_HARPOON: return 0.5f;
        case SND_THUD:  return 0.4f;
        default:        return 0.3f;
    }
}

// Valeur d'un echantillon au temps t. nz = bruit blanc, lp/lp2 = bruit filtre.
// dur = duree totale du son. Le resultat doit rester a peu pres entre -1 et 1
// (il est ecrete ensuite par GenSound).
static float SoundSample(SndId id, float t, float dur, float nz, float lp, float lp2)
{
    switch (id)
    {
        case SND_SHOT:
            // Claquement (bruit blanc tres bref) + souffle (bruit filtre plus long)
            // + grondement grave a 55 Hz pour donner du "corps" a la detonation.
            return nz*expf(-t*70.0f)*0.9f + lp*expf(-t*13.0f)*1.7f + sinf(2*PI*55.0f*t)*expf(-t*10.0f)*0.8f;
        case SND_CLICK:
        {
            // Deux clics metalliques : un premier (bruit + tintement a 2400 Hz),
            // puis un second 35 ms plus tard (le chargeur qui se verrouille).
            float v = nz*expf(-t*400.0f)*0.6f + sinf(2*PI*2400.0f*t)*expf(-t*150.0f)*0.4f;
            if (t > 0.035f) v += nz*expf(-(t - 0.035f)*500.0f)*0.5f;
            return v;
        }
        case SND_EMPTY:
            // Petit "tic" aigu (3000 Hz) et sec
            return sinf(2*PI*3000.0f*t)*expf(-t*300.0f)*0.5f + nz*expf(-t*500.0f)*0.3f;
        case SND_HIT:
            // Impact mat (bruit tres filtre) + note qui chute rapidement depuis 220 Hz
            return lp2*expf(-t*20.0f)*2.2f + sinf(2*PI*(220.0f - 500.0f*t)*t)*expf(-t*18.0f)*0.6f;
        case SND_CHIRP:   // "piou piou piou" affole d'un poussin
        {
            // Le son se repete toutes les 0,15 s : 0,1 s de cri puis 0,05 s de silence
            float tc = fmodf(t, 0.15f);
            if (tc > 0.1f) return 0.0f;
            // La phase est l'integrale de la frequence : f(t) = 3900 - 9000*t,
            // donc la note descend d'environ 3900 Hz a 3000 Hz pendant le cri.
            float phase = 3900.0f*tc - 4500.0f*tc*tc;   // glissando descendant
            // sin(PI*tc/0.1) : enveloppe en arche (monte puis redescend) pour
            // eviter les clics ; le dernier facteur baisse le volume au fil des cris.
            return sinf(2*PI*phase)*sinf(PI*tc/0.1f)*0.4f*(1.0f - 0.3f*t/dur);
        }
        case SND_HURT:
            // Grognement grave qui descend depuis 140 Hz + choc etouffe
            return sinf(2*PI*(140.0f - 120.0f*t)*t)*expf(-t*7.0f)*0.8f + lp2*expf(-t*15.0f)*0.9f;
        case SND_KILL:
            // "Ding" : deux notes en quinte (880 Hz = La, 1320 Hz = Mi) qui s'eteignent
            return (sinf(2*PI*880.0f*t)*0.5f + sinf(2*PI*1320.0f*t)*0.3f)*expf(-t*9.0f);
        case SND_HORN:
        {
            // Corne de brume : dent de scie a 196 Hz (Sol) avec un vibrato
            // (+/- 6 Hz, 5 fois par seconde), plus une seconde dent de scie a la
            // quinte (x1.5) pour un son plus riche.
            float f0 = 196.0f + 6.0f*sinf(2*PI*5.0f*t);
            float saw = 2.0f*fmodf(t*f0, 1.0f) - 1.0f;
            float saw2 = 2.0f*fmodf(t*f0*1.5f, 1.0f) - 1.0f;
            // Enveloppe trapeze : montee en 0,1 s, palier, descente sur les 0,25 dernieres secondes
            float env = fminf(t*10.0f, 1.0f)*fminf((dur - t)*4.0f, 1.0f);
            return (saw*0.22f + saw2*0.14f)*env;
        }
        case SND_PICK:
            // Note qui monte de 600 Hz vers les aigus : son "positif" de bonus
            return sinf(2*PI*(600.0f + 900.0f*t/dur)*t)*expf(-t*6.0f)*0.4f;
        case SND_HARPOON:   // "twang" de l'elastique + souffle de la fleche
            // Corde grave (110 Hz, plus aigue au tout debut) + harmonique a 330 Hz + souffle
            return sinf(2*PI*(110.0f + 40.0f*expf(-t*20.0f))*t)*expf(-t*7.0f)*0.7f
                 + sinf(2*PI*330.0f*t)*expf(-t*14.0f)*0.25f + lp*expf(-t*9.0f)*0.6f;
        case SND_THUD:      // impact sourd
            // Note tres grave (70 Hz) qui chute + bruit etouffe
            return sinf(2*PI*(70.0f - 60.0f*t)*t)*expf(-t*9.0f)*1.0f + lp2*expf(-t*12.0f)*1.2f;
        default:
            return 0.0f;
    }
}

// Genere un son complet en memoire : echantillons 16 bits mono a 44,1 kHz.
static Sound GenSound(SndId id)
{
    const int sampleRate = 44100;   // echantillons par seconde (qualite CD)
    float dur = SoundDuration(id);
    int n = (int)(dur*sampleRate);  // nombre total d'echantillons
    short *data = (short *)MemAlloc(n*sizeof(short));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i/sampleRate;
        float nz = Randf(-1.0f, 1.0f);
        // Filtres passe-bas a un pole : chaque valeur se rapproche du bruit d'une
        // fraction (20 % ou 5 %). Plus la fraction est petite, plus le bruit est
        // lisse et grave (lp2 est plus "sourd" que lp).
        lp += (nz - lp)*0.2f;
        lp2 += (nz - lp2)*0.05f;
        // Ecretage a [-1, 1] puis conversion en entier 16 bits (max 32767, on
        // garde une petite marge avec 32000).
        data[i] = (short)(Clamp(SoundSample(id, t, dur, nz, lp, lp2), -1.0f, 1.0f)*32000.0f);
    }
    // Wave = son brut en memoire ; LoadSoundFromWave l'envoie au moteur audio
    // (qui en fait sa propre copie), on peut donc liberer la Wave ensuite.
    Wave w = { .frameCount = (unsigned int)n, .sampleRate = sampleRate, .sampleSize = 16, .channels = 1, .data = data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// Initialise l'audio. Si aucun peripherique n'est disponible (serveur, CI,
// machine sans carte son), audioOk reste faux et PlaySfx ne fera rien.
void AudioInit(void)
{
    InitAudioDevice();
    audioOk = IsAudioDeviceReady();
    if (!audioOk) return;
    for (int i = 0; i < SND_COUNT; i++) sounds[i] = GenSound((SndId)i);
    audioOk = IsSoundValid(sounds[SND_SHOT]);
    if (audioOk) for (int i = 0; i < SHOT_VOICES; i++) shotVoices[i] = LoadSoundAlias(sounds[SND_SHOT]);
}

// Les alias doivent etre liberes avant le son d'origine dont ils partagent les donnees
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
        // Tir : on prend l'alias suivant (tourniquet) pour que les tirs rapproches se superposent
        s = shotVoices[shotVoice];
        shotVoice = (shotVoice + 1) % SHOT_VOICES;
    }
    if (!IsSoundValid(s)) return;   // pas d'audio : on ignore
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, volume);
    PlaySound(s);
}
