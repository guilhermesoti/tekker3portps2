/* io.c — ver io.h. */
#include "io.h"
#include "cpu_state.h"
#include "ram.h"
#include "cdrom.h"
#include "pad.h"
#include "mdec.h"
#include "gpu.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define IO_BASE  0x1F801000u
#define IO_SIZE  0x2000u          /* 0x1F801000-0x1F802FFF */

static uint8_t io_regs[IO_SIZE] __attribute__((aligned(16)));
static uint8_t io_visto[IO_SIZE / 2];   /* bit0: lido, bit1: escrito (por half) */

static uint32_t i_stat, i_mask;

/* ---------------- relógio ---------------- */

#ifdef _EE
/* COUNT do COP0 do EE: 294,912 MHz. PS1: 33,8688 MHz -> razão ~8,7075.
 * Multiplica por 0,11484 (~= 7527/65536) para converter. */
static uint32_t ee_count(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}
static uint32_t ultimo_ee, acumulado_ps1;
static uint32_t resto;
uint32_t ps2_ps1_cycles(void)
{
    uint32_t agora = ee_count();
    uint32_t delta = agora - ultimo_ee;
    uint64_t t;
    ultimo_ee = agora;
    t = (uint64_t)delta * 7527u + resto;
    acumulado_ps1 += (uint32_t)(t >> 16);
    resto = (uint32_t)(t & 0xFFFFu);
    return acumulado_ps1;
}
#else
uint32_t ps2_ps1_cycles(void)   /* PC (testes): usa clock() */
{
    return (uint32_t)((uint64_t)clock() * 33868800u / CLOCKS_PER_SEC);
}
#endif

/* ---------------- nomes (só para o log) ---------------- */

static const char *nome_io(uint32_t off)
{
    uint32_t a = IO_BASE + off;
    if (a < 0x1F801024) return "MEMCTRL";
    if (a >= 0x1F801040 && a < 0x1F801050) return "JOY/PAD";
    if (a >= 0x1F801050 && a < 0x1F801060) return "SIO";
    if (a == 0x1F801060) return "RAM_SIZE";
    if (a == 0x1F801070) return "I_STAT";
    if (a == 0x1F801074) return "I_MASK";
    if (a >= 0x1F801080 && a < 0x1F8010F0) return "DMA canal";
    if (a == 0x1F8010F0) return "DMA DPCR";
    if (a == 0x1F8010F4) return "DMA DICR";
    if (a >= 0x1F801100 && a < 0x1F801130) return "TIMER";
    if (a >= 0x1F801800 && a < 0x1F801804) return "CDROM";
    if (a == 0x1F801810) return "GPU GP0/READ";
    if (a == 0x1F801814) return "GPU GP1/STAT";
    if (a >= 0x1F801820 && a < 0x1F801828) return "MDEC";
    if (a >= 0x1F801C00 && a < 0x1F802000) return "SPU";
    if (a >= 0x1F802000) return "EXPANSAO2";
    return "?";
}

static void anotar(uint32_t off, int escrita, uint32_t valor)
{
    uint32_t i = off >> 1;
    uint8_t bit = escrita ? 2u : 1u;
    if (io_visto[i] & bit)
        return;
    io_visto[i] |= bit;
    printf("io: 1a %s %-13s %08lx = %08lx\n", escrita ? "escrita" : "leitura",
           nome_io(off), (unsigned long)(IO_BASE + off), (unsigned long)valor);
}

/* ---------------- registradores genéricos ---------------- */

static uint32_t reg_get(uint32_t off, unsigned w)
{
    if (w == 4) return *(ps2_u32a *)&io_regs[off & ~3u];
    if (w == 2) return *(ps2_u16a *)&io_regs[off & ~1u];
    return io_regs[off];
}
static void reg_set(uint32_t off, uint32_t v, unsigned w)
{
    if (w == 4) *(ps2_u32a *)&io_regs[off & ~3u] = v;
    else if (w == 2) *(ps2_u16a *)&io_regs[off & ~1u] = (uint16_t)v;
    else io_regs[off] = (uint8_t)v;
}

