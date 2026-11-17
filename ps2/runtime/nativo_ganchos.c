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

/* V41: linha do tempo de diagnóstico. Guarda os últimos DIAG_N eventos com o
 * relógio do EE (294,912 MHz) e despeja alguns quadros da luta no log, para
 * medir a ordem e a duração exata das etapas (VBlank, timer 2, exceções,
 * espera do quadro, lógica, listas de desenho, cópia para o GS). */
#define DIAG_N 16384u   /* V43: 4096 -> 16384 (~4 s da luta: guarda o quadro lento até o relatório) */
static uint32_t dg_t[DIAG_N], dg_a[DIAG_N];
static uint8_t  dg_e[DIAG_N];
static uint32_t dg_i;
void ps2_diag(unsigned e, uint32_t a)
{
#ifdef _EE
    uint32_t t, k;
    __asm__ volatile("mfc0 %0, $9" : "=r"(t));
    k = dg_i++ & (DIAG_N - 1u);
    dg_t[k] = t; dg_e[k] = (uint8_t)e; dg_a[k] = a;
#else
    (void)e; (void)a;
#endif
}
static const char *const dg_nome[] = {
    "?", "VBLANK", "T2 arma", "T2 desarma", "T2 IRQ", "exc entra", "exc sai",
    "handler ini", "handler fim", "espera ini", "espera fim", "luta",
    "lista ini", "lista fim", "copia GS ini", "copia GS fim", "exc saida jogo",
    "vsync cb", "OTC", "OTC n", "checa recup", "relat ini", "relat fim",
    "CD le", "CD espera"
};
static void diag_despejar_ate(uint32_t fim, unsigned n);
void ps2_diag_despejar(unsigned n) { diag_despejar_ate(dg_i, n); }
/* V43: quadro que passou de ~37 ms (perde 1 passo da lógica): guarda onde a
 * linha do tempo estava; o relatório seguinte despeja os eventos antes dele. */
static uint32_t dg_lento_fim, dg_lento_ms;
static unsigned dg_lento_pend, dg_lentos;
void ps2_diag_despejar_lento(void)
{
    if (!dg_lento_pend) return;
    printf("diag: QUADRO LENTO %lu ms (eventos ate o fim dele)\n", (unsigned long)dg_lento_ms);
    if (dg_i - dg_lento_fim < DIAG_N - 600u) diag_despejar_ate(dg_lento_fim, 500u);
    dg_lento_pend = 0;
}
static void diag_despejar_ate(uint32_t fim, unsigned n)
{
    uint32_t ini, i, t0;
    if (n > DIAG_N - 1u) n = DIAG_N - 1u;
    if (fim < n) n = fim;
    ini = fim - n;
    t0 = dg_t[ini & (DIAG_N - 1u)];
    printf("diag: linha do tempo, %u eventos (us desde o primeiro)\n", n);
    for (i = ini; i != fim; i++) {
        uint32_t k = i & (DIAG_N - 1u);
        unsigned e = dg_e[k];
        printf("diag: %8lu %-14s %08lx\n", (unsigned long)((dg_t[k] - t0) / 295u),
               e < sizeof(dg_nome) / sizeof(dg_nome[0]) ? dg_nome[e] : "?", (unsigned long)dg_a[k]);
    }
}
void ps2_diag_luta(void) { ps2_diag(11, 0); }
void ps2_diag_espera_fim(void) { ps2_diag(10, ps2_ram_get32(0x8009bc5cu)); }
/* V42: 800295F4 = "passaram 2+ IRQs do timer 2 desde o início do quadro?"
 * (decide o passo de recuperação da lógica, sem desenho). Registra o valor
 * do contador 8009BC5C que o jogo vai comparar. */
void ps2_diag_recupera(void) { ps2_diag(20, ps2_ram_get32(0x8009bc5cu)); }

/* V37: período entre entradas na espera do quadro (800296C4), em ciclos do
 * EE (294,912 MHz; 1 VBlank NTSC = 4,92 M). Separa os quadros que couberam em
 * 1 VBlank dos que perderam o VBlank, e guarda o pior. */
