#!/usr/bin/env bash
# Manda o tekken3.elf para o PS2 de verdade pela rede (ps2link) e mostra/grava o log.
# Uso (de dentro de ps2/):  bash tools/rodar_ps2_rede.sh [IP_DO_PS2]
# O PS2 precisa estar na tela do PS2Link. Ctrl+C encerra (depois reinicie o PS2Link).
#
# Como funciona: rodando de host:, o port NÃO reinicia o IOP (o ps2link continua
# vivo). O jogo lê o .bin e o SLUS desta pasta pela rede e o printf vem direto
# para este terminal — sem gravação no pendrive (sem as travadas de ~50 ms).
set -u
AQUI="$(cd "$(dirname "$0")/.." && pwd)"
export PS2DEV="${PS2DEV:-$HOME/ps2dev/ps2dev}"
PS2CLIENT="$PS2DEV/bin/ps2client"
IP="${1:-$(cat "$HOME/.ps2_ip" 2>/dev/null)}"
[ -n "$IP" ] || { echo "Uso: bash tools/rodar_ps2_rede.sh IP_DO_PS2 (ou rode o montar-ps2link.sh)"; exit 1; }
[ -f "$AQUI/tekken3.elf" ] || { echo "Falta $AQUI/tekken3.elf (rode make)"; exit 1; }
[ -f "$AQUI/Tekken 3 (USA) (Track 1).bin" ] || echo "Aviso: .bin do jogo não está em $AQUI"

if ! ping -c1 -W2 "$IP" >/dev/null 2>&1; then
  echo "O PS2 ($IP) não responde. Ele está na tela do PS2Link e com cabo de rede?"; exit 1
fi
LOG="$AQUI/docs/ultimo_log_ps2_rede.txt"
cd "$AQUI" || exit 1                       # host: = esta pasta
# Sem "reset": o PS2Link 1.2 aberto pelo pendrive não consegue se recarregar
# depois do reset e cai (foi o "Could not connect" do primeiro teste).
echo "==> Enviando tekken3.elf (log em docs/ultimo_log_ps2_rede.txt). Ctrl+C para sair."
"$PS2CLIENT" -h "$IP" execee host:tekken3.elf 2>&1 | tee "$LOG"
