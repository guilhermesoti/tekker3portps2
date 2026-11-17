/* kit/src/armazenamento.c — ver kit/armazenamento.h. */
#include "kit/armazenamento.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
extern int fileXioInit(void);     /* o header do fileXio proíbe uso junto da newlib */

static char mensagens[1024];
static int  filexio_ok;

static void msg(const char *fmt, ...)
{
    char linha[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(linha, sizeof(linha), fmt, ap);
    va_end(ap);
    fputs(linha, stdout);
    if (strlen(mensagens) + strlen(linha) < sizeof(mensagens))
        strcat(mensagens, linha);
}

/* gerados por bin2c a partir de $(PS2SDK)/iop/irx (kit/kit.mk) */
extern unsigned char iomanX_irx[], fileXio_irx[], usbd_irx[], bdm_irx[],
                     bdmfs_fatfs_irx[], usbmass_bd_irx[];
extern unsigned int size_iomanX_irx, size_fileXio_irx, size_usbd_irx, size_bdm_irx,
                    size_bdmfs_fatfs_irx, size_usbmass_bd_irx;

static int carregar(const char *nome, unsigned char *irx, unsigned int tam)
{
    int res = 0, id = SifExecModuleBuffer(irx, tam, 0, NULL, &res);
    if (id < 0 || res == 1) {           /* res 1 = NO_RESIDENT_END: falhou */
        msg("iop: %s falhou (id %d, res %d)\n", nome, id, res);
        return 0;
    }
    msg("iop: %s carregado\n", nome);
    return 1;
}

static void esperar_ms(unsigned ms)
{
    uint32_t ini, agora;
    __asm__ volatile("mfc0 %0, $9" : "=r"(ini));
    do __asm__ volatile("mfc0 %0, $9" : "=r"(agora));
    while ((uint32_t)(agora - ini) < ms * 294912u);
}

int kit_arm_preparar(const char *argv0, int forcar_usb)
{
    if (!forcar_usb && argv0 && strncmp(argv0, "host", 4) == 0) {
        msg("iop: rodando de host: (PCSX2), IOP mantido\n");
        return KIT_ARM_HOST;
    }
    msg("iop: reiniciando o IOP e carregando USB (FAT32/exFAT)\n");
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    if (!carregar("iomanX", iomanX_irx, size_iomanX_irx) ||
        !carregar("fileXio", fileXio_irx, size_fileXio_irx) ||
        !carregar("usbd", usbd_irx, size_usbd_irx) ||
        !carregar("bdm", bdm_irx, size_bdm_irx) ||
        !carregar("bdmfs_fatfs", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx) ||
        !carregar("usbmass_bd", usbmass_bd_irx, size_usbmass_bd_irx))
        return KIT_ARM_ERRO_MODULO;
    if (fileXioInit() < 0) {
        msg("iop: fileXioInit falhou\n");
        return KIT_ARM_ERRO_FILEXIO;
    }
    filexio_ok = 1;
    return KIT_ARM_OK;
}

int kit_arm_esperar(const char *const *caminhos, unsigned timeout_ms,
                    char *achado, size_t tam)
{
    unsigned t;
    for (t = 0; t <= timeout_ms; t += 100u) {
        unsigned k;
        for (k = 0; caminhos && caminhos[k]; k++) {
            FILE *f = fopen(caminhos[k], "rb");
            if (f) {
                fclose(f);
                msg("iop: arquivo pronto (%u ms): %s\n", t, caminhos[k]);
                if (achado && tam) {
                    strncpy(achado, caminhos[k], tam - 1u);
                    achado[tam - 1u] = '\0';
                }
                return KIT_ARM_OK;
            }
        }
        esperar_ms(100);
    }
    msg("iop: nenhum arquivo encontrado em %u ms\n", timeout_ms);
    return KIT_ARM_NAO_ACHOU;
}

int kit_arm_pasta_do_elf(const char *argv0, char *saida, size_t tam)
{
    const char *b;
    size_t n;
    if (!argv0 || !tam) return 0;
    b = strrchr(argv0, '/');
    if (!b) b = strrchr(argv0, '\\');
    if (!b) b = strchr(argv0, ':');
    if (!b) return 0;
    n = (size_t)(b - argv0) + 1u;
    if (n >= tam) return 0;
    memcpy(saida, argv0, n);
    saida[n] = '\0';
    return 1;
}

int kit_arm_filexio_ativo(void) { return filexio_ok; }
const char *kit_arm_mensagens(void) { return mensagens; }
