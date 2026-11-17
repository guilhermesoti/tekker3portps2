# Lentidão na luta — análise V41d–V43 (17/11/2026)

Medida que vale: cronômetro do round (40 s de jogo; o método do teste mede 39 s
de jogo). PS1 real: 39,12 s.

| Versão | Sem log | Com log |
|---|---|---|
| V33 | 56,5 s (~71%) | — |
| V35/V36 | 48 s (~83%) | — |
| V38 | 47 s (~85%) | — |
| **V42** | **41,06 s (~95%)** | 41,51 s |

## Como o jogo marca o tempo da luta (desmontagem do SLUS_004.02)

- VBlank → VSyncCallback `800299E0` arma o timer 2 (`SetRCnt` alvo 0x13A7, modo
  0x258: repetir, sys/8 ≈ 1,19 ms).
- IRQ do timer 2 → callback `80029784`: `ResetGraph(1)` (`8007C028`); se o
  contador `8009BC5C` == 0 (quadro novo pronto) troca a tela (`PutDispEnv`
  `8007CBD4`) e manda a OT (`DrawOTag` `8007C998`); se != 0 e a trava
  `80095428` == 0, reenvia a mesma OT (redesenho do 480i). No fim:
  `8009BC5C`++, `80095434`++ e desarma o timer (`SetRCnt ...,0xFFFF,0x2000`).
- Laço da luta `800508C4`: passo `8002AE58` (lógica + montagem do desenho); se
  `800295F4` (contador >= 2) roda UM passo de recuperação sem desenho (marca
  `800ADCA4`, testada nas funções de desenho/efeitos). Máximo 2 passos por quadro.
- Fim do quadro `800296C4`: zera `8009BC5C` e espera a próxima IRQ do timer 2
  (`8004CE54` no laço é só a semente aleatória: tempo ocioso).
- Relógio do round `800ADBCC`: passos de 1/60 s que faltam; início
  `(x+2)*600` (`8002A9C8`), -1 por passo (`8003CCDC`), mostra `(t+59)/60`,
  `t < 1` = TIME UP (`8003E478`).

## V42 — corrigido: redesenho completo à toa em metade dos quadros

`io.c`, limpeza da OT (DMA6): o teste `a - sig.madr < n*4+4` invalidava a
referência do "redesenho igual" quando a OT VIZINHA era limpa. As duas OTs do
jogo têm 1024 entradas e são vizinhas (topos 0x0A7584 e 0x0A8584, distância
exatamente n*4 — confirmado com o evento "OTC" da linha do tempo). O DMA6
escreve de MADR para baixo: faixa `[a-(n-1)*4, a]`, logo o certo é `< n*4`.
Efeito: a lista inteira era processada de novo no começo do passo 1 em metade
dos quadros (PCSX2: +4,2 ms nesses quadros).

Console, luta, por 2 s: V39 45 quadros / 90 passos / 68 listas desenhadas;
V42 60 quadros / 119–120 passos / 60 listas; IRQ do timer 2 18% → 13–14%.
Linha do tempo V42 no console (média de 27 quadros): quadro 33,4 ms, espera
ociosa 8–10 ms, passo 1 17–19 ms, passo 2 5,7–7 ms, lista 2,9 ms, VBlank
1,1 ms por quadro.

## Em aberto — IRQ do timer 2 repetida (não é comportamento do PS1)

O port processa a OT inteira dentro da callback (~3 ms no console, mais que o
período de 1,19 ms) com o timer ainda em modo repetir; a callback volta 3–4
vezes por quadro e o contador chega a 4–5 no teste `800295F4` (PS1: no máximo
2). Hoje isso garante o passo de recuperação na luta.

V43 tem a correção (`PS2_T2_FIEL=1`: escrita no modo feita pela própria
callback descarta os cruzamentos), mas DESLIGADA: no PCSX2 o relógio do round
caiu de 99,2% para 93–97%, porque lá o passo 1 termina perto do VBlank seguinte
e, sem as IRQs extras, às vezes o VBlank se perde (o fim do quadro zera o
contador). Só ligar quando a lista sair de dentro da callback (o quadro
começaria ~3 ms antes, como no PS1).

## Por que ainda parece lento

- Na luta o port faz 30 imagens/s com 2 passos de lógica cada (velocidade do
  jogo ~99%). Fontes sobre o PS1 e o Tekken3Recomp no PC indicam ~60
  quadros/s na luta: mesma velocidade, metade da fluidez. Não foi medido no PS1.
- Nos trechos sem passo de recuperação (abertura do round, KO, pose de vitória)
  o port faz 1 passo a cada 2 VBlanks (60 passos por 2 s no log): metade da
  velocidade, se o PS1 roda esses trechos a 60/s.
- Para 60/s o quadro (callback com a lista + passo 1 + VBlank) precisa caber
  em 16,7 ms; no console hoje são ~22 ms.

## V43 — medição sem cronômetro

- Linha `relogio:` a cada ~2 s e `rodada:` no fim de cada round: passos do
  relógio do round por VBlank do PS1 (100% = PS1), tempo real e o do PS1. O
  painel mostra `QUADROS/S`, `JOGO` (últimos 2 s) e `RODADA` (último round).
- Versão sem log: o stdout é descartado (antes cada linha dos relatórios ia
  para o IOP).
- Linha do tempo: ring de 16384 eventos; despeja os quadros > 37 ms ("QUADRO
  LENTO"), um trecho de 1 passo por quadro, e marca relatório e leituras do CD.
- `tools/linha_tempo.py LOG [n]` resume a linha do tempo quadro a quadro.
- `make AUTO_START=1 LOG_BLOCOS=15` + `tools/rodar_pcsx2_linux.sh 230`: o
  PCSX2 entra sozinho numa luta e grava tudo.