/* ---------------- interrupções ---------------- */

void ps2_irq_raise(unsigned bit) { i_stat |= 1u << bit; }
void ps2_irq_ack(unsigned bit)   { i_stat &= ~(1u << bit); }
uint32_t ps2_irq_pending(void)   { return i_stat & i_mask & 0x7FFu; }

/* ---------------- timers ---------------- */

typedef struct {
    uint32_t base;      /* ciclos PS1 no último reset */
    uint16_t modo, alvo;
    uint32_t marcas;    /* alvo (16 bits baixos) e overflow (altos) já vistos */
    uint32_t irq_marcas;/* idem, só dos eventos que geram IRQ */
    uint8_t  irq_feita; /* modo "one-shot": já gerou a IRQ */
} Timer;
static Timer tmr[3];

static uint32_t timer_divisor(int t)
{
    uint32_t src = (tmr[t].modo >> 8) & 3u;
    if (t == 1 && (src & 1u)) return 2152u;        /* hblank (NTSC ~ 63,5 us) */
    if (t == 2 && (src & 2u)) return 8u;           /* sistema / 8 */
    if (t == 0 && (src & 1u)) return 5u;           /* dotclock (aprox.) */
    return 1u;
}

static uint16_t timer_valor(int t)
{
    uint32_t c = (ps2_ps1_cycles() - tmr[t].base) / timer_divisor(t);
    if ((tmr[t].modo & (1u << 3)) && tmr[t].alvo)   /* reset no alvo */
        c %= (uint32_t)tmr[t].alvo + 1u;
    return (uint16_t)c;
}

static uint32_t timer_read(uint32_t off)
{
    int t = (int)((off - 0x100u) >> 4);
    switch (off & 0xFu) {
    case 0x0: return timer_valor(t);
    case 0x4: { uint32_t m = tmr[t].modo | (1u << 10);  /* sem IRQ pendente */
                tmr[t].modo &= ~((1u << 11) | (1u << 12)); return m; }
    case 0x8: return tmr[t].alvo;
    }
    return 0;
}

/* Marcas (quantas vezes o alvo / o overflow foram cruzados) no instante 'agora'. */
static void timer_marcas(int t, uint32_t agora, uint32_t *m_alvo, uint32_t *m_ovf)
{
    uint32_t alvo = tmr[t].alvo;
    uint32_t ticks = (agora - tmr[t].base) / timer_divisor(t);
    *m_alvo = 0; *m_ovf = 0;
    if ((tmr[t].modo & (1u << 3)) && alvo) {
        *m_alvo = (ticks + 1u) / (alvo + 1u);
    } else {
        *m_alvo = ticks >= alvo ? (ticks - alvo) / 0x10000u + 1u : 0u;
        *m_ovf = (ticks + 1u) / 0x10000u;
    }
}

/* Depois de escrever no contador ou no alvo: as marcas recomeçam do estado
 * atual, sem IRQ pelo recuo (auditoria V19 1.2: antes zerar o contador
 * disparava uma IRQ falsa na hora). */
static void timer_sincronizar(int t, uint32_t agora)
{
    uint32_t ma, mo, modo = tmr[t].modo;
    timer_marcas(t, agora, &ma, &mo);
    tmr[t].marcas = (ma & 0xFFFFu) | ((mo & 0xFFFFu) << 16);
    tmr[t].irq_marcas = (((modo & (1u << 4)) ? ma : 0u) & 0xFFFFu) |
                        ((((modo & (1u << 5)) ? mo : 0u) & 0xFFFFu) << 16);
}

static void timer_write(uint32_t off, uint32_t v)
{
    int t = (int)((off - 0x100u) >> 4);
    uint32_t agora = ps2_ps1_cycles();
    static unsigned n_dbg;
    if (n_dbg < 20u && (off & 0xFu) != 0u) {
        n_dbg++;
        printf("timer%d: %s = %04lx\n", t, (off & 0xFu) == 4u ? "modo" : "alvo",
               (unsigned long)v);
    }
    switch (off & 0xFu) {
    case 0x0: tmr[t].base = agora - (v & 0xFFFFu) * timer_divisor(t);
              timer_sincronizar(t, agora); break;
    case 0x4: tmr[t].modo = (uint16_t)(v & 0x3FFu); tmr[t].base = agora;
              tmr[t].marcas = 0; tmr[t].irq_marcas = 0; tmr[t].irq_feita = 0; break;
    case 0x8: tmr[t].alvo = (uint16_t)v; timer_sincronizar(t, agora); break;
    }
}

