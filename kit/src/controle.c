/* kit/src/controle.c — ver kit/controle.h. Base: ps2sdk samples/rpc/pad. */
#include "kit/controle.h"

#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>

static char buf_porta[2][256] __attribute__((aligned(64)));
static int  aberta[2];
static int  estado_ant[2] = { -1, -1 };

int kit_controle_iniciar(int portas)
{
    int r, p, n = 0;
    if (portas < 1) portas = 1;
    if (portas > 2) portas = 2;
    sceSifInitRpc(0);
    r = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    if (r < 0) { printf("controle: SIO2MAN falhou (%d)\n", r); return 0; }
    r = SifLoadModule("rom0:PADMAN", 0, NULL);
    if (r < 0) { printf("controle: PADMAN falhou (%d)\n", r); return 0; }
    padInit(0);
    for (p = 0; p < portas; p++) {
        if (padPortOpen(p, 0, buf_porta[p]) == 0) {
            printf("controle: porta %d nao abriu\n", p + 1);
            continue;
        }
        aberta[p] = 1;
        n++;
        printf("controle: porta %d aberta (libpad)\n", p + 1);
    }
    return n;
}

int kit_controle_ler(int porta, kit_controle_estado *e)
{
    struct padButtonStatus st;
    int estado;

    e->conectado = 0;
    e->botoes = 0xFFFFu;
    if (porta < 0 || porta > 1 || !aberta[porta]) return 0;
    estado = padGetState(porta, 0);
    if (estado != estado_ant[porta]) {
        estado_ant[porta] = estado;
        printf("controle: porta %d estado %d\n", porta + 1, estado);
    }
    if (estado != PAD_STATE_STABLE && estado != PAD_STATE_FINDCTP1) return 0;
    if (padRead(porta, 0, &st) == 0) return 0;
    e->conectado = 1;
    e->botoes = st.btns;
    return 1;
}
