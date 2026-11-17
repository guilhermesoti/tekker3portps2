#ifndef PS2_GPU_H
#define PS2_GPU_H

#include <stdint.h>

#define PS2_GPU_LARGURA 1024u
#define PS2_GPU_ALTURA 512u

void ps2_gpu_reset(void);
void ps2_gpu_gp0(uint32_t palavra);
void ps2_gpu_gp0_bloco(const uint32_t *p, unsigned n);
void ps2_gpu_gp1(uint32_t palavra);
uint32_t ps2_gpu_read(void);
uint32_t ps2_gpu_status(void);
void ps2_gpu_vblank(void);          /* alterna o campo (480i) */
const uint16_t *ps2_gpu_vram(void);

typedef struct {
    unsigned x, y, largura, altura;
    int ativo, cor24;
} Ps2GpuDisplay;

Ps2GpuDisplay ps2_gpu_display(void);

#endif