/*
 * Gera as IRQs dos timers. Alvo e overflow são contados separadamente
 * (achado F5): com "reset no alvo" o contador volta a 0 depois do alvo;
 * sem ele, o alvo é cruzado uma vez a cada 0x10000 contagens e o overflow
 * acontece em 0xFFFF. Marca também os bits 11 (alvo) e 12 (overflow) do modo.
 */
void ps2_timers_poll(void)
{
    static const unsigned irq[3] = { IRQ_TMR0, IRQ_TMR1, IRQ_TMR2 };
    uint32_t agora = ps2_ps1_cycles();
    int t;

    for (t = 0; t < 3; t++) {
        uint32_t modo = tmr[t].modo;
        uint32_t m_alvo, m_ovf, marcas;
        if (t == 2 && (modo & 1u) && ((modo >> 1) & 3u) % 3u == 0u)
            continue;                                   /* timer 2 parado */
        timer_marcas(t, agora, &m_alvo, &m_ovf);
        if (m_alvo != (tmr[t].marcas & 0xFFFFu)) modo |= 1u << 11;
        if (m_ovf != (tmr[t].marcas >> 16)) modo |= 1u << 12;
        tmr[t].modo = (uint16_t)modo;
        marcas = (((modo & (1u << 4)) ? m_alvo : 0u) & 0xFFFFu) |
                 ((((modo & (1u << 5)) ? m_ovf : 0u) & 0xFFFFu) << 16);
        if (marcas != (tmr[t].irq_marcas)) {
            tmr[t].irq_marcas = marcas;
            if ((modo & (1u << 6)) || !tmr[t].irq_feita) {   /* repetir ou 1ª vez */
                tmr[t].irq_feita = 1;
                ps2_irq_raise(irq[t]);
            }
        }
        tmr[t].marcas = (m_alvo & 0xFFFFu) | ((m_ovf & 0xFFFFu) << 16);
    }
}

/* ---------------- DMA ---------------- */

static uint32_t dma_dicr;

/* Comandos que só desenham (podem ser pulados): preenchimento 0x02 e
 * polígonos/linhas/retângulos 0x20..0x7F. Estado (0xE1..0xE6) e
 * transferências de VRAM sempre são executados. */
static int desenho_pulavel(uint32_t op)
{
    return op == 0x02u || (op - 0x20u) < 0x60u;
}

/*
 * Orçamento da GPU. No PS1 a GPU desenha em paralelo com a CPU; aqui ela
 * roda no próprio EE e, como a libgpu dispara a próxima lista na interrupção
 * de fim de DMA, o desenho inteiro acontecia dentro da interrupção e o laço
 * principal do jogo ficava sem tempo (título lento, cronômetro parado).
 * Solução: a GPU pode usar até ORCAMENTO_GPU % do tempo; quando passa
 * disso, a lista seguinte só aplica estado e transferências (quadro pulado).
 */
/* Vale também com o GS: no PS2 real interpretar os comandos custa bem mais
 * que no PCSX2, e sem o orçamento o jogo inteiro fica lento (não só a imagem). */
