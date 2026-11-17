/* mdec.c — ver mdec.h. */
#include "mdec.h"
#include "ram.h"

#include <string.h>

#define ENTRADA_MAX 0x10000u            /* palavras de parâmetro (16 bits) */
#define SAIDA_MAX   (0x20000u)          /* palavras de saída (512 KB) */

static uint32_t entrada[ENTRADA_MAX];
static uint32_t saida[SAIDA_MAX];
static uint32_t sai_ini, sai_fim;       /* fila de saída: [ini, fim) */

static uint32_t cmd;                    /* comando atual */
static uint32_t faltam;                 /* parâmetros que ainda faltam */
static uint32_t recebidos;
static uint32_t controle;               /* bits 29/30 do registrador de controle */

static uint8_t  q_luma[64], q_croma[64];
static int16_t  escala[64];

static uint32_t pend_madr, pend_palavras;   /* DMA1 esperando dados */

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

void ps2_mdec_reset(void)
{
    cmd = 0; faltam = 0; recebidos = 0;
    sai_ini = sai_fim = 0;
    pend_palavras = 0;
}

/* ---------------- decodificação ---------------- */

static int s10(uint32_t n) { return (int)((n & 0x3FFu) ^ 0x200u) - 0x200; }
static int lim(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* Lê um bloco RLE (halfwords) a partir de *pos. 0 se os dados acabaram. */
static int rle_bloco(int16_t *blk, const uint8_t *qt, uint32_t *pos, uint32_t total)
{
    const uint16_t *src = (const uint16_t *)entrada;
    uint32_t n;
    int k, q, val;

    do {
        if (*pos >= total) return 0;
        n = src[(*pos)++];
    } while (n == 0xFE00u);
    memset(blk, 0, 64 * sizeof(int16_t));
    q = (int)((n >> 10) & 0x3Fu);
    k = 0;
    val = s10(n) * qt[0];
    for (;;) {
        if (q == 0) val = s10(n) * 2;
        val = lim(val, -0x400, 0x3FF);
        if (q) blk[zigzag[k]] = (int16_t)val;
        else   blk[k] = (int16_t)val;
        if (*pos >= total) break;
        n = src[(*pos)++];
        k += (int)((n >> 10) & 0x3Fu) + 1;
        if (k > 63) break;
        val = (s10(n) * qt[k] * q + 4) >> 3;
    }
    return 1;
}

static void idct(int16_t *blk)
{
    int32_t tmp[64];
    int x, y, z;
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            int32_t s = 0;
            for (z = 0; z < 8; z++) s += blk[y + z * 8] * (escala[x + z * 8] >> 3);
            tmp[x + y * 8] = (s + 0xFFF) >> 13;
        }
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            int32_t s = 0;
            for (z = 0; z < 8; z++) s += tmp[y + z * 8] * (escala[x + z * 8] >> 3);
            blk[x + y * 8] = (int16_t)((s + 0xFFF) >> 13);
        }
}

static uint8_t mb_rgb[16 * 16 * 3];     /* macrobloco 16x16 em R,G,B */

static void yuv(const int16_t *cr, const int16_t *cb, const int16_t *yb,
                int xx, int yy, int sem_sinal)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int c = ((x + xx) >> 1) + ((y + yy) >> 1) * 8;
            int R = cr[c], B = cb[c], Y = yb[x + y * 8];
            int G = (-352 * B - 731 * R) >> 10;
            uint8_t *o = &mb_rgb[((x + xx) + (y + yy) * 16) * 3];
            R = (1436 * R) >> 10;
            B = (1815 * B) >> 10;
            R = lim(Y + R, -128, 127); G = lim(Y + G, -128, 127); B = lim(Y + B, -128, 127);
            if (sem_sinal) { R ^= 0x80; G ^= 0x80; B ^= 0x80; }
            o[0] = (uint8_t)R; o[1] = (uint8_t)G; o[2] = (uint8_t)B;
        }
}

static void emitir_bytes(const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4u) {      /* n é sempre múltiplo de 4 */
        if (sai_fim >= SAIDA_MAX) return;
        saida[sai_fim++] = (uint32_t)b[i] | (uint32_t)b[i + 1] << 8 |
                           (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 3] << 24;
    }
}

