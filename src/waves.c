/*
 * waves.c - Une vague = un nombre fixe de gloutons qui apparaissent peu a
 * peu (avec un boss toutes les 5 vagues). Quand tout est mort : pause de
 * quelques secondes, recompenses, puis vague suivante.
 */
#include "waves.h"
#include "game.h"
#include "enemy.h"
#include "player.h"
#include "audio.h"
#include "util.h"

#include <math.h>

#define MAX_ALIVE_AT_ONCE  40
#define INTERMISSION_TIME  8.0f

int WaveSize(int wave)
{
    int n = 3 + wave*3;
    return (numPlayers > 1) ? n*3/2 : n;
}

void WavesStart(int wave)
{
    G.wave = wave;
    G.toSpawn = WaveSize(wave);
    G.bossPending = (wave % 5 == 0);
    G.spawnT = 1.0f;
    G.inIntermission = false;
    G.bannerT = 3.0f;
    PlaySfx(SND_HORN, 1.0f, 0.6f);

    // Les joueurs tombes reviennent au debut de chaque vague
    for (int i = 0; i < numPlayers; i++) if (!players[i].alive) PlayerRespawn(&players[i]);
}

static EnemyType PickType(int wave)
{
    int r = GetRandomValue(0, 99);
    if (wave >= 2 && r < 12 + wave*2) return E_FAST;
    if (wave >= 3 && r > 92 - wave*2) return E_TANK;
    return E_NORMAL;
}

static void GiveRewards(void)
{
    G.score += 250*G.wave;
    G.burgers = (G.burgers + 2 > MAX_BURGERS) ? MAX_BURGERS : G.burgers + 2;
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->alive) continue;
        p->w.reserve = (p->w.reserve + 90 > MAX_RESERVE) ? MAX_RESERVE : p->w.reserve + 90;
        p->hp = fminf(100, p->hp + 25);
    }
    PlaySfx(SND_PICK, 0.8f, 0.6f);
}

void WavesUpdate(float dt)
{
    G.alive = EnemiesAlive();

    if (G.inIntermission)
    {
        G.interT -= dt;
        if (G.interT <= 0) WavesStart(G.wave + 1);
        return;
    }

    if (G.toSpawn > 0 || G.bossPending)
    {
        G.spawnT -= dt;
        if (G.spawnT > 0 || G.alive >= MAX_ALIVE_AT_ONCE) return;
        // Le boss arrive au milieu de sa vague
        if (G.bossPending && G.toSpawn <= WaveSize(G.wave)/2) { EnemySpawn(E_BOSS, G.wave); G.bossPending = false; }
        else if (G.toSpawn > 0) { EnemySpawn(PickType(G.wave), G.wave); G.toSpawn--; }
        G.spawnT = fmaxf(0.35f, 2.2f - G.wave*0.15f)*Randf(0.6f, 1.3f);
    }
    else if (G.alive == 0)
    {
        G.inIntermission = true;
        G.interT = INTERMISSION_TIME;
        GiveRewards();
    }
}
