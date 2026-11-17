#include "video_gs.h"
#include "gpu.h"
#include "jogo.h"
#include "io.h"
#include "kit/controle.h"
#include "gs_render.h"
#include "kit/diagnostico.h"

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

#define TELA_LARGURA 640u
#define TELA_ALTURA 448u
#define TEXTURA_ALTURA 512u

static uint32_t agora_ee(void) { uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
unsigned long long g_prof_pad, g_prof_exibir, g_prof_diag;
static int log_pendente;
static unsigned log_tentativas;
unsigned long g_ps2_log_adiado;

/* Grava o log no pendrive quando o CD não está lendo (a gravação usa o mesmo
 * fileXio e teria de esperar a leitura acabar). Depois de 4 s tentando, grava
 * mesmo assim. */
static void talvez_gravar_log(void)
{
    extern int ps2_cdrom_io_livre(void);
    extern unsigned long long g_prof_log;
    uint32_t t0;
    if (!log_pendente) return;
    if (!ps2_cdrom_io_livre() && ++log_tentativas < 240u) { g_ps2_log_adiado++; return; }
    t0 = agora_ee();
    kit_log_gravar();
    g_prof_log += (uint32_t)(agora_ee() - t0);
    log_pendente = 0; log_tentativas = 0;
}

static GSGLOBAL *gs;
static GSTEXTURE textura_gs;
static GSTEXTURE vram_gs;          /* a VRAM do PS1 dentro do GS (para exibir) */
static uint16_t imagem[TELA_LARGURA * TEXTURA_ALTURA]
    __attribute__((aligned(128)));

/* Copia a área exibida da VRAM para a textura (15 bits do GS). No modo de
 * 24 bits (vídeos do MDEC) cada pixel ocupa 3 bytes R,G,B na VRAM. */
static void copiar_tela(const Ps2GpuDisplay *d, const uint16_t *vram)
{
    unsigned x, y;
    if (!d->cor24) {
        for (y = 0; y < d->altura; ++y) {
            const uint16_t *lin = &vram[((d->y + y) & 511u) * 1024u];
            uint16_t *o = &imagem[y * TELA_LARGURA];
            for (x = 0; x < d->largura; ++x)
                o[x] = lin[(d->x + x) & 1023u] | 0x8000u;
        }
        return;
    }
    for (y = 0; y < d->altura; ++y) {
        const uint8_t *lin = (const uint8_t *)&vram[((d->y + y) & 511u) * 1024u];
        uint16_t *o = &imagem[y * TELA_LARGURA];
        unsigned b = (d->x & 1023u) * 2u;
        for (x = 0; x < d->largura; ++x, b += 3u) {
            unsigned r = lin[b & 2047u], g = lin[(b + 1u) & 2047u], bl = lin[(b + 2u) & 2047u];
            o[x] = (uint16_t)((r >> 3) | (g >> 3) << 5 | (bl >> 3) << 10 | 0x8000u);
        }
    }
}

/* Relógio de referência: VBlanks REAIS do PS2 (interrupção do GS, 59,94 Hz
 * NTSC). O tempo do PS1 é derivado do COUNT do EE; comparar os dois mostra se
 * o jogo roda na velocidade certa em tempo real. */
static volatile unsigned long vsync_real;
static int ao_vsync(int causa)
{
    (void)causa;
    vsync_real++;
    ExitHandler();
    return -1;
}

int ps2_video_init(void)
{
    if (dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                    D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1u << DMA_CHANNEL_GIF) < 0 ||
        dmaKit_chan_init(DMA_CHANNEL_GIF) < 0) {
        printf("video: falha ao inicializar DMA do GS\n");
        return 0;
    }
    gs = gsKit_init_global();
    if (!gs) {
        printf("video: falha ao inicializar gsKit\n");
        return 0;
    }
    if (AddIntcHandler(INTC_VBLANK_S, ao_vsync, 0) >= 0) EnableIntc(INTC_VBLANK_S);
    gs->Mode = GS_MODE_NTSC;
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;               /* 448 linhas de verdade (FRAME divide por 2) */
    gs->Width = TELA_LARGURA;
    gs->Height = TELA_ALTURA;
    gs->PSM = GS_PSM_CT32;
    gs->DoubleBuffering = GS_SETTING_OFF;
    gs->ZBuffering = GS_SETTING_OFF;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    memset(&textura_gs, 0, sizeof(textura_gs));
    textura_gs.Width = TELA_LARGURA;
    textura_gs.Height = TEXTURA_ALTURA;
    textura_gs.PSM = GS_PSM_CT16;
    textura_gs.TBW = TELA_LARGURA / 64;
    textura_gs.Filter = GS_FILTER_NEAREST;
    textura_gs.Mem = (u32 *)imagem;
    textura_gs.Vram = gsKit_vram_alloc(gs,
        gsKit_texture_size(TELA_LARGURA, TEXTURA_ALTURA, GS_PSM_CT16),
        GSKIT_ALLOC_USERBUFFER);
    if (textura_gs.Vram == GSKIT_ALLOC_ERROR) {
        printf("video: memoria do GS insuficiente para textura\n");
        return 0;
    }
    /* VRAM do PS1 (1 MB, alinhada a página) + cache de texturas (64 KB cada) */
    {
        uint32_t a = gsKit_vram_alloc(gs, 1024u * 1024u + 8192u, GSKIT_ALLOC_USERBUFFER);
        uint32_t vram_ini, cache_ini = 0;
        unsigned fatias = 0;
        if (a == GSKIT_ALLOC_ERROR) {
            printf("video: sem memoria do GS para a VRAM do PS1 (usando software)\n");
        } else {
            vram_ini = (a + 8191u) & ~8191u;
            for (;;) {
                uint32_t f;
                /* só pede o que cabe (evita a mensagem de erro do gsKit) */
                if (gs->CurrentPointer + 65536u > 4u * 1024u * 1024u || fatias >= 40u) break;
                f = gsKit_vram_alloc(gs, 65536u, GSKIT_ALLOC_USERBUFFER);
                if (f == GSKIT_ALLOC_ERROR) break;
                if (!fatias) cache_ini = f;
                fatias++;
            }
            if (fatias >= 8u) {
                memset(&vram_gs, 0, sizeof(vram_gs));
                vram_gs.Width = 1024; vram_gs.Height = 512;
                vram_gs.PSM = GS_PSM_CT16; vram_gs.TBW = 16;
                vram_gs.Filter = GS_FILTER_NEAREST;
                vram_gs.Vram = vram_ini;
                gsr_definir_sombra(ps2_gpu_vram());
                gsr_init(vram_ini, cache_ini, fatias);
                { extern void gsr_bench(void); extern void gte_bench(void); gsr_bench(); gte_bench(); }
            } else
                printf("video: cache de texturas pequeno demais (usando software)\n");
        }
    }
    printf("video: GS 640x448 inicializado\n");
    return 1;
}

