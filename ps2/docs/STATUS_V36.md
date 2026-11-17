# Status — V36 (17/11/2026)

## Medida que vale: o relógio do round

O "atualizações da luta por 120 VBlanks" do log **não acompanha** a velocidade
real (a V33 marcava 118/120 e rodava a ~71%). A medida confiável é o
**cronômetro do round**: 40 s do relógio do jogo, medidos com cronômetro.

| Onde | 40 s do jogo levaram | Velocidade |
|---|---|---|
| PS1 real (disco original) | 39,1 s | 100% (referência) |
| Port V33, PS2 real (pendrive) | 56,5 s | ~71% |
| Port V35, PS2 real (pendrive) | 48 s | ~83% |
| Port V36, PS2 real (pendrive) | 48 s | ~83% |

## V33 → V36

- **V33** (Windows): `jal` com alvo constante vira chamada C direta; despachos
  na luta ~560k → ~137k por 120 VBlanks.
- **V34**: log grava no pendrive a cada ~30 s (`PS2_LOG_BLOCOS`, padrão 15
  relatórios; `make LOG_BLOCOS=0` desliga). Buffer do kit 64 → 256 KB.
- **V35**: **IRQs do timer 2 não se perdem mais.** O Tekken libera cada passo
  do jogo na IRQ do timer 2 (handler `80029784`, flag `8009bc5c`), não no
  VBlank. Antes, se o alvo era cruzado várias vezes entre duas conferências,
  só uma IRQ era gerada. Agora as que sobram ficam numa fila (teto 64) e são
  entregues quando o jogo reconhece a anterior. Resultado: 56,5 → 48 s e o
  "maior intervalo sem verificar interrupção" caiu de 40–60 ms para ~6 ms.
  Nova linha no log: `ritmo:` (eventos/entregues/descartados do timer 2,
  IRQs vistas pelo jogo em `80095434`, passos liberados em `80097f00`).
- **V36**: sem som no port → o tick do sequenciador de som (`80088BA8`) volta
  na hora (`PS2_SOM_PULAR=1`, `runtime/nativo_ganchos.c`). Tirou ~18% do EE,
  mas **não acelerou**: esse tempo era gasto durante a espera do quadro.
  Mantido (sem efeito colateral visto até agora).

## Onde está o gargalo agora

- O jogo espera o próximo quadro em `800296c4` (laço com o gerador aleatório
  `8004ce54`). Com a V36 sobra espera em média, mas os quadros mais pesados
  passam de 16,7 ms e perdem o VBlank: ~96 quadros a cada 120 VBlanks (~80%).
- Topo do perfil na luta (V36): `8007e0b8` ~21%, `8003a87c` ~20%,
  `80028bcc` ~8%, `8003b1c4` ~6% (inclui GPU/GTE/IRQ feitos dentro delas).
- Próximo: deixar essas rotinas mais rápidas (C nativo via `tools/nativo.py`,
  GTE, desenho) e medir sempre com o cronômetro do round.

## Ambiente (Ubuntu, Ryzen 5 5600G)

- SDK: `~/ps2dev/ps2dev` (PS2DEV/PS2SDK no `~/.bashrc`).
- PCSX2 sem tela: `bash ps2/tools/rodar_pcsx2_linux.sh 60` (Vulkan escondido,
  renderizador 13, BIOS scph39001). A velocidade do PCSX2 **não** representa o console.
- PS2 real pela rede: PS2Link novo aberto pelo wLaunchELF, IP fixo na rede local;
  `bash ps2/tools/rodar_ps2_rede.sh IP_DO_PS2`. Rodando de `host:` o
  port não reinicia o IOP, mas **lê o CD pela rede de forma síncrona** (~13% do
  EE): serve para testar se funciona, **não para medir velocidade**.
