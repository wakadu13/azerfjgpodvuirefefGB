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
#include <string.h>

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
//----------------------------------------------------------------------------------
// Voix et musique : filtres resonants (formants)
//----------------------------------------------------------------------------------
// Un "resonateur" est un filtre passe-bande tres selectif (biquad a 2 poles) :
// il ne garde du signal que les frequences proches de f. Une voix humaine se
// reconnait a ses formants (2 ou 3 resonances de la bouche, qui changent
// selon la voyelle) : en filtrant une onde en dent de scie (les cordes
// vocales) par deux resonateurs, on obtient un "a", un "o", un "ou"...
typedef struct { float y1, y2; } Reso;

static float ResoStep(Reso *r, float x, float f, float bw, float sampleRate)
{
    float rad = expf(-PI*bw/sampleRate);                // largeur de bande -> rayon du pole
    float a1 = 2.0f*rad*cosf(2*PI*f/sampleRate), a2 = -rad*rad;
    float y = (1.0f - rad)*x + a1*r->y1 + a2*r->y2;      // y[n] = b0.x + a1.y[n-1] + a2.y[n-2]
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

// Dent de scie entre -1 et 1 pour une phase en tours (0..1 = une periode)
static float Saw(float phase) { return 2.0f*(phase - floorf(phase)) - 1.0f; }

// Voix grognee "On va croquer les poussins !" : 7 syllabes, chacune avec ses
// deux formants de voyelle (F1, F2 en Hz) et un souffle de consonne au debut.
// Ce n'est pas une vraie synthese vocale, plutot un grommellement qui en a
// le rythme et la melodie.
static Sound GenTaunt(void)
{
    const int sampleRate = 22050;
    static const struct { float f1, f2, pitch; bool consonant; } SYL[7] = {
        { 500, 900, 1.00f, false },   // on
        { 750, 1250, 1.05f, true },   // va
        { 450, 850, 1.15f, true },    // cro
        { 550, 1700, 1.25f, true },   // quer
        { 420, 1900, 1.05f, true },   // les
        { 330, 800, 1.10f, true },    // pous
        { 520, 1500, 0.90f, true },   // sins
    };
    const float sylLen = 0.21f, gap = 0.03f, start = 0.05f;
    float dur = start + 7*(sylLen + gap) + 0.15f;
    int n = (int)(dur*sampleRate);
    float *buf = (float *)MemAlloc(n*sizeof(float));
    Reso r1 = { 0 }, r2 = { 0 };
    float phase = 0.0f, peak = 1e-6f, hp = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float t = (float)i/sampleRate;
        int k = (int)((t - start)/(sylLen + gap));
        float local = t - start - k*(sylLen + gap);
        float v = 0.0f;
        if (t >= start && k < 7 && local < sylLen)
        {
            // Hauteur de la voix : grave (95 Hz) avec l'intonation de la syllabe,
            // un tremblement rauque (35 Hz) et la fin de phrase qui retombe
            float f0 = 95.0f*SYL[k].pitch*(1.0f + 0.03f*sinf(2*PI*35.0f*t)) * (k == 6 ? 1.0f - 0.4f*local : 1.0f);
            phase += f0/sampleRate;
            float env = powf(sinf(PI*local/sylLen), 0.6f);
            float src = Saw(phase) + Randf(-0.15f, 0.15f);   // cordes vocales + souffle
            v = (ResoStep(&r1, src, SYL[k].f1, 90.0f, sampleRate) + 0.6f*ResoStep(&r2, src, SYL[k].f2, 130.0f, sampleRate))*env;
            // Consonne : bruit aigu (passe-haut) pendant les 40 premieres ms
            if (SYL[k].consonant && local < 0.04f)
            {
                float nz = Randf(-1.0f, 1.0f);
                v += (nz - hp)*0.35f*(1.0f - local/0.04f);
                hp = nz;
            }
        }
        buf[i] = v;
        peak = fmaxf(peak, fabsf(v));
    }
    // Normalisation : le son le plus fort atteint 85 % du maximum
    short *data = (short *)MemAlloc(n*sizeof(short));
    for (int i = 0; i < n; i++) data[i] = (short)(buf[i]/peak*0.85f*32000.0f);
    MemFree(buf);
    Wave w = { .frameCount = (unsigned int)n, .sampleRate = sampleRate, .sampleSize = 16, .channels = 1, .data = data };
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

//----------------------------------------------------------------------------------
// Musique de fond : marche au tambour et choeur grave
//----------------------------------------------------------------------------------
// Boucle de 8 mesures a 4 temps, generee au demarrage (~21 s) :
//  - caisse claire militaire (coups, accents, roulements) et grosse caisse ;
//  - choeur d'hommes sur "aaah", melodie solennelle en re mineur, doublee a
//    l'octave et a la quinte en dessous, sur un bourdon re-la ;
//  - une composition originale "dans l'esprit" d'un chant de troupe.
// Elle est emballee dans un fichier WAV en memoire et jouee comme Music, que
// raylib sait faire tourner en boucle sans coupure.
#define MUSIC_RATE 22050
#define MUSIC_BPM  92.0f

static Music music;
static unsigned char *musicWav;   // fichier WAV en memoire (doit vivre aussi longtemps que la Music)
static bool musicOk;

// Ajoute (en boucle) un coup de caisse claire de force vel a l'echantillon s0
static void AddSnare(float *buf, int n, int s0, float vel)
{
    float prev = 0.0f;
    for (int i = 0; i < MUSIC_RATE/4; i++)
    {
        float t = (float)i/MUSIC_RATE;
        float nz = Randf(-1.0f, 1.0f);
        float timbre = (nz - prev)*0.55f*expf(-t*22.0f)            // timbre (bruit aigu)
                     + sinf(2*PI*185.0f*t)*0.45f*expf(-t*38.0f);   // peau
        prev = nz;
        buf[(s0 + i) % n] += timbre*vel;
    }
}

// Ajoute un coup de grosse caisse : sinus grave dont la hauteur chute
static void AddKick(float *buf, int n, int s0, float vel)
{
    float ph = 0.0f;
    for (int i = 0; i < MUSIC_RATE/2; i++)
    {
        float t = (float)i/MUSIC_RATE;
        ph += (48.0f + 70.0f*expf(-t*28.0f))/MUSIC_RATE;
        buf[(s0 + i) % n] += sinf(2*PI*ph)*expf(-t*6.0f)*vel;
    }
}

// Ajoute une note chantee (choeur : 3 voix legerement desaccordees) de
// hauteur midi, de start a start+len secondes, avec attaque et relachement
static void AddVoice(float *buf, int n, float start, float len, int midi, float gain)
{
    float f = 440.0f*powf(2.0f, (midi - 69)/12.0f);
    static const float DETUNE[3] = { 1.0f, 1.005f, 0.995f };
    float ph[3] = { 0.0f, 0.33f, 0.66f };
    int s0 = (int)(start*MUSIC_RATE), total = (int)((len + 0.3f)*MUSIC_RATE);
    for (int i = 0; i < total; i++)
    {
        float t = (float)i/MUSIC_RATE;
        float env = fminf(t/0.12f, 1.0f)*(t > len ? expf(-(t - len)*12.0f) : 1.0f);
        float vib = 1.0f + 0.004f*sinf(2*PI*5.0f*t);   // vibrato
        float v = 0.0f;
        for (int k = 0; k < 3; k++) { ph[k] += f*DETUNE[k]*vib/MUSIC_RATE; v += Saw(ph[k]); }
        buf[(s0 + i) % n] += v*env*gain;
    }
}

static void MusicInit(void)
{
    const float beat = 60.0f/MUSIC_BPM;
    const int bars = 8;
    int n = (int)(bars*4*beat*MUSIC_RATE);
    float *drums = (float *)MemAlloc(n*sizeof(float));
    float *choir = (float *)MemAlloc(n*sizeof(float));

    // Tambour : force des coups de caisse claire par double-croche (16 par mesure).
    // Mesure A = marche ; mesure B (4e et 8e) = finit par un roulement.
    static const float MARCH[16] = { 1, 0, .35f, 0, .8f, 0, .4f, .35f, 1, 0, .35f, 0, .8f, .45f, .55f, .65f };
    static const float ROLL[16]  = { 1, 0, .35f, 0, .8f, 0, .4f, .35f, 1, .3f, .4f, .5f, .6f, .7f, .8f, .9f };
    float sixteenth = beat/4;
    for (int b = 0; b < bars; b++)
    {
        const float *pat = (b % 4 == 3) ? ROLL : MARCH;
        for (int s = 0; s < 16; s++)
        {
            float t = (b*16 + s)*sixteenth;
            if (pat[s] > 0) AddSnare(drums, n, (int)(t*MUSIC_RATE), pat[s]);
            // Roulement : un coup supplementaire entre chaque double-croche
            if (pat == ROLL && s >= 9) AddSnare(drums, n, (int)((t + sixteenth/2)*MUSIC_RATE), pat[s]*0.6f);
            if (s == 0 || s == 8) AddKick(drums, n, (int)(t*MUSIC_RATE), s == 0 ? 1.0f : 0.8f);
        }
    }

    // Melodie (notes midi et durees en temps) : re mineur, 8 mesures
    static const struct { int midi; float beats; } MELODY[] = {
        { 50, 2 }, { 53, 1 }, { 55, 1 },   // re  fa  sol
        { 57, 3 }, { 57, 1 },              // la ...
        { 58, 2 }, { 57, 1 }, { 55, 1 },   // sib la sol
        { 57, 4 },                         // la
        { 62, 2 }, { 60, 1 }, { 58, 1 },   // re  do  sib
        { 57, 2 }, { 55, 2 },              // la  sol
        { 53, 1 }, { 55, 1 }, { 52, 2 },   // fa  sol mi
        { 50, 4 },                         // re
    };
    float t = 0.0f;
    for (unsigned i = 0; i < sizeof(MELODY)/sizeof(MELODY[0]); i++)
    {
        float len = MELODY[i].beats*beat;
        AddVoice(choir, n, t, len*0.95f, MELODY[i].midi, 0.16f);         // voix principale
        AddVoice(choir, n, t, len*0.95f, MELODY[i].midi - 12, 0.12f);    // basses a l'octave
        AddVoice(choir, n, t, len*0.95f, MELODY[i].midi - 7, 0.08f);     // quinte en dessous (couleur medievale)
        t += len;
    }
    // Bourdon re-la tenu sur toute la boucle (deux notes par mesure pour respirer)
    for (int b = 0; b < bars; b++)
    {
        AddVoice(choir, n, b*4*beat, 4*beat*0.97f, 38, 0.07f);
        AddVoice(choir, n, b*4*beat, 4*beat*0.97f, 45, 0.05f);
    }

    // Le choeur passe par les formants de la voyelle "a" (bouche ouverte)
    Reso r1 = { 0 }, r2 = { 0 }, r3 = { 0 };
    float peak = 1e-6f;
    for (int pass = 0; pass < 2; pass++)   // 2 tours : le 2e part des etats du 1er, la boucle se raccorde sans clic
        for (int i = 0; i < n; i++)
        {
            float x = choir[i];
            float v = ResoStep(&r1, x, 700.0f, 110.0f, MUSIC_RATE) + 0.7f*ResoStep(&r2, x, 1150.0f, 140.0f, MUSIC_RATE)
                    + 0.25f*ResoStep(&r3, x, 2600.0f, 220.0f, MUSIC_RATE);
            if (pass == 1)
            {
                drums[i] = drums[i]*0.55f + v*0.65f;   // meme volume moyen pour le tambour et le choeur
                peak = fmaxf(peak, fabsf(drums[i]));
            }
        }

    // Fichier WAV en memoire : en-tete de 44 octets puis echantillons 16 bits
    int dataBytes = n*2;
    musicWav = (unsigned char *)MemAlloc(44 + dataBytes);
    unsigned char *h = musicWav;
    #define PUT32(p, v) do { unsigned int x_ = (v); (p)[0] = x_ & 255; (p)[1] = (x_ >> 8) & 255; (p)[2] = (x_ >> 16) & 255; (p)[3] = x_ >> 24; } while (0)
    #define PUT16(p, v) do { (p)[0] = (v) & 255; (p)[1] = ((v) >> 8) & 255; } while (0)
    memcpy(h, "RIFF", 4); PUT32(h + 4, 36 + dataBytes); memcpy(h + 8, "WAVEfmt ", 8);
    PUT32(h + 16, 16); PUT16(h + 20, 1); PUT16(h + 22, 1);            // PCM, mono
    PUT32(h + 24, MUSIC_RATE); PUT32(h + 28, MUSIC_RATE*2);           // frequence, octets par seconde
    PUT16(h + 32, 2); PUT16(h + 34, 16);                              // 2 octets par echantillon, 16 bits
    memcpy(h + 36, "data", 4); PUT32(h + 40, dataBytes);
    for (int i = 0; i < n; i++)
    {
        short v = (short)(drums[i]/peak*0.85f*32000.0f);
        PUT16(h + 44 + i*2, (unsigned short)v);
    }
    #undef PUT32
    #undef PUT16
    MemFree(drums);
    MemFree(choir);

    music = LoadMusicStreamFromMemory(".wav", musicWav, 44 + dataBytes);
    musicOk = IsMusicValid(music);
    if (!musicOk) return;
    music.looping = true;
    SetMusicVolume(music, 0.4f);
    PlayMusicStream(music);
}

static Sound GenSound(SndId id)
{
    if (id == SND_TAUNT) return GenTaunt();   // voix : generateur a part (filtres a etat)
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
    if (audioOk) MusicInit();
}

// A appeler a chaque image : raylib remplit le tampon audio de la musique au fur et a mesure
void AudioUpdate(void)
{
    if (musicOk) UpdateMusicStream(music);
}

// Les alias doivent etre liberes avant le son d'origine dont ils partagent les donnees
void AudioClose(void)
{
    if (audioOk)
    {
        for (int i = 0; i < SHOT_VOICES; i++) UnloadSoundAlias(shotVoices[i]);
        for (int i = 0; i < SND_COUNT; i++) UnloadSound(sounds[i]);
        if (musicOk) UnloadMusicStream(music);
        MemFree(musicWav);
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