#define ORCAMENTO_GPU 50u
#ifdef _EE
static int64_t  gpu_divida;            /* ciclos do EE acima do orçamento */
static uint32_t gpu_t_ini, gpu_janela_ini;
static uint32_t gpu_usado;             /* ciclos de GPU na janela (VBlank) atual */
static int      pular_quadro;
unsigned long   g_ps2_listas_puladas, g_ps2_listas_total;
/* Chamado a cada VBlank: decide se o PRÓXIMO quadro inteiro é desenhado. */
void ps2_gpu_orcamento_vblank(void)
{
    uint32_t agora = ee_count(), janela = agora - gpu_janela_ini;
    gpu_janela_ini = agora;
    gpu_divida += (int64_t)gpu_usado - (int64_t)((uint64_t)janela * ORCAMENTO_GPU / 100u);
    if (gpu_divida < 0) gpu_divida = 0;
    gpu_usado = 0;
    /* tolerância de 1 VBlank inteiro: o jogo desenha em rajadas */
    pular_quadro = gpu_divida > (int64_t)janela;
}
int ps2_gpu_pular_lista(void)
{
    gpu_t_ini = ee_count();
    g_ps2_listas_total++;
    if (pular_quadro) g_ps2_listas_puladas++;
    return pular_quadro;
}
void ps2_gpu_fim_lista(int pulou)
{
    gpu_usado += (uint32_t)(ee_count() - gpu_t_ini);
    (void)pulou;
}
#else
void ps2_gpu_orcamento_vblank(void) {}
int  ps2_gpu_pular_lista(void) { return 0; }
void ps2_gpu_fim_lista(int pulou) { (void)pulou; }
#endif

/*
 * Listas do DMA2 com ciclo. No PS1 o jogo nunca manda uma (a GPU ficaria
 * presa); se aparecer aqui, é efeito do port, e antes ela era percorrida até
 * 524.288 nós, reenviando os mesmos comandos centenas de milhares de vezes
 * (luta parada no console aos ~7 min 30 s, V11). Agora a cadeia é validada
 * antes (Floyd, só lê os cabeçalhos), cada nó é desenhado uma vez, o DMA é
 * concluído e a primeira ocorrência fica registrada no log com o contexto.
 */
unsigned long g_ps2_listas_ciclicas;
unsigned long g_ps2_listas_grandes;
unsigned long g_ps2_quadros_novos;
unsigned long g_ps2_redesenhos, g_ps2_otcs;   /* lista grande sem OTC (limpeza da OT) antes */
static int g_ps2_otc_desde_grande;
unsigned long g_ps2_redesenhos_pulados;
/* assinatura da ÚLTIMA lista grande desenhada (ver dma_gpu) */
static struct { int valida; uint32_t madr, nos, hash, geracao; } sig;
static struct { int ok; uint32_t madr, nos, hash; } sig_cand;
int dedup_ligado = 1;                   /* 0 = sempre desenha (teste A/B) */
unsigned long g_ps2_nos_percorridos;
extern uint32_t g_ps2_vram_geracao;
static uint32_t ultimas_madr[4], ultima_otc_madr, ultima_otc_n;
static unsigned n_madr;
extern CPUState g_ps2_cpu __attribute__((weak));

static uint32_t proximo_no(uint32_t a)
{
    uint32_t c = ps2_ram_get32(a);
    return (c & 0x800000u) ? 0xFFFFFFFFu : (c & 0x1ffffcu);
}

/* 0 = sem ciclo; senão devolve 1 e o número de nós a desenhar (mu + lambda) */
static int lista_ciclica(uint32_t a0, uint32_t *mu_out, uint32_t *lambda_out)
{
    uint32_t t = a0, h = a0, mu = 0, lambda = 1, p, l;
    for (;;) {                                  /* Floyd */
        h = proximo_no(h); if (h == 0xFFFFFFFFu) return 0;
        h = proximo_no(h); if (h == 0xFFFFFFFFu) return 0;
        t = proximo_no(t);
        if (t == h) break;
    }
    t = a0;                                     /* início do ciclo */
    while (t != h) { t = proximo_no(t); h = proximo_no(h); mu++; }
    p = proximo_no(t);                          /* comprimento */
    for (l = 1; p != t; l++) p = proximo_no(p);
    lambda = l;
    *mu_out = mu; *lambda_out = lambda;
    return 1;
}

