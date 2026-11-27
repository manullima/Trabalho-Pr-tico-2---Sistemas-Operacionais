#!/usr/bin/env bash
# H2O: (1) escala de desempenho; (2) solucao corrigida x barreira do livro.
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all
BIN=./bin/h2o
OUT=${1:-results/h2o.csv}
REPS=${REPS:-30}
echo "versao,moleculas,rep,T_s,moleculas_por_s,grupos_invalidos,resultado" > "$OUT"
for k in 10 100 1000 3000; do
  for r in $(seq 1 "$REPS"); do
    echo -n "corrigida," >> "$OUT"; $BIN -g $k -S $r -r $r -q >> "$OUT"
  done
done
for k in 10 100 300; do
  for r in $(seq 1 "$REPS"); do
    if line=$(timeout 3 $BIN -g $k -S $r -r $r -q -B livro); then echo "livro,$line" >> "$OUT"
    else rc=$?
      if [ $rc -eq 2 ]; then echo "livro,$line" >> "$OUT"; else echo "livro,$k,$r,NA,NA,NA,TRAVOU" >> "$OUT"; fi
    fi
  done
done
echo "resultados em $OUT" >&2
