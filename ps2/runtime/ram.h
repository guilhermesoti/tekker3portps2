/*
 * ram.h — RAM principal do PS1 (2 MB) dentro do PS2.
 *
 * O PS1 e o EE do PS2 são little-endian e o PS1 exige que acessos de 16 e
 * 32 bits sejam alinhados (desalinhado gera exceção no console original).
 * Por isso a leitura/escrita é feita direto na memória, sem montar byte a
 * byte. Endereços desalinhados são alinhados para baixo (não acontecem em
 * código correto; assim evitamos travar o EE com exceção de alinhamento).
 *
 * Mapa: a RAM de 2 MB se repete 4 vezes no intervalo físico 0x000000-0x7FFFFF
 * e aparece nos segmentos KUSEG (0x0...), KSEG0 (0x8...) e KSEG1 (0xA...).
 */
#ifndef PS2_RAM_H
#define PS2_RAM_H

#include <stdint.h>

#define PS2_RAM_SIZE         0x00200000u
#define PS2_RAM_MIRROR_SIZE  0x00800000u
#define PS2_RAM_MASK         (PS2_RAM_SIZE - 1u)

/* Tipos com may_alias: acesso direto sem violar strict aliasing. */
typedef uint16_t ps2_u16a __attribute__((may_alias));
typedef uint32_t ps2_u32a __attribute__((may_alias));

extern uint8_t g_ps1_ram[PS2_RAM_SIZE];

/*
 * Detecção de código sobrescrito: toda escrita que cai na área do
 * executável original (onde existe código compilado) marca um "grânulo"
 * de 64 bytes como sujo. O despacho (stubs.c) só usa o código compilado
 * se a RAM ainda for igual ao executável original nesse trecho; senão o
 * trecho (overlay carregado do CD) vai para o interpretador.
 */
extern uint32_t g_cod_ini, g_cod_tam;      /* offset na RAM e tamanho do EXE */
extern uint8_t  g_cod_sujo[];              /* 1 bit por 64 bytes */
extern uint32_t g_cod_ver;                 /* V31: sobe quando um grânulo limpo suja */

static inline __attribute__((always_inline)) void ps2_cod_marcar(uint32_t addr)
{
    uint32_t o = (addr & PS2_RAM_MASK) - g_cod_ini;
    if (o < g_cod_tam) {
        /* V31: grânulo já sujo (dados no meio do EXE) não grava de novo */
        uint8_t *b = &g_cod_sujo[o >> 9], m = (uint8_t)(1u << ((o >> 6) & 7u));
        if (__builtin_expect(!(*b & m), 0)) { *b |= m; g_cod_ver++; }
    }
}

/*
 * Verdadeiro se o endereço cai na RAM (ou espelhos).
 * Segmentos válidos (bits 31..29): 0 = KUSEG baixo, 4 = KSEG0, 5 = KSEG1.
 * A máscara 0x31 tem os bits 0, 4 e 5 ligados.
 */
static inline __attribute__((always_inline)) int ps2_ram_hit(uint32_t addr)
{
    return ((0x31u >> (addr >> 29)) & 1u) &&
           ((addr & 0x1FFFFFFFu) < PS2_RAM_MIRROR_SIZE);
}

/* Acessos diretos — chamar só depois de ps2_ram_hit(). */
static inline __attribute__((always_inline)) uint8_t ps2_ram_get8(uint32_t addr)
{
    return g_ps1_ram[addr & PS2_RAM_MASK];
}
static inline __attribute__((always_inline)) uint16_t ps2_ram_get16(uint32_t addr)
{
    return *(const ps2_u16a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~1u)];
}
static inline __attribute__((always_inline)) uint32_t ps2_ram_get32(uint32_t addr)
{
    return *(const ps2_u32a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~3u)];
}
static inline __attribute__((always_inline)) void ps2_ram_set8(uint32_t addr, uint8_t v)
{
    g_ps1_ram[addr & PS2_RAM_MASK] = v;
    ps2_cod_marcar(addr);
}
static inline __attribute__((always_inline)) void ps2_ram_set16(uint32_t addr, uint16_t v)
{
    *(ps2_u16a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~1u)] = v;
    ps2_cod_marcar(addr);
}
static inline __attribute__((always_inline)) void ps2_ram_set32(uint32_t addr, uint32_t v)
{
    *(ps2_u32a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~3u)] = v;
    ps2_cod_marcar(addr);
}

/* API com verificação (usada pelo runtime e pelos testes). */
void ps2_ram_clear(void);
uint8_t *ps2_ram_data(void);
int ps2_ram_offset(uint32_t address, uint32_t *offset);

int ps2_ram_read8(uint32_t address, uint8_t *value);
int ps2_ram_read16(uint32_t address, uint16_t *value);
int ps2_ram_read32(uint32_t address, uint32_t *value);
int ps2_ram_write8(uint32_t address, uint8_t value);
int ps2_ram_write16(uint32_t address, uint16_t value);
int ps2_ram_write32(uint32_t address, uint32_t value);

#endif