static void registrar_ciclo(uint32_t a0, uint32_t mu, uint32_t lambda)
{
    uint32_t a = a0, i;
    g_ps2_listas_ciclicas++;
    if (g_ps2_listas_ciclicas > 3u) return;     /* evidência só das primeiras */
    printf("gpu: ERRO lista DMA2 com ciclo #%lu: inicio %06lx, %lu nos ate o ciclo, ciclo de %lu nos\n",
           g_ps2_listas_ciclicas, (unsigned long)a0, (unsigned long)mu, (unsigned long)lambda);
    if (&g_ps2_cpu)
        printf("gpu:   cpu pc=%08lx ra=%08lx sp=%08lx a0=%08lx a1=%08lx\n",
               (unsigned long)g_ps2_cpu.pc, (unsigned long)g_ps2_cpu.gpr[31],
               (unsigned long)g_ps2_cpu.gpr[29], (unsigned long)g_ps2_cpu.gpr[4],
               (unsigned long)g_ps2_cpu.gpr[5]);
    printf("gpu:   listas anteriores %06lx %06lx %06lx %06lx | ultima OTC %06lx n=%lu\n",
           (unsigned long)ultimas_madr[(n_madr + 3u) & 3u], (unsigned long)ultimas_madr[(n_madr + 2u) & 3u],
           (unsigned long)ultimas_madr[(n_madr + 1u) & 3u], (unsigned long)ultimas_madr[n_madr & 3u],
           (unsigned long)ultima_otc_madr, (unsigned long)ultima_otc_n);
    for (i = 0; i < mu; i++) a = proximo_no(a);
    for (i = 0; i < lambda + 1u && i < 8u; i++) {   /* nós do ciclo */
        uint32_t c = ps2_ram_get32(a);
        printf("gpu:   no %06lx: cab %08lx (%lu palavras) cmd %08lx %08lx\n",
               (unsigned long)a, (unsigned long)c, (unsigned long)(c >> 24),
               (unsigned long)ps2_ram_get32((a + 4u) & 0x1ffffcu),
               (unsigned long)ps2_ram_get32((a + 8u) & 0x1ffffcu));
        a = proximo_no(a);
    }
}

