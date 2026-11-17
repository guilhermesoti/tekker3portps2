/* Pulo de redesenho igual (io.c) — casos da revisão do ChatGPT (V16, P1).
 * Stubs contam palavras enviadas à GPU. */
#include <assert.h>
#include "../runtime/io.c"

uint8_t g_ps1_ram[PS2_RAM_SIZE];
uint32_t g_cod_ini, g_cod_tam;
uint8_t g_cod_sujo[4096];
uint32_t g_ps2_vram_geracao;
static unsigned long palavras;
void ps2_gpu_gp0(uint32_t v) { (void)v; palavras++; }
void ps2_gpu_gp0_bloco(const uint32_t *p, uint32_t n) { (void)p; palavras += n; }
uint32_t ps2_gpu_read(void) { return 0; }
void ps2_mdec_dma_in(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); }
int ps2_mdec_dma_out(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); return 0; }
void ps2_cdrom_dma(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); }

static void montar(uint32_t base, uint32_t cor)      /* 300 nós, 2 palavras cada */
{
    uint32_t i;
    for (i = 0; i < 300; i++) {
        uint32_t a = base + i * 12u;
        ps2_ram_set32(a, i == 299 ? 0x02FFFFFFu : (0x02000000u | (a + 12u)));
        ps2_ram_set32(a + 4, 0x02000000u | cor);
        ps2_ram_set32(a + 8, 0x00000000u);
    }
}
static unsigned long disparar(uint32_t madr)
{
    palavras = 0;
    reg_set(0xA0u, madr, 4);
    dma_chcr_write(0xA8u, 0x01000401u);
    assert(!(reg_get(0xA8u, 4) & (1u << 24)));
    return palavras;
}

int main(void)
{
    memset(g_ps1_ram, 0, sizeof g_ps1_ram);
    montar(0x10000, 0x0000FF);                  /* A: vermelho */
    montar(0x20000, 0x00FF00);                  /* B */
    assert(disparar(0x10000) == 600);           /* 1a vez desenha */
    assert(disparar(0x10000) == 0);             /* igual: pula */
    ps2_ram_set32(0x10004, 0x0200FF00u);        /* muda só um COMANDO (cor) */
    assert(disparar(0x10000) == 600);           /* conteúdo mudou: desenha */
    assert(disparar(0x10000) == 0);
    assert(disparar(0x20000) == 600);           /* B */
    assert(disparar(0x10000) == 600);           /* A depois de B: desenha */
    g_ps2_vram_geracao++;                       /* envio para a VRAM */
    assert(disparar(0x10000) == 600);
    dedup_ligado = 0;
    assert(disparar(0x10000) == 600);           /* desligado: sempre desenha */
    dedup_ligado = 1;
    assert(disparar(0x10000) == 0);             /* a desenhada sem dedup vale como referência */
    puts("dedup_test: OK");
    return 0;
}
