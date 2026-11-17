/*
 * cdrom.h — controlador de CD-ROM do PS1 (0x1F801800-0x1F801803).
 *
 * Lê o disco de uma imagem .bin (setores de 2352 bytes) e responde aos
 * comandos da libcd da Sony com as interrupções (INT1..INT5) certas.
 * Faixas de áudio (CDDA) e XA-ADPCM ainda não tocam (Fase 4).
 */
#ifndef PS2_CDROM_H
#define PS2_CDROM_H

#include <stdint.h>

/* Procura a imagem do disco (ver cdrom.c) a partir da pasta do ELF. */
int      ps2_cdrom_abrir(const char *pasta_elf);
void     ps2_cdrom_reset(void);
int      ps2_cdrom_ler_dados(uint32_t lba, uint8_t *dst2048);  /* ISO9660 */
uint8_t  ps2_cdrom_read(uint32_t reg);           /* reg = 0..3 */
void     ps2_cdrom_write(uint32_t reg, uint8_t v);
void     ps2_cdrom_update(uint32_t ciclos_ps1);  /* chamado periodicamente */
void     ps2_cdrom_dma(uint32_t madr, uint32_t palavras);   /* DMA canal 3 */

#endif
