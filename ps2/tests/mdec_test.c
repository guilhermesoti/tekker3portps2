/* mdec_test.c — MDEC: tabelas, macrobloco 15/24 bits e DMA de saída. */
#include "../runtime/mdec.h"
#include "../runtime/ram.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int prontas;
void ps2_dma_mdec_saida_pronta(void) { prontas++; }

#define CHECA(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void tabelas(void)
{
    uint16_t esc[64];
    int u, x;
    ps2_mdec_write_data(2u << 29 | 1u);            /* luma + croma */
    for (x = 0; x < 32; x++) ps2_mdec_write_data(0x08080808u);  /* qt = 8 */
    for (u = 0; u < 8; u++)
        for (x = 0; x < 8; x++)
            esc[u * 8 + x] = (uint16_t)(int16_t)lround(u == 0 ? 23170.0 :
                32768.0 * cos((2 * x + 1) * u * M_PI / 16.0));
    ps2_mdec_write_data(3u << 29);
    for (x = 0; x < 32; x++)
        ps2_mdec_write_data((uint32_t)esc[2 * x] | (uint32_t)esc[2 * x + 1] << 16);
}

/* Macrobloco com DC de luma = 64 (pixel +64) e croma 0, q_scale=1. */
static void macrobloco(uint32_t prof, uint32_t bit15)
{
    uint16_t h[12];
    int i;
    h[0] = (1u << 10) | 0; h[1] = 0xFE00u;          /* Cr */
    h[2] = (1u << 10) | 0; h[3] = 0xFE00u;          /* Cb */
    for (i = 0; i < 4; i++) { h[4 + 2 * i] = (1u << 10) | 64u; h[5 + 2 * i] = 0xFE00u; }
    ps2_mdec_write_data(1u << 29 | prof << 27 | bit15 << 25 | 6u);
    for (i = 0; i < 6; i++) ps2_mdec_write_data((uint32_t)h[2 * i] | (uint32_t)h[2 * i + 1] << 16);
}

int main(void)
{
    uint32_t w, a;
    ps2_mdec_reset();
    tabelas();
    CHECA((ps2_mdec_read_status() & 0xFFFFu) == 0xFFFFu);

    /* 15 bits com bit 15: 64 -> 0xC0 sem sinal -> 24 por canal */
    macrobloco(3u, 1u);
    w = ps2_mdec_read_data();
    CHECA(w == 0xE318E318u);   /* 0x8000 | 24 | 24<<5 | 24<<10 = 0xE318 */

    /* 24 bits: 768 bytes = 192 palavras de 0xC0 */
    ps2_mdec_reset();
    macrobloco(2u, 0u);
    /* DMA1 pedido antes de haver dados suficientes: fica pendente */
    ps2_mdec_reset();
    CHECA(ps2_mdec_dma_out(0x80010000u, 192u) == 0);
    macrobloco(2u, 0u);
    CHECA(prontas == 1);
    for (a = 0; a < 192u; a++) CHECA(ps2_ram_get32(0x80010000u + a * 4u) == 0xC0C0C0C0u);
    CHECA(ps2_mdec_read_status() & (1u << 31));     /* saída vazia */

    /* DMA0: comando + parâmetros vindos da RAM */
    {
        uint16_t h[12];
        int i;
        h[0] = (1u << 10); h[1] = 0xFE00u; h[2] = (1u << 10); h[3] = 0xFE00u;
        for (i = 0; i < 4; i++) { h[4 + 2 * i] = (1u << 10) | (uint16_t)(-64 & 0x3FF); h[5 + 2 * i] = 0xFE00u; }
        ps2_ram_set32(0x80020000u, 1u << 29 | 2u << 27 | 6u);
        for (i = 0; i < 6; i++)
            ps2_ram_set32(0x80020004u + (uint32_t)i * 4u, (uint32_t)h[2 * i] | (uint32_t)h[2 * i + 1] << 16);
        ps2_mdec_dma_in(0x80020000u, 7u);
        CHECA(ps2_mdec_dma_out(0x80030000u, 192u) == 1);
        CHECA(ps2_ram_get32(0x80030000u) == 0x40404040u);   /* -64 ^ 0x80 */
    }
    printf("MDEC: tabelas, macroblocos 15/24 bits e DMA OK\n");
    return 0;
}
