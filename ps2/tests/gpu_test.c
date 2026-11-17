#include "../runtime/gpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void enviar(uint32_t comando, uint32_t pos, uint32_t tam)
{
    ps2_gpu_gp0(comando); ps2_gpu_gp0(pos); ps2_gpu_gp0(tam);
}

int main(void)
{
    const uint16_t *v;
    ps2_gpu_reset();
    v = ps2_gpu_vram();
    enviar(0x020000ffu, (3u << 16) | 17u, (2u << 16) | 1u);
    assert(v[3 * 1024 + 16] == 0x001fu);
    assert(v[4 * 1024 + 31] == 0x001fu);
    assert(v[3 * 1024 + 32] == 0);

    enviar(0xa0000000u, (10u << 16) | 1023u, (1u << 16) | 3u);
    ps2_gpu_gp0(0x1234abcdu);
    ps2_gpu_gp0(0xeeee5678u);
    assert(v[10 * 1024 + 1023] == 0xabcdu);
    assert(v[10 * 1024] == 0x1234u);
    assert(v[10 * 1024 + 1] == 0x5678u);
    enviar(0xc0000000u, (10u << 16) | 1023u, (1u << 16) | 3u);
    assert(ps2_gpu_read() == 0x1234abcdu);
    assert(ps2_gpu_read() == 0x00005678u);

    ps2_gpu_gp0(0xe3000000u);
    ps2_gpu_gp0(0xe407ffffu);
    ps2_gpu_gp0(0x200000ffu);
    ps2_gpu_gp0((20u << 16) | 20u);
    ps2_gpu_gp0((20u << 16) | 30u);
    ps2_gpu_gp0((30u << 16) | 20u);
    assert(v[21 * 1024 + 21] == 31u);
    assert(v[29 * 1024 + 29] == 0u);

    /* A coordenada Y da área ocupa dez bits, inclusive o bit 19. */
    ps2_gpu_gp0(0xe3080000u);
    ps2_gpu_gp0(0xe40fffffu);
    ps2_gpu_gp0(0x600000ffu);
    ps2_gpu_gp0((80u << 16) | 80u);
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[80 * 1024 + 80] == 0u);
    ps2_gpu_gp1(0x10000003u);
    assert(ps2_gpu_read() == 0x80000u);
    ps2_gpu_gp0(0xe3000000u);
    ps2_gpu_gp0(0xe407ffffu);

    /* A caixa do quad excede 1023, mesmo que cada triângulo não exceda. */
    ps2_gpu_gp0(0x280000ffu);
    ps2_gpu_gp0((100u << 16) | 1048u); /* -1000 */
    ps2_gpu_gp0((100u << 16) | 1748u); /* -300 */
    ps2_gpu_gp0((110u << 16) | 1748u);
    ps2_gpu_gp0((110u << 16) | 400u);
    assert(v[109 * 1024 + 1] == 0u);

    ps2_gpu_gp0(0x80000000u);
    ps2_gpu_gp0((20u << 16) | 20u);
    ps2_gpu_gp0((40u << 16) | 40u);
    ps2_gpu_gp0((4u << 16) | 4u);
    assert(v[41 * 1024 + 41] == 31u);

    /* Página 4bpp na linha 256 e CLUT na linha 100. */
    enviar(0xa0000000u, (256u << 16), (1u << 16) | 1u);
    ps2_gpu_gp0(0x00000001u);
    enviar(0xa0000000u, (100u << 16), (1u << 16) | 2u);
    ps2_gpu_gp0(0x801f0000u);
    ps2_gpu_gp0(0xe1000030u); /* página Y=256; mistura B+F */
    ps2_gpu_gp0(0x65808080u); /* sprite 1x1, textura crua */
    ps2_gpu_gp0((60u << 16) | 60u);
    ps2_gpu_gp0((100u << 22));
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[60 * 1024 + 60] == 0x801fu);
    /* STP permite a mistura somente no pixel com bit 15 ligado. */
    enviar(0xa0000000u, (60u << 16) | 60u, (1u << 16) | 1u);
    ps2_gpu_gp0(0x000003e0u);
    ps2_gpu_gp0(0x67808080u);
    ps2_gpu_gp0((60u << 16) | 60u);
    ps2_gpu_gp0((100u << 22));
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[60 * 1024 + 60] == 0x83ffu);

    ps2_gpu_gp0(0xe6000001u);
    enviar(0xa0000000u, (50u << 16) | 50u, (1u << 16) | 1u);
    ps2_gpu_gp0(0x000003e0u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    ps2_gpu_gp0(0xe6000002u);
    enviar(0xa0000000u, (50u << 16) | 50u, (1u << 16) | 1u);
    ps2_gpu_gp0(31u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    ps2_gpu_gp1(0x00000000u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    assert((ps2_gpu_status() & 0x1c000000u) == 0x1c000000u);
    ps2_gpu_gp1(0x05000805u);
    ps2_gpu_gp1(0x06a00000u);
    ps2_gpu_gp1(0x0703c000u);
    ps2_gpu_gp1(0x08000001u);
    ps2_gpu_gp1(0x03000000u);
    {
        Ps2GpuDisplay d = ps2_gpu_display();
        assert(d.ativo && d.x == 5 && d.y == 2);
        assert(d.largura == 320 && d.altura == 240 && !d.cor24);
    }
    puts("GPU: preenchimento, triangulo e transferencias OK");
    return 0;
}
