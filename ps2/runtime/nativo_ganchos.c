/*
 * nativo_ganchos.c — liga as rotinas traduzidas por tools/nativo.py
 * (build/r/nativo.c) no lugar do código do psxrecomp.
 *
 * O código gerado chama ps2_nat_XXXXXXXX_gancho() no começo da função (só na
 * entrada normal, cpu->pc == 0). Com NAT_VERIFICAR=1 no make, as primeiras
 * chamadas rodam as DUAS versões sobre o mesmo estado (RAM, scratchpad,
 * registradores, GTE) e registram no log se o resultado foi igual; o jogo
 * segue com o resultado da versão original.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cpu_state.h"
#include "ram.h"

static uint32_t g_nat_end[32], g_nat_n[32], g_nat_max[32];
static unsigned long long g_nat_ciclos[32];
int g_ps2_nat_ligado = 1;
unsigned long g_ps2_nat_chamadas;

#ifdef PS2_NAT_VERIFICAR
extern int psx_dispatch_game_compiled(CPUState *cpu, uint32_t addr);
extern uint8_t g_ps2_scratchpad[1024];
extern int g_ps2_contagem_irq;
static uint8_t ram_s[0x200000], ram_a[0x200000];
static uint8_t spad_s[1024], spad_a[1024];
static unsigned n_verif, n_dif;

static void verificar(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *))
{
    CPUState salvo = *cpu, a;
    uint32_t ret = cpu->gpr[31], pc;
    int cont = g_ps2_contagem_irq;
    unsigned i, passos = 0;
    memcpy(ram_s, g_ps1_ram, sizeof ram_s);
    memcpy(spad_s, g_ps2_scratchpad, sizeof spad_s);
    nat(cpu);
    a = *cpu;
    memcpy(ram_a, g_ps1_ram, sizeof ram_a);
    memcpy(spad_a, g_ps2_scratchpad, sizeof spad_a);
    *cpu = salvo;
    memcpy(g_ps1_ram, ram_s, sizeof ram_s);
    memcpy(g_ps2_scratchpad, spad_s, sizeof spad_s);
    /* original: despacha até voltar para quem chamou, sem interrupções */
    g_ps2_nat_ligado = 0;
    g_ps2_contagem_irq = 1 << 30;
    pc = ini;
    do {
        if (!psx_dispatch_game_compiled(cpu, pc)) { printf("nativo: %08lx original saiu do despacho em %08lx\n", (unsigned long)ini, (unsigned long)pc); break; }
        pc = cpu->pc;
    } while (pc != ret && ++passos < 1000000u);
    g_ps2_contagem_irq = cont;
    g_ps2_nat_ligado = 1;
    n_verif++;
    {
        int dif = 0;
        for (i = 0; i < 32 && !dif; i++)
            if (a.gpr[i] != cpu->gpr[i]) { dif = 1; printf("nativo: %08lx DIFERENTE gpr[%u] nat=%08lx orig=%08lx\n", (unsigned long)ini, i, (unsigned long)a.gpr[i], (unsigned long)cpu->gpr[i]); }
        if (!dif && (a.hi != cpu->hi || a.lo != cpu->lo)) { dif = 1; printf("nativo: %08lx DIFERENTE hi/lo\n", (unsigned long)ini); }
        for (i = 0; i < 32 && !dif; i++)
            if (a.gte_data[i] != cpu->gte_data[i] || a.gte_ctrl[i] != cpu->gte_ctrl[i]) { dif = 1; printf("nativo: %08lx DIFERENTE gte %u\n", (unsigned long)ini, i); }
        if (!dif) for (i = 0; i < sizeof ram_a; i++)
            if (ram_a[i] != g_ps1_ram[i]) { dif = 1; printf("nativo: %08lx DIFERENTE ram %06x nat=%02x orig=%02x\n", (unsigned long)ini, i, ram_a[i], g_ps1_ram[i]); break; }
        if (!dif) for (i = 0; i < 1024; i++)
            if (spad_a[i] != g_ps2_scratchpad[i]) { dif = 1; printf("nativo: %08lx DIFERENTE scratchpad %03x\n", (unsigned long)ini, i); break; }
        if (dif) n_dif++;
        if (dif || n_verif <= 3 || n_verif % 10 == 0)
            printf("nativo: %08lx verificacao %u: %s (diferentes ate agora: %u, passos originais %u)\n",
                   (unsigned long)ini, n_verif, dif ? "DIFERENTE" : "igual", n_dif, passos);
    }
}
#endif

