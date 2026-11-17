/* kit/src/diagnostico.c — ver kit/diagnostico.h. */
#define _GNU_SOURCE 1   /* fopencookie */
#include "kit/diagnostico.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef KIT_LOG_BUF
#define KIT_LOG_BUF (256u * 1024u)    /* gravando a cada ~30 s cabe com folga */
#endif
#define BUF_TAM KIT_LOG_BUF

static char     buf[BUF_TAM];
static size_t   buf_n;
static unsigned long perdidos;         /* bytes descartados ainda não registrados */
static unsigned long perdidos_total;
static char     caminho_log[256];
static int      ativo;
static unsigned long gravado;          /* total já no arquivo */
static char     estado[48] = "LOG DESLIGADO";
static void   (*antes_de_gravar)(void);

void kit_log_antes_de_gravar(void (*f)(void)) { antes_de_gravar = f; }

static ssize_t escrever(void *cookie, const char *dados, size_t n)
{
    size_t livre = BUF_TAM - buf_n, k = n < livre ? n : livre;
    (void)cookie;
    memcpy(buf + buf_n, dados, k);
    buf_n += k;
    perdidos += n - k;
    return (ssize_t)n;                  /* nunca falha para quem chamou printf */
}

static void erro(const char *passo, int codigo)
{
    snprintf(estado, sizeof(estado), "LOG ERRO %s %d", passo, codigo);
}

/* Sem log: o stdout vira um "ralo" (formata e descarta), em vez de mandar
 * cada linha para o IOP. */
static ssize_t descartar(void *cookie, const char *dados, size_t n)
{
    (void)cookie; (void)dados;
    return (ssize_t)n;
}
void kit_log_silenciar(void)
{
    static char b[1024];
    cookie_io_functions_t f = { NULL, descartar, NULL, NULL };
    FILE *s = fopencookie(NULL, "w", f);
    if (!s) return;
    setvbuf(s, b, _IOFBF, sizeof(b));
    stdout = s;
}

int kit_log_iniciar(const char *caminho)
{
    static char buf_stdio[4096];
    cookie_io_functions_t f = { NULL, escrever, NULL, NULL };
    FILE *s;
    int fd = open(caminho, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) { erro("CRIAR", fd); return 0; }
    if (close(fd) < 0) { erro("FECHAR", -1); return 0; }
    s = fopencookie(NULL, "w", f);
    if (!s) { erro("STREAM", 0); return 0; }
    setvbuf(s, buf_stdio, _IOFBF, sizeof(buf_stdio));
    strncpy(caminho_log, caminho, sizeof(caminho_log) - 1u);
    stdout = s;
    ativo = 1;
    snprintf(estado, sizeof(estado), "LOG OK 0");
    return 1;
}

int kit_log_gravar(void)
{
    int fd, w;
    if (!ativo) return 1;
    if (antes_de_gravar) antes_de_gravar();
    fflush(stdout);
    if (perdidos) {                     /* registra também no arquivo */
        char aviso[64];
        int n = snprintf(aviso, sizeof(aviso), "log: %lu bytes descartados (buffer cheio)\n", perdidos);
        if (n > 0 && buf_n + (size_t)n <= BUF_TAM) {
            memcpy(buf + buf_n, aviso, (size_t)n);
            buf_n += (size_t)n;
            perdidos_total += perdidos;
            perdidos = 0;
        }
    }
    if (!buf_n) return 1;
    fd = open(caminho_log, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd < 0) { erro("ABRIR", fd); return 0; }
    w = (int)write(fd, buf, buf_n);
    if (w > 0) {
        /* o que foi gravado sai do buffer (escrita parcial não duplica) */
        gravado += (unsigned long)w;
        memmove(buf, buf + w, buf_n - (size_t)w);
        buf_n -= (size_t)w;
    }
    if (buf_n) {
        erro("ESCREVER", w);
        close(fd);
        return 0;
    }
    if (close(fd) < 0) {
        /* os dados já foram entregues: não regrava (evita duplicar) */
        erro("FECHAR", -1);
        return 0;
    }
    if (perdidos_total)
        snprintf(estado, sizeof(estado), "LOG OK %lu (-%lu)", gravado, perdidos_total);
    else
        snprintf(estado, sizeof(estado), "LOG OK %lu", gravado);
    return 1;
}

const char *kit_log_estado(void) { return estado; }

size_t kit_log_pendente(void)
{
    if (!ativo) return 0;
    fflush(stdout);
    return buf_n;
}

size_t kit_log_capacidade(void) { return BUF_TAM; }
