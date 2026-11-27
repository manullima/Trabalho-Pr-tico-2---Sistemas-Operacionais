#!/usr/bin/env bash
# Plano de teste da montanha-russa com m carros (secao 1.2 do enunciado).
#   Cenario 1: n = 200 fixo (m*C <= n)        -> ganho ao adicionar carros
#   Cenario 2: n = 2*m*C                        -> custo de sincronizacao
#   Cenario 3: tL = tR = tU = 0, n = 2*m*C      -> sobrecarga pura dos semaforos
# C = 4 em todos; sensibilidade com C = 2 e C = 8. R = 20 voltas, r = 30 repeticoes.
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all
BIN=./bin/montanha_russa
OUT=${1:-results/montanha.csv}
REPS=${REPS:-30}
R=20
MS=${MS:-"1 2 5 10 15 20 25 30 40 50"}
CS=${CS:-"4 2 8"}

# APPEND=1 continua um arquivo existente (execucao em partes)
[ "${APPEND:-0}" = 1 ] || $BIN -H > "$OUT"
for C in $CS; do
  for m in $MS; do
    for r in $(seq 1 "$REPS"); do
      # cenario 1 (so quando m*C <= 200)
      if (( m * C <= 200 )); then
        $BIN -s c1_fixo -m "$m" -C "$C" -n 200 -R $R -L 1 -T 10 -U 1 -r "$r" >> "$OUT"
      fi
      # cenario 2
      $BIN -s c2_proporcional -m "$m" -C "$C" -n $((2 * m * C)) -R $R -L 1 -T 10 -U 1 -r "$r" >> "$OUT"
      # cenario 3
      $BIN -s c3_sem_trabalho -m "$m" -C "$C" -n $((2 * m * C)) -R $R -L 0 -T 0 -U 0 -r "$r" >> "$OUT"
    done
    echo "C=$C m=$m ok" >&2
  done
done
echo "resultados em $OUT" >&2