extern unsigned long g_ps2_gpu_palavras;
extern unsigned long long g_ps2_gpu_ciclos;
unsigned long long g_ps2_ee_total;
unsigned long long g_ps2_video_ciclos;

#ifdef PS2_DEBUG_VRAM
/* COUNT do EE avança a 294,912 MHz; acumula diferenças para suportar wrap. */
static void salvar_vram(const uint16_t *vram)
{
    static const char *nomes[] = {
        "host:vram_dump_20.bin", "host:vram_dump_40.bin", "host:vram_dump_80.bin"
    };
    static const unsigned segundos[] = {30, 200, 300};   /* tempo do EE, não do relógio */
    static uint32_t anterior;
    static uint64_t ciclos;
    static unsigned proximo, iniciado;
    uint32_t agora;
    __asm__ volatile("mfc0 %0, $9" : "=r"(agora));
    if (!iniciado) {
        anterior = agora;
        iniciado = 1;
        return;
    }
    ciclos += (uint32_t)(agora - anterior);
    anterior = agora;
    if (proximo < 3u && ciclos >= (uint64_t)segundos[proximo] * 294912000u) {
        FILE *f = fopen(nomes[proximo], "wb");
        if (!f) {
            printf("video: erro ao abrir %s\n", nomes[proximo]);
        } else {
            size_t escritos = fwrite(vram, sizeof(*vram), 1024u * 512u, f);
            int erro = fclose(f);
            if (escritos != 1024u * 512u || erro)
                printf("video: erro ao gravar %s (%lu pixels)\n", nomes[proximo],
                       (unsigned long)escritos);
            else
                printf("video: VRAM salva %s em %lu ms\n", nomes[proximo],
                       (unsigned long)(ciclos / 294912u));
        }
        ++proximo;
    }
}
#endif