static uint32_t q_ant, q_max, q_n, q_1vb, q_2vb, q_3vb;
uint32_t g_ps2_quadros_ult;          /* V43: quadros do jogo no último relatório (~2 s) */
void ps2_quadro_espera(void)
{
    ps2_diag(9, ps2_ram_get32(0x8009bc5cu));   /* V41: passos do timer no quadro */
#ifdef _EE
    uint32_t t, d;
    __asm__ volatile("mfc0 %0, $9" : "=r"(t));
    d = t - q_ant;
    q_ant = t;
    if (q_n++ == 0u && q_1vb + q_2vb + q_3vb == 0u) return;   /* 1º do período */
    if (d < 6000000u) q_1vb++;          /* < ~20 ms   */
    else if (d < 11000000u) q_2vb++;    /* < ~37 ms   */
    else {
        q_3vb++;
        if (!dg_lento_pend && dg_lentos < 6u && d < 60000000u) {   /* V43 */
            dg_lento_pend = 1; dg_lentos++; dg_lento_fim = dg_i; dg_lento_ms = d / 294912u;
        }
    }
    if (d > q_max) q_max = d;
#endif
}
void ps2_quadro_relatorio(void)
{
    g_ps2_quadros_ult = q_n;
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

/* V43: velocidade REAL do jogo pelo relógio do round. 800ADBCC = passos de
 * 1/60 s que faltam: o jogo põe (x+2)*600 no início (8002A9C8), tira 1 por
 * passo da lógica (8003CCDC), mostra (t+59)/60 s e com t < 1 dá TIME UP
 * (8003E478). No PS1 o relógio anda 1 passo por VBlank (59,94 por segundo),
 * então passos do relógio / VBlanks do PS1 = velocidade do jogo (100% = PS1).
 * Mede sem cronômetro: do 1º ao último passo do relógio de cada rodada. */
static int32_t rl_ant;
static int rl_ativo;
static uint32_t rl_vb, rl_vb_ult, rl_dec, rl_ee_ant;
static uint64_t rl_ee, rl_ee_ult;
static uint32_t rl_jan_dec, rl_jan_vb;
static char rl_rodada[24] = "-";
static char rl_painel[64];
void ps2_relogio_vblank(void)
{
    int32_t t = (int32_t)ps2_ram_get32(0x800ADBCCu);
    int32_t d = rl_ant - t;
    uint32_t agora = 0;
#ifdef _EE
    __asm__ volatile("mfc0 %0, $9" : "=r"(agora));
#endif
    if (rl_ativo) {
        rl_vb++; rl_jan_vb++;
        rl_ee += (uint32_t)(agora - rl_ee_ant);
    }
    rl_ee_ant = agora;
    if (d > 0 && d <= 8) {
        if (!rl_ativo) {                 /* 1º passo: começa a medir daqui */
            rl_ativo = 1; rl_vb = rl_vb_ult = rl_dec = 0; rl_ee = rl_ee_ult = 0;
        } else {
            rl_dec += (uint32_t)d; rl_jan_dec += (uint32_t)d;
            rl_vb_ult = rl_vb; rl_ee_ult = rl_ee;
        }
    }
    if (rl_ativo && (t <= 0 || d < 0 || d > 8 || rl_vb - rl_vb_ult > 180u)) {
        if (rl_dec >= 600u && rl_vb_ult) {
            uint32_t pct = (uint32_t)((uint64_t)rl_dec * 10000u / rl_vb_ult);
            uint32_t ms = (uint32_t)(rl_ee_ult / 294912u);
            uint32_t ms_ps1 = (uint32_t)((uint64_t)rl_dec * 1000000u / 59940u);
            printf("rodada: relogio andou %lu passos (%lu.%02lu s de jogo) em %lu VBlanks do PS1 = %lu.%02lu%% "
                   "| %lu ms reais (no PS1: %lu ms)\n",
                   (unsigned long)rl_dec, (unsigned long)(rl_dec / 60u), (unsigned long)(rl_dec % 60u * 100u / 60u),
                   (unsigned long)rl_vb_ult, (unsigned long)(pct / 100u), (unsigned long)(pct % 100u),
                   (unsigned long)ms, (unsigned long)ms_ps1);
            snprintf(rl_rodada, sizeof(rl_rodada), "%lu.%lu%% %lus",
                     (unsigned long)(pct / 100u), (unsigned long)(pct % 100u / 10u), (unsigned long)(rl_dec / 60u));
        }
        rl_ativo = 0;
    }
    rl_ant = t;
}
/* Uma linha por relatório (~2 s) e o texto do painel. */
const char *ps2_relogio_relatorio(void)
{
    char jan[16] = "-";
    if (rl_jan_vb) {
        uint32_t pct = (uint32_t)((uint64_t)rl_jan_dec * 1000u / rl_jan_vb);
        printf("relogio: %lu passos do relogio do round em %lu VBlanks do PS1 (%lu.%lu%%)\n",
               (unsigned long)rl_jan_dec, (unsigned long)rl_jan_vb,
               (unsigned long)(pct / 10u), (unsigned long)(pct % 10u));
        snprintf(jan, sizeof(jan), "%lu.%lu%%", (unsigned long)(pct / 10u), (unsigned long)(pct % 10u));
    }
    rl_jan_dec = rl_jan_vb = 0;
    snprintf(rl_painel, sizeof(rl_painel), "QUADROS/S %lu JOGO %s\nRODADA %s",
             (unsigned long)(g_ps2_quadros_ult / 2u), jan, rl_rodada);
    return rl_painel;
}
