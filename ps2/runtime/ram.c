/* ram.c — RAM principal do PS1 (ver ram.h). */
#include "ram.h"

#include <string.h>

uint8_t g_ps1_ram[PS2_RAM_SIZE] __attribute__((aligned(64)));

void ps2_ram_clear(void)
{
    memset(g_ps1_ram, 0, sizeof(g_ps1_ram));
}

uint8_t *ps2_ram_data(void)
{
    return g_ps1_ram;
}

int ps2_ram_offset(uint32_t address, uint32_t *offset)
{
    if (offset == 0 || !ps2_ram_hit(address))
        return 0;
    *offset = address & PS2_RAM_MASK;
    return 1;
}

int ps2_ram_read8(uint32_t address, uint8_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get8(address);
    return 1;
}

int ps2_ram_read16(uint32_t address, uint16_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get16(address);
    return 1;
}

int ps2_ram_read32(uint32_t address, uint32_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get32(address);
    return 1;
}

int ps2_ram_write8(uint32_t address, uint8_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set8(address, value);
    return 1;
}

int ps2_ram_write16(uint32_t address, uint16_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set16(address, value);
    return 1;
}

int ps2_ram_write32(uint32_t address, uint32_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set32(address, value);
    return 1;
}
