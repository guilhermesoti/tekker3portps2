# Tekken 3 — Port nativo para PlayStation 2

Port do **Tekken 3 (USA, SLUS-00402)** do PS1 para o PS2: o código MIPS do PS1
é **recompilado estaticamente para C** (psxrecomp) e compilado para o Emotion
Engine com o ps2sdk. O hardware do PS1 (GPU→GS, GTE, MDEC, CD, BIOS, IRQ/DMA)
é reimplementado no runtime. Código que o jogo carrega do CD em tempo de
execução (overlays) ainda roda num interpretador.

## Estrutura

| Pasta | O que é | Publicar? |
|---|---|---|
| `ps2/runtime/` | runtime do port (PS1 → PS2) + `jogo.h` (nomes/versão do Tekken) | sim |
| `ps2/tests/` | testes no PC (`make test`) | sim |
| `ps2/tools/` | geração do código, teste no PCSX2 Linux, transformação do código gerado | sim |
| `ps2/docs/` | `PS2_REAL.md` (pendrive/OPL), `PCSX2_LINUX.md`, `PLANO_KIT.md`; histórico em `docs/historico/` | sim |
| `kit/` | **ps2-port-kit**: bibliotecas reutilizáveis (controle, armazenamento, diagnóstico) | sim |
| `gerado/` | C gerado do seu dump | **NÃO** (código do jogo) |
| `referencia/Tekken3Recompiled/` | psxrecomp + headers do runtime (terceiros) | não |
| `*.bin`/`*.cue` | seu dump do jogo | **NÃO** |

## Compilar e testar (Linux/WSL)

```bash
export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
cd ps2 && make test && make -j3 && make pacote
```

- `make pacote` → `build/pacote/APPS/TEKKEN3/` (copie o `.bin` do jogo para lá; ver `docs/PS2_REAL.md`).
- Teste no PCSX2 Linux: `make FORCAR_IOP=1 AUTO_START=1 && bash tools/rodar_pcsx2_linux.sh 60`
  (precisa do `.bin` do jogo na pasta `ps2/`).
- Requer gsKit/dmaKit em `$PS2DEV/gsKit`.

## Estado (15/11/2026)

Boot pelo pendrive no PS2 real, menus, controle, MDEC, GS, luta completa.
A luta ainda está abaixo da velocidade normal no console (otimizações em
andamento: V15–V17). A abertura (FMV) pula porque o tocador é um overlay
interpretado. Sem som e sem memory card ainda.

## Créditos

- **PSXRecomp** — mstan (licença PolyForm Noncommercial 1.0.0)
- **Tekken3Recompiled** — FishB0nes98 (mapas de funções, patches do Tekken 3)
- **ps2sdk / ps2dev** — comunidade ps2dev
