/*
 * jogo.h — tudo o que é específico do Tekken 3 e que as bibliotecas do kit
 * (../kit) NÃO sabem: nomes de arquivos, pastas e a versão mostrada.
 */
#ifndef JOGO_H
#define JOGO_H

/* JOGO_SUFIXO: variante de compilação (ex.: " Os"), vem do Makefile */
#ifndef JOGO_SUFIXO
#define JOGO_SUFIXO ""
#endif
#define JOGO_VERSAO      "T3 PS2 V32" JOGO_SUFIXO
#define JOGO_PASTA_USB   "mass:/TEKKEN3/"         /* reserva se o argv0 falhar */
#define JOGO_LOG         "tekken3_log.txt"
#define JOGO_EXE         "SLUS_004.02"
/* arquivos que indicam que o pendrive montou (procurados ao lado do ELF) */
#define JOGO_ARQUIVOS    "TEKKEN3.BIN", "Tekken 3 (USA) (Track 1).bin", JOGO_EXE

#endif
