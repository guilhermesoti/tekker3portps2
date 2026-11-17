/*
 * Regressão dos achados F1, F4, F5 e F6 da revisão técnica (14/11/2026,
 * reproduções originais feitas pelo ChatGPT/Codex). Relógio controlado:
 * compile com -Dclock=relogio_teste.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../runtime/memory.h"
#include "../runtime/io.h"
#include "../runtime/ram.h"
#include "../runtime/gpu.h"
#include "../runtime/bios_hle.h"

static clock_t ticks;
clock_t relogio_teste(void) { return ticks; }
void ps2_video_vblank(void) {}

static void chamar(CPUState *c, unsigned tab, unsigned fn, unsigned a0)
{
    c->gpr[9] = fn; c->gpr[4] = a0; c->gpr[31] = 0x80010000u;
    ps2_bios_call(c, tab);
}

int main(void)
{
    CPUState c;
    unsigned h;
    memset(&c, 0, sizeof c);
    ps2_memory_init(&c);

    /* F1: GPU_cw (A0 49h) chega na GPU igual ao GP0 direto */
    chamar(&c, 0xA0, 0x49, 0x020000FFu);
    chamar(&c, 0xA0, 0x49, 0x00010000u);
    chamar(&c, 0xA0, 0x49, 0x00010010u);
    assert(ps2_gpu_vram()[1024] == 0x001Fu);

    /* F5: alvo com IRQ e sem reset no alvo */
    ps2_io_write(0x1F801074u, 1u << IRQ_TMR0, 4);
    ps2_io_write(0x1F801104u, 0x50u, 4);
    ps2_io_write(0x1F801108u, 1000u, 4);
    ticks = 60;                                 /* ~2032 ciclos */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));

    /* V19 1.2: zerar o contador depois de cruzar o alvo não gera IRQ falsa */
    ps2_io_init();
    ticks = 0;
    ps2_io_write(0x1F801074u, 1u << IRQ_TMR0, 4);
    ps2_io_write(0x1F801104u, 0x58u, 4);        /* reset no alvo, IRQ no alvo, repetir */
    ps2_io_write(0x1F801108u, 1000u, 4);
    ticks = 60;                                 /* ~2032 ciclos: alvo cruzado 2 vezes */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));
    ps2_io_write(0x1F801070u, ~(1u << IRQ_TMR0), 4);
    ps2_io_write(0x1F801100u, 0u, 4);           /* COUNT = 0 */
    ps2_timers_poll();                          /* mesmo instante */
    assert(!(ps2_irq_pending() & (1u << IRQ_TMR0)));
    ps2_io_write(0x1F801108u, 2000u, 4);        /* aumentar o alvo também não */
    ps2_timers_poll();
    assert(!(ps2_irq_pending() & (1u << IRQ_TMR0)));
    ticks = 130;                                /* ~2370 ciclos depois: cruza 2000 */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));

    /* F6: escrever DICR com o bit mestre forçado gera IRQ de DMA */
    ps2_io_init();
    ps2_io_write(0x1F801074u, 1u << IRQ_DMA, 4);
    ps2_io_write(0x1F8010F4u, 1u << 15, 4);
    assert(ps2_irq_pending() & (1u << IRQ_DMA));

    /* F4: WaitEvent de evento ainda não entregue não retorna */
    c.gpr[5] = 2; c.gpr[6] = 0x2000; c.gpr[7] = 0;
    chamar(&c, 0xB0, 0x08, 0xF2000000u);
    h = c.gpr[2];
    chamar(&c, 0xB0, 0x0C, h);
    chamar(&c, 0xB0, 0x0A, h);
    assert(c.pc == 0xB0u);                      /* vai repetir a chamada */

    puts("Revisao (F1, F4, F5, F6, V19 1.2): OK");
    return 0;
}
