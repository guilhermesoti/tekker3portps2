# Tekken 3 — port nativo para PlayStation 2

Port do **Tekken 3 (PS1, USA, SLUS-00402)** para o **PS2**, sem emulação: o
código MIPS do PS1 é recompilado estaticamente para C (psxrecomp) e compilado
para o Emotion Engine com o ps2sdk. O hardware do PS1 (GPU→GS, GTE, MDEC, CD,
BIOS, IRQ/DMA) é reimplementado no runtime deste repositório.

Projeto gratuito e sem fins comerciais.

## O que NÃO está aqui

Este repositório tem só o código do port. **Nada do jogo é distribuído:**

- o dump do disco (`*.bin` / `*.cue`) — use o seu próprio, do seu disco original;
- o C gerado a partir do executável do jogo (`gerado/`, `build/r/nativo.c`);
- executáveis compilados (`*.elf`), logs e capturas de tela do jogo;
- `referencia/` (psxrecomp e headers de terceiros).

O `.gitignore` bloqueia esses arquivos.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `ps2/runtime/` | runtime do port (PS1 → PS2): GPU→GS, GTE, MDEC, CD, BIOS HLE, IRQ/DMA |
| `ps2/tools/` | transformação do código gerado, tradutor de rotinas para C nativo, teste no PCSX2 |
| `ps2/tests/` | testes no PC (`make test`) |
| `ps2/docs/` | documentação (PS2 real/OPL, PCSX2 Linux, auditorias, histórico) |
| `kit/` | **ps2-port-kit**: bibliotecas reutilizáveis (controle, armazenamento, diagnóstico) |

Detalhes de compilação e teste: [`ps2/README.md`](ps2/README.md).

## Estado

- Boot pelo pendrive (OPL) no PS2 real, menus, controle, vídeos (MDEC) e lutas
  completas com renderização no GS.
- **As lutas ainda rodam abaixo de 60 fps no console** (cerca de 40 fps na
  V32). As otimizações estão em andamento: GTE especializado, desenho direto
  para o GS, despacho mais barato e rotinas do jogo traduzidas para C nativo.
- Ainda sem som e sem memory card. A abertura (FMV) é pulada.
