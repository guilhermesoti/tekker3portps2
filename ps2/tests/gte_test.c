#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu_state.h"

int main(void)
{
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    gte_write_data(&cpu, 12, 1);
    gte_write_data(&cpu, 13, 2);
    gte_write_data(&cpu, 14, 3);
    gte_write_data(&cpu, 15, 4);
    assert(gte_read_data(&cpu, 12) == 2);
    assert(gte_read_data(&cpu, 13) == 3);
    assert(gte_read_data(&cpu, 14) == 4);
    assert(gte_read_data(&cpu, 15) == 4);

    gte_write_data(&cpu, 28, 0x7c41);
    assert(gte_read_data(&cpu, 9) == 128);
    assert(gte_read_data(&cpu, 10) == 256);
    assert(gte_read_data(&cpu, 11) == 3968);
    assert(gte_read_data(&cpu, 29) == 0x7c41);
    gte_write_data(&cpu, 9, 0xffff);
    gte_write_data(&cpu, 10, 0x7fff);
    assert(gte_read_data(&cpu, 28) == 0x7fe0);
    gte_write_data(&cpu, 29, 0);
    assert(gte_read_data(&cpu, 29) == 0x7fe0);

    gte_write_data(&cpu, 30, 0);
    assert(gte_read_data(&cpu, 31) == 32);
    gte_write_data(&cpu, 30, 0xfffffff0u);
    assert(gte_read_data(&cpu, 31) == 28);
    gte_write_data(&cpu, 31, 0);
    assert(gte_read_data(&cpu, 31) == 28);

    gte_write_data(&cpu, 1, 0x8000);
    assert(gte_read_data(&cpu, 1) == 0xffff8000u);
    gte_write_ctrl(&cpu, 26, 0xffff);
    assert(gte_read_ctrl(&cpu, 26) == 0xffffffffu);
    gte_write_ctrl(&cpu, 31, 0xffffffffu);
    assert(gte_read_ctrl(&cpu, 31) == 0xfffff000u);
    gte_write_ctrl(&cpu, 31, 0x00400000u);
    assert(gte_read_ctrl(&cpu, 31) == 0x00400000u);
    /* ---- comandos ---- */
    memset(&cpu, 0, sizeof(cpu));
    /* NCLIP: triângulo (0,0) (10,0) (0,10) -> 100 */
    gte_write_data(&cpu, 12, 0);
    gte_write_data(&cpu, 13, 10);
    gte_write_data(&cpu, 14, 10u << 16);
    gte_execute(&cpu, 0x06);
    assert((int32_t)gte_read_data(&cpu, 24) == 100);

    /* AVSZ3: ZSF3=1365, SZ1..3=300 -> MAC0=1228500, OTZ=299 */
    gte_write_ctrl(&cpu, 29, 1365);
    gte_write_data(&cpu, 17, 300); gte_write_data(&cpu, 18, 300); gte_write_data(&cpu, 19, 300);
    gte_execute(&cpu, 0x2D);
    assert(gte_read_data(&cpu, 24) == 1228500u);
    assert(gte_read_data(&cpu, 7) == 299u);

    /* RTPS: RT identidade, TR=(0,0,1000), V0=(100,50,0), H=1000, sf=1 */
    memset(&cpu, 0, sizeof(cpu));
    gte_write_ctrl(&cpu, 0, 0x1000);            /* RT11=1.0, RT12=0 */
    gte_write_ctrl(&cpu, 2, 0x1000);            /* RT22=1.0, RT23=0 */
    gte_write_ctrl(&cpu, 4, 0x1000);            /* RT33=1.0 */
    gte_write_ctrl(&cpu, 7, 1000);              /* TRZ */
    gte_write_ctrl(&cpu, 26, 1000);             /* H */
    gte_write_data(&cpu, 0, (50u << 16) | 100u);
    gte_write_data(&cpu, 1, 0);
    gte_execute(&cpu, (1u << 19) | 0x01);
    assert(gte_read_data(&cpu, 25) == 100u && gte_read_data(&cpu, 26) == 50u);
    assert(gte_read_data(&cpu, 19) == 1000u);   /* SZ3 */
    {
        uint32_t sxy = gte_read_data(&cpu, 14);
        int sx = (int16_t)(sxy & 0xFFFF), sy = (int16_t)(sxy >> 16);
        assert(sx >= 99 && sx <= 100 && sy >= 49 && sy <= 50);
    }
    assert((gte_read_ctrl(&cpu, 31) & 0x80000000u) == 0u);   /* sem erro */

    /* SQR: IR=(3,4,5), sf=0 -> MAC=(9,16,25) */
    gte_write_data(&cpu, 9, 3); gte_write_data(&cpu, 10, 4); gte_write_data(&cpu, 11, 5);
    gte_execute(&cpu, 0x28);
    assert(gte_read_data(&cpu, 25) == 9u && gte_read_data(&cpu, 26) == 16u &&
           gte_read_data(&cpu, 27) == 25u);

    puts("GTE: registradores e comandos OK");
    return 0;
}
