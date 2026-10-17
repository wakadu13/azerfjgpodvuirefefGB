/*
 * waves.c - Une vague = un nombre fixe de mercenaires qui apparaissent peu a
 * peu (avec un boss toutes les 5 vagues). Quand tout est mort : pause de
 * quelques secondes, recompenses, puis vague suivante.
 *
 * L'etat des vagues est stocke dans la structure globale G (game.h) :
 *   G.wave           numero de la vague en cours
 *   G.toSpawn        mercenaires restant a faire apparaitre
 *   G.bossPending    vrai tant que le boss de la vague n'est pas apparu
 *   G.spawnT         compte a rebours avant la prochaine apparition
 *   G.inIntermission vrai pendant la pause entre deux vagues
 *   G.interT         temps restant de la pause
 *   G.bannerT        duree d'affichage restante du bandeau "VAGUE X"
 */
#include "waves.h"
#include "game.h"
#include "enemy.h"
#include "player.h"
#include "effects.h"
#include "util.h"

#include <math.h>

// Plafond de mercenaires vivants simultanement (lisibilite et performances) :
// 40 en solo, +2 par joueur supplementaire, 120 au maximum (40 joueurs et plus)
static int MaxAliveAtOnce(void)
{
    int n = 40 + 2*(PlayersConnected() - 1);
    return n > 120 ? 120 : n;
}
#define INTERMISSION_TIME  8.0f   // duree de la pause entre deux vagues (secondes)

// Taille d'une vague : 3 + 3 par numero de vague (vague 1 = 6, vague 5 = 18...).
// Multipliee par (1 + nombre de joueurs) / 2 : x1 en solo, x1.5 a deux, x2 a trois...
int WaveSize(int wave)
{
    // +50 % d'ennemis par joueur supplementaire
    int n = 3 + wave*3;
    return n*(2 + PlayersConnected() - 1)/2;
}

void WavesStart(int wave)
{
    G.wave = wave;
    G.toSpawn = WaveSize(wave);
    G.bossPending = (wave % 5 == 0);   // vagues 5, 10, 15... : un boss en plus
    G.spawnT = 1.0f;                   // premier ennemi 1 s apres le debut
    G.inIntermission = false;
    G.bannerT = 3.0f;                  // bandeau affiche 3 s
    SfxWorld(SND_HORN, 1.0f, 0.6f);

    // Les joueurs tombes reviennent au debut de chaque vague
    for (int i = 0; i < numPlayers; i++)
        if (players[i].connected && !players[i].alive) PlayerRespawn(&players[i]);
}

// Choix aleatoire du type de mercenaire. Avec r entre 0 et 99 :
//  - rapide (E_FAST) a partir de la vague 2, probabilite (12 + 2*vague) % ;
//  - costaud (E_TANK) a partir de la vague 3, probabilite (7 + 2*vague) % ;
//  - sinon normal. Les ennemis speciaux deviennent donc plus frequents.
static EnemyType PickType(int wave)
{
    int r = GetRandomValue(0, 99);
    if (wave >= 2 && r < 12 + wave*2) return E_FAST;
    if (wave >= 3 && r > 92 - wave*2) return E_TANK;
    return E_NORMAL;
}

// Recompenses de fin de vague : points (250 x numero de vague), 2 poussins
// rendus au stand (sans depasser MAX_CHICKS), et pour chaque joueur vivant :
// 90 balles, 3 harpons et 25 PV (maximum 100).
static void GiveRewards(void)
{
    G.score += 250*G.wave;
    G.chicks = (G.chicks + 2 > MAX_CHICKS) ? MAX_CHICKS : G.chicks + 2;
    for (int i = 0; i < numPlayers; i++)
    {
        Player *p = &players[i];
        if (!p->connected || !p->alive) continue;
        PlayerGiveAmmo(p, 90);
        PlayerGiveHarpoons(p, 3);
        p->hp = fminf(100, p->hp + 25);
    }
    SfxWorld(SND_PICK, 0.8f, 0.6f);   // son de bonus un peu plus grave (pitch 0.8)
}

// Machine a etats simple : pause -> apparitions -> attente de la fin -> pause...
void WavesUpdate(float dt)
{
    G.alive = EnemiesAlive();

    // 1. Pause entre deux vagues : on attend la fin du compte a rebours
    if (G.inIntermission)
    {
        G.interT -= dt;
        if (G.interT <= 0) WavesStart(G.wave + 1);
        return;
    }

    // 2. Il reste des ennemis a faire apparaitre
    if (G.toSpawn > 0 || G.bossPending)
    {
        G.spawnT -= dt;
        // On attend la fin du delai, et qu'il n'y ait pas deja trop d'ennemis
        if (G.spawnT > 0 || G.alive >= MaxAliveAtOnce()) return;
        // Le boss arrive au milieu de sa vague
        if (G.bossPending && G.toSpawn <= WaveSize(G.wave)/2) { EnemySpawn(E_BOSS, G.wave); G.bossPending = false; }
        else if (G.toSpawn > 0) { EnemySpawn(PickType(G.wave), G.wave); G.toSpawn--; }
        // Delai avant le suivant : 2,2 s a la vague 0, -0,15 s par vague,
        // jamais moins de 0,35 s ; puis +/- aleatoire (x0,6 a x1,3) pour un rythme irregulier.
        // Divise par (1 + 0,25 par joueur en plus) : les grosses vagues des parties
        // a 50 joueurs arrivent plus vite (environ 13 fois plus vite a 50).
        G.spawnT = fmaxf(0.35f, 2.2f - G.wave*0.15f)*Randf(0.6f, 1.3f)/(1.0f + 0.25f*(PlayersConnected() - 1));
    }
    // 3. Tout est apparu et tout est mort : vague terminee
    else if (G.alive == 0)
    {
        G.inIntermission = true;
        G.interT = INTERMISSION_TIME;
        GiveRewards();
    }
}
