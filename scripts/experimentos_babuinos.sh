#!/usr/bin/env bash
# Plano de testes com variacao de carga - travessia dos babuinos (secao 2.1).
#   A: taxa de chegada variando (N = 100, sentidos 50/50)
#   B: rajada (todos chegam juntos), N variando
#   C: inanicao - fluxo continuo no sentido 0 com 1 babuino do sentido 1 a cada 20
#   D: assimetria de sentidos (p0 = 0.5, 0.7, 0.9)
# Cada configuracao roda com a solucao justa e com a ingenua (sem catraca).
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all
BIN=./bin/babuinos
OUT=${1:-results/babuinos.csv}
REPS=${REPS:-10}          # solucao justa
REPS_ING=${REPS_ING:-5}   # ingenua (apenas comparacao)

SOLS=${SOLS:-"justa ingenua"}
REP_INI=${REP_INI:-1}
WATCHDOG=${WATCHDOG:-1}
# APPEND=1 continua um arquivo existente (execucao em partes)
[ "${APPEND:-0}" = 1 ] || $BIN -H > "$OUT"
for x in $SOLS; do
  NR=$REPS; [ "$x" = ingenua ] && NR=$REPS_ING
  for r in $(seq "$REP_INI" "$NR"); do
    for a in 100 50 20 10 5; do
      $BIN -s A_taxa -x $x -N 100 -a $a -S $r -r $r >> "$OUT"
    done
    for N in 5 10 25 50 100; do
      $BIN -s B_rajada -x $x -N $N -a 0 -S $r -r $r >> "$OUT"
    done
    for a in 30 25; do
      $BIN -s C_fluxo -x $x -N 200 -a $a -f -S $r -r $r >> "$OUT"
    done
    for p in 0.5 0.7 0.9; do
      $BIN -s D_assimetria -x $x -N 100 -a 20 -p $p -S $r -r $r >> "$OUT"
    done
    echo "$x rep $r ok" >&2
  done
done
# validacao do watchdog: variante que nunca destranca a corda
[ "$WATCHDOG" = 1 ] && $BIN -s E_watchdog -x defeituosa -N 20 -a 20 -r 1 >> "$OUT" 2>/dev/null || true
echo "resultados em $OUT" >&2
