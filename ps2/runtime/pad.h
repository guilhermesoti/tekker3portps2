/*
 * pad.h — porta de controle/memory card do PS1 (SIO0, 0x1F801040-0x1F80104F).
 *
 * Simula um controle digital (SCPH-1080) na porta 1; porta 2 e memory
 * cards: desconectados (por enquanto). Os botões vêm de ps2_pad_botoes
 * (ativo em 0, formato do PS1); a leitura do controle do PS2 entra depois.
 */
#ifndef PS2_PAD_H
#define PS2_PAD_H

#include <stdint.h>

extern uint16_t ps2_pad_botoes[2];   /* 0xFFFF = nada apertado */

void     ps2_pad_reset(void);
void     ps2_pad_tick(void);     /* entrega a IRQ de ACK adiada */
uint32_t ps2_pad_read(uint32_t off, unsigned w);    /* off = 0x40..0x4F */
void     ps2_pad_write(uint32_t off, uint32_t v, unsigned w);

#endif
