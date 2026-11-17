/* pad.c — ver pad.h. Referência: nocash psx-spx (Controllers and Memory Cards). */
#include "pad.h"
#include "io.h"
#include <stdio.h>

unsigned long g_ps2_pad_trocas, g_ps2_pad_leituras;

uint16_t ps2_pad_botoes[2] = { 0xFFFFu, 0xFFFFu };

static uint16_t ctrl, modo, baud;
static uint8_t  rx, rx_cheio, ack, irq;
static int      etapa;          /* posição na conversa com o dispositivo */
static int      alvo;           /* 0 = nenhum, 1 = controle, 2 = memory card */
static int      ack_pend;       /* IRQ de ACK agendada */
static uint32_t ack_quando;

void ps2_pad_reset(void)
{
    ctrl = modo = baud = 0;
    rx = 0xFF; rx_cheio = ack = irq = 0;
    etapa = 0; alvo = 0;
    ack_pend = 0;
}

/* Entrega a IRQ de ACK ~100 ciclos PS1 (~3 us) depois do byte. */
void ps2_pad_tick(void)
{
    if (ack_pend && (uint32_t)(ps2_ps1_cycles() - ack_quando) >= 100u) {
        ack_pend = 0;
        irq = 1;
        ps2_irq_raise(IRQ_PAD);
    }
}

/* Um byte trocado com o dispositivo selecionado. */
static void trocar(uint8_t tx)
{
    int porta = (ctrl >> 13) & 1;
    uint16_t b = ps2_pad_botoes[porta];

    static unsigned n_log;
    g_ps2_pad_trocas++;
    ack = 0;
    rx = 0xFF;
    if (!(ctrl & 0x0002u)) {            /* /JOYn não selecionado */
        rx_cheio = 1;
        return;
    }
    if (etapa == 0) {
        alvo = tx == 0x01 ? 1 : tx == 0x81 ? 2 : 0;
        /* só o controle da porta 1 existe */
        if (alvo == 1 && porta == 0) { rx = 0xFF; ack = 1; etapa = 1; }
        else { alvo = 0; }
    } else if (alvo == 1) {
        switch (etapa) {
        case 1: rx = 0x41; ack = 1; break;                 /* ID: controle digital */
        case 2: rx = 0x5A; ack = 1; break;
        case 3: rx = (uint8_t)b; ack = 1; break;           /* botões (byte baixo) */
        case 4: rx = (uint8_t)(b >> 8); ack = 0; break;    /* último: sem ACK */
        }
        etapa = ack ? etapa + 1 : 0;
    }
    rx_cheio = 1;
    if (alvo == 1 && etapa == 0) g_ps2_pad_leituras++;
    if (n_log < 12u) { n_log++;
        printf("pad: tx=%02x rx=%02x ctrl=%04x etapa=%d ack=%d\n", tx, rx, ctrl, etapa, ack); }
    if (ack && (ctrl & 0x1000u)) {      /* IRQ de ACK habilitada */
        /* No hardware o /ACK chega alguns microssegundos depois do byte;
         * o driver do jogo limpa I_STAT bit 7 logo após escrever TX e só
         * então espera a IRQ. Por isso ela é adiada (ps2_pad_tick). */
        ack_pend = 1;
        ack_quando = ps2_ps1_cycles();
    }
}

uint32_t ps2_pad_read(uint32_t off, unsigned w)
{
    uint32_t v = 0;
    switch (off & ~1u) {
    case 0x40:                          /* RX_DATA */
        v = rx_cheio ? rx : 0xFFu;
        rx_cheio = 0;
        if (w == 4) v |= 0xFFFFFF00u;
        break;
    case 0x44:                          /* STAT */
        v = 0x0005u;                    /* TX pronto + TX terminado */
        if (rx_cheio) v |= 0x0002u;
        ps2_pad_tick();
        if (irq) v |= 0x0200u;          /* bit 7 (/ACK) é só um pulso: 0 */
        break;
    case 0x48: v = modo; break;
    case 0x4A: v = ctrl; break;
    case 0x4E: v = baud; break;
    }
    return v;
}

void ps2_pad_write(uint32_t off, uint32_t v, unsigned w)
{
    (void)w;
    switch (off & ~1u) {
    case 0x40: trocar((uint8_t)v); break;
    case 0x48: modo = (uint16_t)v; break;
    case 0x4A:
        ctrl = (uint16_t)(v & ~0x0050u);
        if (v & 0x0010u) { irq = 0; }                   /* ACK da IRQ */
        if (v & 0x0040u) { ps2_pad_reset(); }           /* reset */
        if (!(v & 0x0002u)) {                           /* fim da seleção */
            etapa = 0; alvo = 0;
        }
        break;
    case 0x4E: baud = (uint16_t)v; break;
    }
}
