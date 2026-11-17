#!/usr/bin/env bash
# Compila o port e entrega o ELF pronto (e copia para o pendrive, se estiver
# montado em /media/$USER/*/APPS/TEKKEN3).
#
# Uso (de dentro de ps2/):  bash tools/gerar_elf.sh [jogo|log|medida]
#   jogo   (padrão) sem log: para jogar e cronometrar (sem travadas)
#   log    log no pendrive a cada ~30 s (trava ~0,5 s ao gravar): diagnóstico
#   medida log + perfil das chamadas diretas (mais lento): só para o perfil
set -euo pipefail
cd "$(dirname "$0")/.."
MODO="${1:-jogo}"
case "$MODO" in
  jogo)   ARGS=("LOG_BLOCOS=0") ;;
  log)    ARGS=("LOG_BLOCOS=15") ;;
  medida) ARGS=("LOG_BLOCOS=15" "GEN_OPT=-O2 -DPS2_PROF_DIRETO=1") ;;
  *) echo "uso: bash tools/gerar_elf.sh [jogo|log|medida]"; exit 1 ;;
esac
VERSAO=$(sed -n 's/.*JOGO_VERSAO *"T3 PS2 \(V[0-9a-z]*\)".*/\1/p' runtime/jogo.h)
echo "==> Compilando $VERSAO ($MODO): make ${ARGS[*]}"
echo "    (se mudou o código gerado ou a ferramenta, leva ~3-5 min)"
make -j"$(nproc)" "${ARGS[@]}"
make pacote "${ARGS[@]}"

ELF=build/pacote/APPS/TEKKEN3/tekken3.elf
SAIDA="../entrega/tekken3_${VERSAO}_${MODO}.elf"
mkdir -p ../entrega
cp "$ELF" "$SAIDA"
echo "==> Pronto: $(realpath "$SAIDA")"

COPIADO=0
for d in /media/"$USER"/*/APPS/TEKKEN3 /run/media/"$USER"/*/APPS/TEKKEN3; do
  [ -d "$d" ] || continue
  cp "$ELF" "$d/tekken3.elf" && sync && echo "==> Copiado para o pendrive: $d/tekken3.elf" && COPIADO=1
done
[ $COPIADO = 1 ] || echo "    (pendrive não encontrado em /media/$USER/*/APPS/TEKKEN3: copie o arquivo acima à mão)"
echo "    No console o painel deve mostrar \"T3 PS2 $VERSAO\"."