/* Chamado pelos ganchos gerados em build/r/nativo.c. */
int ps2_nat_executar(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *))
{
    if (!g_ps2_nat_ligado) return 0;
    g_ps2_nat_chamadas++;
#ifdef PS2_NAT_VERIFICAR
    {   /* 25 verificações por rotina */
        static uint32_t quem[32]; static unsigned quantas[32];
        unsigned k;
        for (k = 0; k < 32u && quem[k] && quem[k] != ini; k++) ;
        if (k < 32u) {
            quem[k] = ini;
            if (quantas[k] < 25u) { quantas[k]++; verificar(cpu, ini, nat); return 1; }
        }
    }
#endif
#ifdef _EE
    {   /* V32: tempo total, maior chamada e contagem por rotina nativa */
        static uint32_t quem_t[32];
        unsigned k; uint32_t t0, t1;
        for (k = 0; k < 32u && quem_t[k] && quem_t[k] != ini; k++) ;
        __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        nat(cpu);
        __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
        if (k < 32u) {
            uint32_t d = t1 - t0;
            quem_t[k] = ini; g_nat_end[k] = ini;
            g_nat_ciclos[k] += d; g_nat_n[k]++;
            if (d > g_nat_max[k]) g_nat_max[k] = d;
        }
    }
#else
    nat(cpu);
#endif
    return 1;
}

/* V36: sem som no port -> pula o tick do sequenciador de som (80088BA8).
 * Se algum ponto do jogo travar esperando música/voz terminar, compile com
 * -DPS2_SOM_PULAR=0 (ou troque o padrão abaixo) para voltar ao original. */
#ifndef PS2_SOM_PULAR
#define PS2_SOM_PULAR 1
#endif
unsigned long g_ps2_som_pulados;
int ps2_som_pular(CPUState *cpu)
{
#if PS2_SOM_PULAR
    g_ps2_som_pulados++;
    cpu->gpr[2] = 0;
    cpu->pc = cpu->gpr[31];         /* volta para quem chamou (jr $ra) */
    return 1;
#else
    (void)cpu;
    return 0;
#endif
}

/* V37: período entre entradas na espera do quadro (800296C4), em ciclos do
 * EE (294,912 MHz; 1 VBlank NTSC = 4,92 M). Separa os quadros que couberam em
 * 1 VBlank dos que perderam o VBlank, e guarda o pior. */
static uint32_t q_ant, q_max, q_n, q_1vb, q_2vb, q_3vb;
void ps2_quadro_espera(void)
{
#ifdef _EE
    uint32_t t, d;
    __asm__ volatile("mfc0 %0, $9" : "=r"(t));
    d = t - q_ant;
    q_ant = t;
    if (q_n++ == 0u && q_1vb + q_2vb + q_3vb == 0u) return;   /* 1º do período */
    if (d < 6000000u) q_1vb++;          /* < ~20 ms   */
    else if (d < 11000000u) q_2vb++;    /* < ~37 ms   */
    else q_3vb++;
    if (d > q_max) q_max = d;
#endif
}
void ps2_quadro_relatorio(void)
{
    printf("quadros: %lu (1 VBlank %lu, 2 VBlanks %lu, 3+ %lu), pior %lu.%lu ms\n",
           (unsigned long)q_n, (unsigned long)q_1vb, (unsigned long)q_2vb,
           (unsigned long)q_3vb, (unsigned long)(q_max / 294912u),
           (unsigned long)((q_max % 294912u) * 10u / 294912u));
    q_n = q_1vb = q_2vb = q_3vb = q_max = 0;
}

/* V32: uma linha por período no log (video_gs.c) */
void ps2_nat_relatorio(unsigned long long total)
{
    unsigned k;
    printf("nativas (%% do tempo, chamadas, maior chamada em us):");
    for (k = 0; k < 32u && g_nat_end[k]; k++) {
        if (!g_nat_n[k]) continue;
        printf(" %08lx:%llu%%/%lu/%lu", (unsigned long)g_nat_end[k],
               total ? g_nat_ciclos[k] * 100ull / total : 0ull,
               (unsigned long)g_nat_n[k], (unsigned long)(g_nat_max[k] / 294u));
        g_nat_ciclos[k] = 0; g_nat_n[k] = 0; g_nat_max[k] = 0;
    }
    printf("\n");
}