/* Diagnóstico a cada 2 s: estado da exibição e se há algo desenhado. */
static void diagnostico(const Ps2GpuDisplay *d, const uint16_t *vram)
{
    static unsigned n;
    unsigned x, y, acesos = 0, total = 0;
    if (++n % 120u) return;
    if (!g_gsr_ativo)          /* com o GS a imagem não está na sombra */
    for (y = 0; y < 512u; y += 8u)
        for (x = 0; x < 1024u; x += 8u) {
            total++;
            if (vram[y * 1024u + x] & 0x7FFFu) acesos++;
        }
    {
        extern unsigned long g_ps2_px_tri, g_ps2_px_tri_tex, g_ps2_px_ret, g_ps2_px_semi;
        extern unsigned long g_ps2_px_ret_solido, g_ps2_px_ret_tex, g_ps2_px_ret_lento, g_ps2_px_ret_bruto;
        static unsigned long long total_ant, gpu_ant, vid_ant;
        unsigned long long total = g_ps2_ee_total;
        if (total_ant && total > total_ant) {
            unsigned long long t = total - total_ant;
            { extern unsigned long long g_ps2_exc_ciclos; static unsigned long long ant;
              printf("perfil: interrupcao %llu%%\n", (g_ps2_exc_ciclos - ant) * 100ull / t); ant = g_ps2_exc_ciclos; }
            if (g_gsr_ativo) {
                extern unsigned long long g_ps2_exc_ciclos;
                extern unsigned long g_ps2_listas_puladas, g_ps2_listas_total, g_gsr_prims, g_ps2_listas_ciclicas;
                extern int dedup_ligado;
                static unsigned long long exc_ant2;
                char txt[180];
                snprintf(txt, sizeof(txt),
                         "%s%s\nGPU %llu%% INT %llu%%\nLISTAS %lu PUL %lu CIC %lu\nPOLI/VB %lu\n%s",
                         JOGO_VERSAO, dedup_ligado ? "" : " SEM DEDUP",
                         (unsigned long long)(g_ps2_gpu_ciclos - gpu_ant) * 100ull / t,
                         (g_ps2_exc_ciclos - exc_ant2) * 100ull / t,
                         g_ps2_listas_total, g_ps2_listas_puladas, g_ps2_listas_ciclicas, g_gsr_prims / 120ul,
                         kit_log_estado());
                gsr_painel(txt);
                /* linha fácil de analisar: tempo(ms), gpu%, int%, listas, puladas, poligonos/VB */
                {
                    static unsigned long vs_ant;
                    unsigned long vs = vsync_real;
                    { extern unsigned long g_ps2_vblanks_perdidos;
                      printf("tempo: %lu VBlanks reais do PS2 nestes 120 VBlanks do PS1 (iguais = velocidade certa); "
                             "VBlanks do PS1 perdidos por atraso: %lu\n", vs - vs_ant, g_ps2_vblanks_perdidos);
                      g_ps2_vblanks_perdidos = 0; }
                    vs_ant = vs;
                }
                printf("csv:%lu,%llu,%llu,%lu,%lu,%lu\n",
                       (unsigned long)(g_ps2_ee_total / 294912ull),
                       (unsigned long long)(g_ps2_gpu_ciclos - gpu_ant) * 100ull / t,
                       (g_ps2_exc_ciclos - exc_ant2) * 100ull / t,
                       g_ps2_listas_total, g_ps2_listas_puladas, g_gsr_prims / 120ul);
                exc_ant2 = g_ps2_exc_ciclos;
            }
            {   /* perfilador: onde vai o tempo (PS2 real) */
                extern unsigned long long g_prof_gte, g_prof_irq, g_prof_interp;
                extern unsigned long g_prof_gte_n, g_prof_scratch_n, g_prof_io_n;
                extern unsigned g_prof_hist[2048];
                extern unsigned g_prof_amostras, g_prof_amostras_exc;
                static unsigned long long gte_a, irq_a, int_a;
                unsigned k, j;
                printf("prof: gte %llu%% (%lu/VB) | poll %llu%% | interp %llu%% | scratch %lu/VB io %lu/VB | amostras %u (exc %u)\n",
                       (g_prof_gte - gte_a) * 100ull / t, g_prof_gte_n / 120ul,
                       (g_prof_irq - irq_a) * 100ull / t,
                       (g_prof_interp - int_a) * 100ull / t,
                       g_prof_scratch_n / 120ul, g_prof_io_n / 120ul,
                       g_prof_amostras, g_prof_amostras_exc);
                {
                    extern unsigned long long g_prof_irq_ciclos[11];
                    extern unsigned long g_prof_irq_n[11];
                    static const char *nome[11] = {"vbl","gpu","cd","dma","t0","t1","t2","pad","sio","spu","outro"};
                    static unsigned long long ant[11];
                    unsigned q;
                    printf("irq:");
                    for (q = 0; q < 11u; q++)
                        if (g_prof_irq_n[q])
                            printf(" %s %llu%% (%lu)", nome[q], (g_prof_irq_ciclos[q] - ant[q]) * 100ull / t, g_prof_irq_n[q]);
                    printf("\n");
                    for (q = 0; q < 11u; q++) { ant[q] = g_prof_irq_ciclos[q]; g_prof_irq_n[q] = 0; }
                }
                {   /* V35: ritmo do jogo. O Tekken avança pelo timer 2 (handler
                     * 80029784): 80095434 conta as IRQs que o jogo recebeu e
                     * 80097f00 os passos liberados (8009bc5c). */
                    extern uint8_t g_ps1_ram[];
                    static uint32_t tick_a, passo_a;
                    uint32_t tick, passo;
                    unsigned long ev, en, de; unsigned md, al;
                    memcpy(&tick, g_ps1_ram + 0x95434u, 4);
                    memcpy(&passo, g_ps1_ram + 0x97f00u, 4);
                    ps2_timers_estat(2, &ev, &en, &de, &md, &al);
                    extern unsigned long g_ps2_som_pulados;
                    printf("ritmo: timer2 eventos %lu entregues %lu descartados %lu (modo %04x alvo %04x) | jogo: irqs vistas %lu, passos liberados %lu | som pulado %lu\n",
                           ev, en, de, md, al, (unsigned long)(tick - tick_a), (unsigned long)(passo - passo_a), g_ps2_som_pulados);
                    g_ps2_som_pulados = 0;
                    tick_a = tick; passo_a = passo;
                }
                {
                    extern unsigned long long g_prof_espera_gs, g_prof_log, g_prof_cd, g_prof_tmr, g_prof_vblank;
                    extern unsigned long g_gsr_texflush;
                    static unsigned long long a[8];
                    unsigned long long v[8];
                    v[0] = g_prof_vblank; v[1] = g_prof_exibir; v[2] = g_prof_espera_gs; v[3] = g_prof_pad;
                    v[4] = g_prof_diag; v[5] = g_prof_log; v[6] = g_prof_cd; v[7] = g_prof_tmr;
                    {
                        extern unsigned long g_ps2_cd_leituras_arquivo, g_ps2_cd_seeks, g_ps2_cd_bfrd_atrasado;
                        extern unsigned long g_ps2_cd_antecipadas, g_ps2_cd_prontas, g_ps2_cd_faltas;
                        extern unsigned long long g_ps2_cd_espera;
                        extern uint32_t g_ps2_cd_espera_max;
                        printf("cd: leituras %lu (antecipadas %lu, ja prontas %lu, faltas %lu), fseek %lu, "
                               "espera %lu ms (max %lu ms), BFRD atendido depois %lu, log adiado %lu VB (em 120 VBlanks)\n",
                               g_ps2_cd_leituras_arquivo, g_ps2_cd_antecipadas, g_ps2_cd_prontas, g_ps2_cd_faltas,
                               g_ps2_cd_seeks, (unsigned long)(g_ps2_cd_espera / 294912ull),
                               (unsigned long)(g_ps2_cd_espera_max / 294912u), g_ps2_cd_bfrd_atrasado,
                               g_ps2_log_adiado);
                        g_ps2_log_adiado = 0;
                        g_ps2_cd_leituras_arquivo = g_ps2_cd_seeks = g_ps2_cd_bfrd_atrasado = 0;
                        g_ps2_cd_antecipadas = g_ps2_cd_prontas = g_ps2_cd_faltas = 0;
                        g_ps2_cd_espera = 0; g_ps2_cd_espera_max = 0;
                    }
                    printf("poll: vblank %llu%% (exibir %llu%%, espera GS %llu%%, controle %llu%%, diag %llu%%, log %llu%%) | cd %llu%% timers %llu%% | texflush %lu/VB\n",
                           (v[0]-a[0])*100ull/t, (v[1]-a[1])*100ull/t, (v[2]-a[2])*100ull/t, (v[3]-a[3])*100ull/t,
                           (v[4]-a[4])*100ull/t, (v[5]-a[5])*100ull/t, (v[6]-a[6])*100ull/t, (v[7]-a[7])*100ull/t,
                           g_gsr_texflush / 120ul);
                    memcpy(a, v, sizeof(a));
                    g_gsr_texflush = 0;
                }
                {
                    extern unsigned long long g_prof_ciclos_amostras;
                    unsigned long long tot = g_prof_ciclos_amostras >> 8;
                    unsigned long blocos = (unsigned long)g_prof_amostras * 256ul;
                    /* ciclos do EE por bloco do PS1: mede a eficiência do código
                     * gerado (cache de instruções etc.); menor = mais rápido */
                    printf("velocidade: %lu blocos do PS1/s, %llu ciclos do EE por bloco\n",
                           blocos / 2ul, blocos ? g_prof_ciclos_amostras / blocos : 0ull);
                    printf("quente (%% do tempo):");
                    for (k = 0; k < 12u; k++) {
                        unsigned m = 0;
                        for (j = 1; j < 2048u; j++) if (g_prof_hist[j] > g_prof_hist[m]) m = j;
                        if (!g_prof_hist[m] || !tot) break;
                        printf(" %08lx:%llu", 0x80000000ul | ((unsigned long)m << 10),
                               (unsigned long long)g_prof_hist[m] * 100ull / tot);
                        g_prof_hist[m] = 0;
                    }
                    printf("\n");
                    g_prof_ciclos_amostras = 0;
                    { extern void ps2_prof_funcoes(void); ps2_prof_funcoes(); }
                    {   /* V32 */
                        extern void ps2_nat_relatorio(unsigned long long total);
                        extern unsigned long g_ps2_luta_atualiz;
                        extern uint32_t g_ps2_maior_sem_poll;
                        ps2_nat_relatorio((unsigned long long)t);
                        printf("logica: atualizacoes da luta (8002ae58) %lu | maior intervalo sem verificar interrupcao %lu us\n",
                               g_ps2_luta_atualiz, (unsigned long)(g_ps2_maior_sem_poll / 294u));
                        g_ps2_luta_atualiz = 0; g_ps2_maior_sem_poll = 0;
                    }
                }
                memset(g_prof_hist, 0, sizeof(g_prof_hist));
                gte_a = g_prof_gte; irq_a = g_prof_irq; int_a = g_prof_interp;
                g_prof_gte_n = g_prof_scratch_n = g_prof_io_n = 0;
                g_prof_amostras = g_prof_amostras_exc = 0;
            }
            {   /* V28: onde vai o tempo do desenho */
                extern unsigned long long g_prof_gpu_hash, g_prof_gsr_poli, g_prof_espera_gs;
                extern unsigned long g_gsr_prims;
                static unsigned long long h_a, p_a, e_a; static unsigned long pr_a;
                unsigned long pr = g_gsr_prims; (void)pr_a;  /* zerado a cada período */
                printf("desenho: comandos %llu%% (dos quais gsr_poligono %llu%%, %llu ciclos/poligono) | "
                       "hash das listas %llu%% | espera DMA do GS %llu%%\n",
                       (unsigned long long)(g_ps2_gpu_ciclos - gpu_ant) * 100ull / t,
                       (g_prof_gsr_poli - p_a) * 100ull / t, pr ? (g_prof_gsr_poli - p_a) / pr : 0ull,
                       (g_prof_gpu_hash - h_a) * 100ull / t, (g_prof_espera_gs - e_a) * 100ull / t);
                h_a = g_prof_gpu_hash; p_a = g_prof_gsr_poli; e_a = g_prof_espera_gs; pr_a = g_gsr_prims;
            }
            printf("perfil: GPU %llu%%, copia %llu%% | pixels: tri %lu (tex %lu), ret %lu, semi %lu\n",
                   (unsigned long long)(g_ps2_gpu_ciclos - gpu_ant) * 100ull / t,
                   (unsigned long long)(g_ps2_video_ciclos - vid_ant) * 100ull / t,
                   g_ps2_px_tri, g_ps2_px_tri_tex, g_ps2_px_ret, g_ps2_px_semi);
            {
                { extern unsigned long g_ps2_listas_puladas, g_ps2_listas_total, g_ps2_listas_ciclicas,
                                       g_ps2_listas_grandes, g_ps2_quadros_novos;
                  printf("perfil: listas %lu (grandes %lu, trocas de endereco da lista grande %lu), puladas %lu, com ciclo (total) %lu\n",
                         g_ps2_listas_total, g_ps2_listas_grandes, g_ps2_quadros_novos, g_ps2_listas_puladas,
                         g_ps2_listas_ciclicas);
                  g_ps2_listas_grandes = 0; g_ps2_quadros_novos = 0;
                  { extern unsigned long g_ps2_redesenhos, g_ps2_otcs, g_ps2_redesenhos_pulados, g_ps2_nos_percorridos;
                    printf("perfil: OTs limpas pelo jogo (DMA6, ~quadros montados) %lu, listas grandes sem limpeza antes %lu, redesenhos iguais pulados %lu, nos percorridos %lu\n",
                           g_ps2_otcs, g_ps2_redesenhos, g_ps2_redesenhos_pulados, g_ps2_nos_percorridos);
                    g_ps2_nos_percorridos = 0;
                    g_ps2_otcs = g_ps2_redesenhos = g_ps2_redesenhos_pulados = 0; }
                  { extern unsigned long g_ps2_desp_buscas, g_ps2_desp_acertos;
                    printf("perfil: despachos de funcao %lu (cache %lu, busca na tabela %lu)\n",
                           g_ps2_desp_buscas + g_ps2_desp_acertos, g_ps2_desp_acertos, g_ps2_desp_buscas);
                    g_ps2_desp_buscas = g_ps2_desp_acertos = 0; }
                  g_ps2_listas_total = g_ps2_listas_puladas = 0; }
                { extern unsigned long g_gsr_paginas, g_gsr_envios, g_gsr_envio_px, g_gsr_flushes, g_gsr_prims;
                  extern unsigned long long g_ps2_gpu_ciclos;
                  static unsigned long long gc_ant;
                  printf("perfil: gs paginas %lu, uploads %lu (%lu px), flushes %lu, poligonos %lu, ciclos GPU/poligono %llu\n",
                         g_gsr_paginas, g_gsr_envios, g_gsr_envio_px, g_gsr_flushes, g_gsr_prims,
                         g_gsr_prims ? (g_ps2_gpu_ciclos - gc_ant) / g_gsr_prims : 0ull);
                  gc_ant = g_ps2_gpu_ciclos;
                  g_gsr_paginas = g_gsr_envios = g_gsr_envio_px = g_gsr_flushes = g_gsr_prims = 0; }
                extern unsigned long g_ps2_pad_trocas, g_ps2_pad_leituras;
                printf("perfil: pad trocas=%lu leituras=%lu\n", g_ps2_pad_trocas, g_ps2_pad_leituras);
            }
            printf("perfil: ret solido %lu, ret tex rapido %lu (bruto %lu), ret lento %lu\n",
                   g_ps2_px_ret_solido, g_ps2_px_ret_tex, g_ps2_px_ret_bruto, g_ps2_px_ret_lento);
        }
        g_ps2_px_ret_solido = g_ps2_px_ret_tex = g_ps2_px_ret_lento = g_ps2_px_ret_bruto = 0;
        g_ps2_px_tri = g_ps2_px_tri_tex = g_ps2_px_ret = g_ps2_px_semi = 0;
        total_ant = total; gpu_ant = g_ps2_gpu_ciclos; vid_ant = g_ps2_video_ciclos;
    }
    {   /* V32b: gravar no pendrive trava ~50 ms (log V33 do console: maior
         * intervalo sem interrupção 50-60 ms em todo relatório). Grava só a
         * cada PS2_LOG_BLOCOS relatórios ou com o buffer pela metade. */
        static unsigned blocos;
        if (++blocos >= (unsigned)PS2_LOG_BLOCOS ||
            kit_log_pendente() > kit_log_capacidade() / 2u) {
            log_pendente = 1;           /* gravado em ps2_video_vblank (ver lá) */
            blocos = 0;
        }
    }
    printf("video: exib=%d x=%u y=%u %ux%u 24b=%d | VRAM com cor: %u/%u amostras | GP0: %lu palavras\n",
           d->ativo, d->x, d->y, d->largura, d->altura, d->cor24, acesos, total,
           g_ps2_gpu_palavras);
}


