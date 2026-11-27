#!/usr/bin/env bash
# Testes de corretude dos tres problemas. Cada programa tem oraculos internos
# (mutex proprio, independente da solucao) que contam violacoes; aqui
# executamos muitas configuracoes e exigimos zero violacoes.
set -uo pipefail
cd "$(dirname "$0")/.."
make -s all

PASS=0; FAIL=0
ok()   { PASS=$((PASS + 1)); }
nok()  { FAIL=$((FAIL + 1)); echo "  FALHOU: $*"; }

echo "== Montanha-russa, 1 carro =="
for cfg in "-n 5 -C 4 -R 50" "-n 8 -C 4 -R 50" "-n 30 -C 7 -R 50" "-n 2 -C 1 -R 100"; do
  if ./bin/montanha_russa_1carro $cfg | tail -1 | grep -q "0 violacoes"; then ok; else nok "1 carro $cfg"; fi
done
echo "   $PASS ok"

echo "== Montanha-russa, m carros (sem sleep = maxima concorrencia) =="
P0=$PASS
for m in 1 2 3 5 10 25 50; do
  for C in 1 2 4 8; do
    for mult in 1 2; do
      n=$((mult * m * C)); (( n <= C )) && n=$((C + 1))
      for r in 1 2 3 4 5; do
        line=$(timeout 30 ./bin/montanha_russa -m $m -C $C -n $n -R 20 -L 0 -T 0 -U 0 -s teste -r $r)
        rc=$?
        if [ $rc -eq 0 ] && [ "${line##*,}" = "0" ]; then ok; else nok "m=$m C=$C n=$n rc=$rc: $line"; fi
      done
    done
  done
done
for m in 2 7 20; do   # com os tempos do enunciado
  line=$(./bin/montanha_russa -m $m -C 4 -n $((2 * m * 4)) -R 10 -s teste)
  if [ "${line##*,}" = "0" ]; then ok; else nok "m=$m com sleep: $line"; fi
done
echo "   $((PASS - P0)) ok"

echo "== Babuinos (solucao justa) =="
P0=$PASS
for cfg in "-N 50 -a 5 -c 20" "-N 100 -a 0 -c 20" "-N 100 -a 10 -p 0.9 -c 20" "-N 200 -a 10 -f -c 20" "-N 30 -a 20"; do
  for r in 1 2 3; do
    line=$(timeout 60 ./bin/babuinos $cfg -S $r -r $r -s teste)
    IFS=, read -r _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ maxt viol _ dl <<< "$line"
    if [ "$viol" = "0" ] && [ "$maxt" -le 5 ] && [ "$dl" = "0" ]; then ok; else nok "babuinos $cfg: $line"; fi
  done
done
echo "   $((PASS - P0)) ok"
echo "   watchdog (variante defeituosa, ~10 s)..."
timeout 30 ./bin/babuinos -N 20 -a 20 -x defeituosa -s teste > /dev/null 2>&1
if [ $? -eq 3 ]; then ok; echo "   watchdog detectou o deadlock injetado: ok"; else nok "watchdog nao disparou"; fi

echo "== H2O =="
P0=$PASS
for in in HOH OOHHHH HHHHHHOOO OHHOHHOHH; do
  if ./bin/h2o -i $in | tail -1 | grep -q "SAIDA VALIDA"; then ok; else nok "h2o $in"; fi
done
if ./bin/h2o -i OOHHH | tail -1 | grep -q "sobraram 1 O e 1 H"; then ok; else nok "h2o entrada desbalanceada"; fi
for s in $(seq 1 100); do
  if timeout 10 ./bin/h2o -g $((s * 5)) -S $s -q > /dev/null; then ok; else nok "h2o aleatorio semente $s"; fi
done
echo "   $((PASS - P0)) ok"

echo
echo "TOTAL: $PASS testes ok, $FAIL falhas"
[ $FAIL -eq 0 ]