static int dma_gpu(uint32_t off, uint32_t madr, uint32_t bcr, uint32_t chcr)
{
    uint32_t a = madr & 0x1ffffcu;
    uint32_t modo = (chcr >> 9) & 3u;
    if (modo == 2u) {
        uint32_t nos = 0, limite = 0xFFFFFFFFu, mu, lambda;
        int pular;
        if (!(chcr & 1u)) {
            printf("gpu: DMA linked list com direcao invalida\n");
            return 0;
        }
        {
            /*
             * Uma passada pela cadeia (Brent): detecta ciclo, conta os nós e
             * calcula um hash dos cabeçalhos E dos comandos de cada nó.
             */
            uint32_t t = a, h = a, poder = 1, lam = 0, cnt = 0, hash = 2166136261u;
            int ciclo = 0;
#ifdef _EE
            uint32_t th0 = ee_count();
#endif
            for (;;) {
                uint32_t c = ps2_ram_get32(h), n = c >> 24, i, q = h;
                cnt++;
                hash = (hash ^ c) * 16777619u;
                for (i = 0; i < n; i++) {
                    q = (q + 4u) & 0x1ffffcu;
                    hash = (hash ^ ps2_ram_get32(q)) * 16777619u;
                }
                if (c & 0x800000u) break;
                h = c & 0x1ffffcu;
                if (h == t) { ciclo = 1; break; }
                if (poder == ++lam) { t = h; poder <<= 1; lam = 0; }
            }
            g_ps2_nos_percorridos += cnt;
#ifdef _EE
            { extern unsigned long long g_prof_gpu_hash; g_prof_gpu_hash += (uint32_t)(ee_count() - th0); }
#endif
            if (ciclo && lista_ciclica(a, &mu, &lambda)) {
                registrar_ciclo(a, mu, lambda);
                limite = mu + lambda;           /* cada nó uma vez */
            } else if (cnt >= 200u) {
                /*
                 * Redesenho da MESMA lista (modo entrelaçado 368x480: o jogo
                 * reenvia a OT atual a cada campo enquanto o quadro novo não
                 * fica pronto). Aqui a primeira vez já desenhou a tela
                 * inteira; repetir gastava 39-46% do EE. Só pula quando é
                 * exatamente a última lista grande desenhada (inteira, sem
                 * pulos do orçamento), com o mesmo conteúdo (hash de
                 * cabeçalhos + comandos), sem limpeza da OT (DMA6) e sem
                 * envio/cópia para a VRAM desde então. Ver revisão do ChatGPT
                 * (REVISAO_LENTIDAO_LUTA_V16, P1).
                 */
                if (dedup_ligado && sig.valida && sig.madr == a && sig.nos == cnt && sig.hash == hash &&
                    sig.geracao == g_ps2_vram_geracao) {
                    g_ps2_redesenhos_pulados++;
                    g_ps2_listas_grandes++;
                    reg_set(off - 8u, 0x00ffffffu, 4);
                    return 1;
                }
                sig.valida = 0;
                sig_cand.madr = a; sig_cand.nos = cnt; sig_cand.hash = hash; sig_cand.ok = 1;
            } else
                sig_cand.ok = 0;
        }
        ultimas_madr[n_madr++ & 3u] = a;
        pular = ps2_gpu_pular_lista();
        for (;;) {
            uint32_t cabecalho, n, i;
            if (++nos > limite) break;
            cabecalho = ps2_ram_get32(a);
            n = cabecalho >> 24;
            if (pular && n && desenho_pulavel(ps2_ram_get32((a + 4u) & 0x1ffffcu) >> 24)) {
                a = (a + 4u * n) & 0x1ffffcu;
                n = 0;
            }
            if (n && a + 4u + 4u * n <= 0x200000u) {
                ps2_gpu_gp0_bloco((const uint32_t *)&g_ps1_ram[a + 4u], n);
                a = (a + 4u * n) & 0x1ffffcu;
            } else
                for (i = 0; i < n; ++i) {
                    a = (a + 4u) & 0x1ffffcu;
                    ps2_gpu_gp0(ps2_ram_get32(a));
                }
            if (cabecalho & 0x800000u) break;
            a = cabecalho & 0x1ffffcu;
        }
        ps2_gpu_fim_lista(pular);
        if (sig_cand.ok) {                  /* desenhada agora: vira referência */
            sig.valida = !pular;            /* com pulos do orçamento não vale */
            sig.madr = sig_cand.madr; sig.nos = sig_cand.nos; sig.hash = sig_cand.hash;
            sig.geracao = g_ps2_vram_geracao;
            sig_cand.ok = 0;
        }
        if (nos >= 200u) {
            static uint32_t ultima_grande;
            g_ps2_listas_grandes++;
            if (madr != ultima_grande) { g_ps2_quadros_novos++; ultima_grande = madr; }
            if (!g_ps2_otc_desde_grande) g_ps2_redesenhos++;
            g_ps2_otc_desde_grande = 0;
        }
        reg_set(off - 8u, 0x00ffffffu, 4);
    } else if (modo <= 1u) {
        uint32_t palavras = bcr & 0xffffu;
        uint32_t blocos = bcr >> 16;
        uint64_t total;
        uint32_t passo = (chcr & 2u) ? (uint32_t)-4 : 4u;
        if (!palavras) palavras = 0x10000u;
        if (!blocos) blocos = 0x10000u;
        total = (uint64_t)palavras * (modo ? blocos : 1u);
        if (total > 0x1000000u) {
            printf("gpu: DMA bloco grande demais: %lu x %lu\n",
                   (unsigned long)palavras, (unsigned long)(modo ? blocos : 1u));
            return 0;
        }
        while (total--) {
            if (chcr & 1u) ps2_gpu_gp0(ps2_ram_get32(a));
            else ps2_ram_set32(a, ps2_gpu_read());
            a = (a + passo) & 0x1ffffcu;
        }
        reg_set(off - 8u, a, 4);
    } else {
        printf("gpu: modo DMA invalido\n");
        return 0;
    }
    return 1;
}

static uint32_t dma_palavras(uint32_t bcr, uint32_t chcr)
{
    uint32_t palavras = bcr & 0xFFFFu, blocos = bcr >> 16;
    if (palavras == 0) palavras = 0x10000u;
    if (((chcr >> 9) & 3u) == 1u) palavras *= blocos ? blocos : 1u;
    return palavras;
}