void ps2_video_vblank(void)
{
    uint32_t tp0;
    {
        static uint32_t ant;
        uint32_t agora;
        __asm__ volatile("mfc0 %0, $9" : "=r"(agora));
        if (ant) g_ps2_ee_total += (uint32_t)(agora - ant);
        ant = agora;
    }
    tp0 = agora_ee();
    {   /* controle do PS2 (kit) -> controle do PS1 (pad.c) */
        extern uint16_t ps2_pad_botoes[2];
        kit_controle_estado e;
        kit_controle_ler(0, &e);
        ps2_pad_botoes[0] = e.botoes;   /* 0xFFFF se desconectado */
    }
    g_prof_pad += (uint32_t)(agora_ee() - tp0);
    {   /* L3 + R3: liga/desliga o painel de desempenho */
        extern uint16_t ps2_pad_botoes[2];
        static int antes;
        int agora = (ps2_pad_botoes[0] & 0x0006u) == 0u;
        if (agora && !antes && g_gsr_ativo) gsr_painel_alternar();
        antes = agora;
    }
    /* V23: os atalhos SELECT+R3 (sem som) e SELECT+L3 (pulo de redesenho)
     * saíram — no console o SELECT+R3 desligou o pulo de redesenho por
     * engano e a luta ficou mais lenta; o som custa só ~1%. */
#ifdef PS2_AUTO_START
    {   /* teste sem controle: a partir de 20 s, a cada 1 s aperta por 0,2 s,
         * alternando START e X (pula vídeos, título, menus e seleção) */
        extern uint16_t ps2_pad_botoes[2];
        static unsigned n_vb;
        n_vb++;
        if (n_vb > 1200u && n_vb % 60u < 12u) {
            /* depois de 35 s só X: START no meio da luta pausa o jogo */
            int start = n_vb < 2100u && ((n_vb / 60u) & 1u);
            ps2_pad_botoes[0] &= start ? 0xFFF7u : 0xBFFFu;
            if (n_vb % 600u == 0u) printf("auto: %s\n", start ? "START" : "X");
        }
    }
#endif
    Ps2GpuDisplay d = ps2_gpu_display();
    const uint16_t *vram = ps2_gpu_vram();
    unsigned x, y;
    static unsigned quadro;
#ifdef PS2_DEBUG_VRAM
    salvar_vram(vram);
#endif
    tp0 = agora_ee();
    diagnostico(&d, vram);
    g_prof_diag += (uint32_t)(agora_ee() - tp0);
    talvez_gravar_log();
    /* Com o GS desenhando, exibir é só um sprite da VRAM no GS para a tela
     * (todo VBlank). Sem GS (ou 24 bits), copia da sombra a cada 3 VBlanks. */
    if (g_gsr_ativo && !(d.ativo && d.cor24)) {
#ifdef PS2_VER_VRAM
        gsr_exibir(gs->ScreenBuffer[0], TELA_LARGURA / 64u, gs->PSM, TELA_LARGURA, (unsigned)gs->Height, 0, 0, 1020, 512);
#else
        tp0 = agora_ee();
        if (d.ativo)
            gsr_exibir(gs->ScreenBuffer[0], TELA_LARGURA / 64u, gs->PSM, TELA_LARGURA, (unsigned)gs->Height,
                       d.x, d.y, d.largura, d.altura);
        g_prof_exibir += (uint32_t)(agora_ee() - tp0);
#endif
        (void)x; (void)y;
        return;
    }
    if (++quadro % 3u) return;
    {
        static unsigned long t_copia, t_gs, n_med;
        uint32_t t0, t1, t2;
        __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        if (d.ativo) copiar_tela(&d, vram);
        __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
        if (g_gsr_ativo) { gsr_flush(); dmaKit_wait(DMA_CHANNEL_GIF, 0); }
        gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
        if (d.ativo) {
            FlushCache(0);
            gsKit_texture_upload(gs, &textura_gs);
            gsKit_prim_sprite_texture(gs, &textura_gs, 0, 0, 0, 0, TELA_LARGURA, TELA_ALTURA,
                (float)d.largura, (float)d.altura, 0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
        }
        gsKit_queue_exec(gs);
        dmaKit_wait_fast();
        if (g_gsr_ativo) gsr_estado_perdido();
        __asm__ volatile("mfc0 %0, $9" : "=r"(t2));
        t_copia += t1 - t0; t_gs += t2 - t1;
        g_ps2_video_ciclos += t2 - t0;
        if (++n_med == 40u) {
            printf("video: tempo medio por quadro: copia %lu us, GS %lu us\n",
                   t_copia / 40u / 295u, t_gs / 40u / 295u);
            t_copia = t_gs = 0; n_med = 0;
        }
    }
}