static void decodificar(void)
{
    uint32_t prof = (cmd >> 27) & 3u;   /* 0=4 bits, 1=8, 2=24, 3=15 */
    int sem_sinal = !((cmd >> 26) & 1u);
    uint16_t bit15 = (cmd >> 25) & 1u ? 0x8000u : 0u;
    uint32_t pos = 0, total = recebidos * 2u;
    static int16_t blk[6][64];

    /* fila já consumida: recomeça do início; com sobra, move a sobra para o
     * início para a saída nova caber (auditoria V19 1.5) */
    if (sai_ini == sai_fim) sai_ini = sai_fim = 0;
    else if (sai_ini > 0u) {
        memmove(saida, saida + sai_ini, (size_t)(sai_fim - sai_ini) * 4u);
        sai_fim -= sai_ini;
        sai_ini = 0;
    }

    if (prof <= 1u) {                   /* monocromático: só blocos Y */
        while (rle_bloco(blk[0], q_luma, &pos, total)) {
            uint8_t b[64];
            int i;
            idct(blk[0]);
            for (i = 0; i < 64; i++) {
                int v = lim(blk[0][i], -128, 127);
                b[i] = (uint8_t)(sem_sinal ? v ^ 0x80 : v);
            }
            if (prof == 1u) emitir_bytes(b, 64);
            else {
                uint8_t p[32];
                for (i = 0; i < 32; i++) p[i] = (uint8_t)((b[2 * i] >> 4) | (b[2 * i + 1] & 0xF0u));
                emitir_bytes(p, 32);
            }
        }
        return;
    }
    for (;;) {
        int i;
        if (!rle_bloco(blk[0], q_croma, &pos, total)) break;   /* Cr */
        if (!rle_bloco(blk[1], q_croma, &pos, total)) break;   /* Cb */
        for (i = 2; i < 6; i++)
            if (!rle_bloco(blk[i], q_luma, &pos, total)) return;
        for (i = 0; i < 6; i++) idct(blk[i]);
        yuv(blk[0], blk[1], blk[2], 0, 0, sem_sinal);
        yuv(blk[0], blk[1], blk[3], 8, 0, sem_sinal);
        yuv(blk[0], blk[1], blk[4], 0, 8, sem_sinal);
        yuv(blk[0], blk[1], blk[5], 8, 8, sem_sinal);
        if (prof == 2u) emitir_bytes(mb_rgb, sizeof(mb_rgb));
        else {
            uint8_t p[16 * 16 * 2];
            for (i = 0; i < 256; i++) {
                const uint8_t *c = &mb_rgb[i * 3];
                uint16_t v = (uint16_t)((c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10 | bit15);
                p[2 * i] = (uint8_t)v; p[2 * i + 1] = (uint8_t)(v >> 8);
            }
            emitir_bytes(p, sizeof(p));
        }
    }
}

static void executar(void)
{
    uint32_t c = cmd >> 29;
    uint32_t i;
    const uint8_t *b = (const uint8_t *)entrada;
    if (c == 1u) decodificar();
    else if (c == 2u) {
        for (i = 0; i < 64; i++) q_luma[i] = b[i];
        if (cmd & 1u) for (i = 0; i < 64; i++) q_croma[i] = b[64 + i];
    } else if (c == 3u) {
        const uint16_t *h = (const uint16_t *)entrada;
        for (i = 0; i < 64; i++) escala[i] = (int16_t)h[i];
    }
    /* DMA1 esperando? */
    if (pend_palavras && ps2_mdec_dma_out(pend_madr, pend_palavras))
        ps2_dma_mdec_saida_pronta();
}

void ps2_mdec_write_data(uint32_t v)
{
    if (faltam) {
        if (recebidos < ENTRADA_MAX) entrada[recebidos++] = v;
        if (--faltam == 0) executar();
        return;
    }
    cmd = v;
    recebidos = 0;
    switch (v >> 29) {
    case 1: faltam = v & 0xFFFFu; break;
    case 2: faltam = (v & 1u) ? 32u : 16u; break;
    case 3: faltam = 32u; break;
    default: faltam = 0; break;
    }
    if (faltam == 0 && (v >> 29) == 1u) executar();
}

uint32_t ps2_mdec_read_data(void)
{
    if (sai_ini < sai_fim) return saida[sai_ini++];
    return 0;
}

uint32_t ps2_mdec_read_status(void)
{
    uint32_t s = 0;
    if (sai_ini >= sai_fim) s |= 1u << 31;                 /* saída vazia */
    if (faltam) s |= 1u << 29;                             /* comando em andamento */
    if ((controle & (1u << 30)) && faltam) s |= 1u << 28;  /* pede entrada */
    if ((controle & (1u << 29)) && sai_ini < sai_fim) s |= 1u << 27;
    s |= ((cmd >> 25) & 0xFu) << 23;                       /* formato */
    if (!faltam) s |= 4u << 16;                            /* bloco atual (ocioso) */
    s |= faltam ? ((faltam - 1u) & 0xFFFFu) : 0xFFFFu;
    return s;
}

void ps2_mdec_write_control(uint32_t v)
{
    if (v & (1u << 31)) ps2_mdec_reset();
    controle = v & (3u << 29);
}

void ps2_mdec_dma_in(uint32_t madr, uint32_t palavras)
{
    uint32_t a = madr & 0x1FFFFCu;
    while (palavras--) {
        ps2_mdec_write_data(ps2_ram_get32(a));
        a = (a + 4u) & 0x1FFFFCu;
    }
}

int ps2_mdec_dma_out(uint32_t madr, uint32_t palavras)
{
    uint32_t a = madr & 0x1FFFFCu;
    while (palavras && sai_ini < sai_fim) {
        ps2_ram_set32(a, saida[sai_ini++]);
        a = (a + 4u) & 0x1FFFFCu;
        palavras--;
    }
    pend_madr = a;
    pend_palavras = palavras;
    return palavras == 0;
}
