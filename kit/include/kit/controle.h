/*
 * kit/controle.h — controle do PS2 via libpad (módulo "controle" do kit).
 *
 * Carrega rom0:SIO2MAN e rom0:PADMAN (presentes em toda BIOS de PS2) e abre
 * até 2 portas. Os botões vêm ATIVOS EM 0, no layout do PS1/PS2:
 *   bit 0 Select, 1 L3, 2 R3, 3 Start, 4 Cima, 5 Direita, 6 Baixo,
 *   7 Esquerda, 8 L2, 9 R2, 10 L1, 11 R1, 12 Triângulo, 13 Círculo,
 *   14 X, 15 Quadrado.
 * Só digital por enquanto (sem analógico/vibração).
 */
#ifndef KIT_CONTROLE_H
#define KIT_CONTROLE_H

#include <stdint.h>

#define KIT_BOTAO_SELECT   0x0001u
#define KIT_BOTAO_L3       0x0002u
#define KIT_BOTAO_R3       0x0004u
#define KIT_BOTAO_START    0x0008u
#define KIT_BOTAO_CIMA     0x0010u
#define KIT_BOTAO_DIREITA  0x0020u
#define KIT_BOTAO_BAIXO    0x0040u
#define KIT_BOTAO_ESQUERDA 0x0080u
#define KIT_BOTAO_L2       0x0100u
#define KIT_BOTAO_R2       0x0200u
#define KIT_BOTAO_L1       0x0400u
#define KIT_BOTAO_R1       0x0800u
#define KIT_BOTAO_TRIANGULO 0x1000u
#define KIT_BOTAO_CIRCULO  0x2000u
#define KIT_BOTAO_X        0x4000u
#define KIT_BOTAO_QUADRADO 0x8000u

typedef struct {
    int      conectado;     /* 1 = controle pronto nesta porta */
    uint16_t botoes;        /* ativo em 0; 0xFFFF se desconectado */
} kit_controle_estado;

/* Abre 'portas' portas (1 ou 2). Devolve quantas abriram (0 = nenhuma:
 * o programa decide se segue sem controle). */
int kit_controle_iniciar(int portas);

/* Lê a porta (0 ou 1); chamar uma vez por quadro. Devolve e->conectado. */
int kit_controle_ler(int porta, kit_controle_estado *e);

/* 1 se TODOS os botões da máscara estão apertados (estado ativo em 0). */
static inline int kit_controle_apertados(const kit_controle_estado *e, uint16_t mascara)
{
    return e->conectado && (e->botoes & mascara) == 0u;
}

#endif