static void dma_ordering_table(uint32_t madr, uint32_t bcr)
{
    /* Canal 6 (OTC): limpa a ordering table de trás para a frente.
     * Cada entrada aponta para a anterior; a última é 0x00FFFFFF. */
    uint32_t n = bcr & 0xFFFFu;
    uint32_t a = madr & 0x1FFFFCu;
    if (n == 0) n = 0x10000u;
    ultima_otc_madr = a; ultima_otc_n = n;
    g_ps2_otc_desde_grande = 1; g_ps2_otcs++;
    if (sig.valida && a - sig.madr < n * 4u + 4u) sig.valida = 0;
    if ((n - 1u) * 4u <= a) {
        /* não dá a volta nos 2 MB: escreve direto e marca cada grânulo de
         * 64 bytes uma vez (antes marcava a cada palavra) */
        uint32_t ini = a - (n - 1u) * 4u, m;
        ps2_u32a *p = (ps2_u32a *)&g_ps1_ram[ini];
        uint32_t k;
        p[0] = 0x00FFFFFFu;
        for (k = 1; k < n; k++) p[k] = ini + (k - 1u) * 4u;
        for (m = ini & ~63u; m <= a; m += 64u) ps2_cod_marcar(m);
        return;
    }
    while (n--) {
        ps2_ram_set32(a, n ? ((a - 4u) & 0x1FFFFCu) : 0x00FFFFFFu);
        a = (a - 4u) & 0x1FFFFCu;
    }
}

static void dma_irq(uint32_t canal)
{
    if (dma_dicr & (1u << (16 + canal))) {
        dma_dicr |= 1u << (24 + canal);
        if (dma_dicr & (1u << 23))
            ps2_irq_raise(IRQ_DMA);
    }
}

static void dma_chcr_write(uint32_t off, uint32_t v)
{
    uint32_t canal = (off - 0x80u) >> 4;
    uint32_t madr = reg_get(off - 8u, 4), bcr = reg_get(off - 4u, 4);

    if (v & (1u << 24)) {                 /* start/busy */
        if (canal == 0)
            ps2_mdec_dma_in(madr, dma_palavras(bcr, v));
        else if (canal == 1 && !ps2_mdec_dma_out(madr, dma_palavras(bcr, v))) {
            reg_set(off, v, 4);           /* espera o MDEC: fica ocupado */
            return;
        }
        else if (canal == 2 && !dma_gpu(off, madr, bcr, v)) {
            reg_set(off, v, 4);
            return;
        }
        else if (canal == 6)
            dma_ordering_table(madr, bcr);
        else if (canal == 3) {            /* CD-ROM -> RAM */
            uint32_t bs = bcr & 0xFFFFu, bc = bcr >> 16;
            ps2_cdrom_dma(madr, bs * (bc ? bc : 1u));
        }
        /* SPU (canal 4) ainda não transfere. */
        v &= ~((1u << 24) | (1u << 28));
        dma_irq(canal);
    }
    reg_set(off, v, 4);
}

/* O MDEC terminou de entregar a saída que o DMA1 esperava. */
void ps2_dma_mdec_saida_pronta(void)
{
    reg_set(0x098, reg_get(0x098, 4) & ~((1u << 24) | (1u << 28)), 4);
    dma_irq(1);
}

static uint32_t dma_dicr_read(void);

static void dma_dicr_write(uint32_t v)
{
    uint32_t antes = dma_dicr_read() >> 31;
    /* bits 24-30 (flags) são limpos escrevendo 1 */
    uint32_t flags = (dma_dicr & ~v) & 0x7F000000u;
    dma_dicr = (v & 0x00FF803Fu) | flags;
    /* bit mestre passou de 0 para 1 na escrita: IRQ (achado F6) */
    if (!antes && (dma_dicr_read() >> 31))
        ps2_irq_raise(IRQ_DMA);
}

static uint32_t dma_dicr_read(void)
{
    uint32_t r = dma_dicr;
    if ((r & (1u << 15)) ||
        ((r & (1u << 23)) && (((r >> 16) & (r >> 24) & 0x7Fu) != 0)))
        r |= 1u << 31;
    return r;
}

/* ---------------- API ---------------- */

void ps2_io_init(void)
{
    memset(io_regs, 0, sizeof(io_regs));
    memset(io_visto, 0, sizeof(io_visto));
    memset(tmr, 0, sizeof(tmr));
    i_stat = i_mask = dma_dicr = 0;
    ps2_cdrom_reset();
    ps2_pad_reset();
    ps2_mdec_reset();
    ps2_gpu_reset();
}

int ps2_io_hit(uint32_t address)
{
    uint32_t seg = address >> 29;
    uint32_t phys = address & 0x1FFFFFFFu;
    if (!(seg == 0u || seg == 4u || seg == 5u))
        return 0;
    return phys - IO_BASE < IO_SIZE;
}

uint32_t ps2_io_read(uint32_t address, unsigned w)
{
    uint32_t off = (address & 0x1FFFFFFFu) - IO_BASE;
    uint32_t v;

    if (off == 0x070) { ps2_pad_tick(); v = i_stat; }
    else if (off == 0x074) v = i_mask;
    else if (off == 0x0F4) v = dma_dicr_read();
    else if (off >= 0x100 && off < 0x130) v = timer_read(off & ~1u);
    else if (off == 0x814) v = ps2_gpu_status();
    else if (off == 0x810) v = ps2_gpu_read();
    else if (off == 0x820) v = ps2_mdec_read_data();
    else if (off == 0x824) v = ps2_mdec_read_status();
    else if (off == 0xDAE)                      /* SPUSTAT espelha SPUCNT */
        v = reg_get(0xDAA, 2) & 0x3Fu;
    /* ENDX: sem SPU emulado, todas as vozes "já terminaram". Com 0 o driver
     * de som achava que nada acabava e a tela de título ficava esperando. */
    else if (off == 0xD9C) v = 0xFFFFu;
    else if (off == 0xD9E) v = 0x00FFu;
    else if (off >= 0x040 && off < 0x050) v = ps2_pad_read(off, w);
    else if (off >= 0x800 && off < 0x804) {    /* CD-ROM */
        v = ps2_cdrom_read(off - 0x800u);
        if (w > 1) v |= (uint32_t)ps2_cdrom_read(off + 1u - 0x800u) << 8;
    }
    else v = reg_get(off, w);

    if (w == 2) v &= 0xFFFFu;
    else if (w == 1) v &= 0xFFu;
    anotar(off, 0, v);
    return v;
}

void ps2_io_write(uint32_t address, uint32_t v, unsigned w)
{
    uint32_t off = (address & 0x1FFFFFFFu) - IO_BASE;

    anotar(off, 1, v);
    if (off == 0x070) {                                 /* ack: escreve 0 */
        /* escrita de 8/16 bits só alcança os bits que escreve */
        i_stat &= v | (w == 1 ? ~0xFFu : w == 2 ? ~0xFFFFu : 0u);
        return;
    }
    if (off == 0x074) { i_mask = v & 0x7FFu; return; }
    if (off == 0x0F4) { dma_dicr_write(v); return; }
    if (off >= 0x080 && off < 0x0F0 && (off & 0xFu) == 0x8u) {
        dma_chcr_write(off, v);
        return;
    }
    if (off >= 0x100 && off < 0x130) { timer_write(off & ~1u, v); return; }
    if (off >= 0x040 && off < 0x050) { ps2_pad_write(off, v, w); return; }
    if (off >= 0x800 && off < 0x804) { ps2_cdrom_write(off - 0x800u, (uint8_t)v); return; }
    if (off == 0x810) { ps2_gpu_gp0(v); return; }
    if (off == 0x814) { ps2_gpu_gp1(v); return; }
    if (off == 0x820) { ps2_mdec_write_data(v); return; }
    if (off == 0x824) { ps2_mdec_write_control(v); return; }
    reg_set(off, v, w);
}
