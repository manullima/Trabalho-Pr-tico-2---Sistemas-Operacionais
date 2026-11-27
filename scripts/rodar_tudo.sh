#!/usr/bin/env bash
# Roda todos os experimentos em sequencia (evita interferencia entre eles)
# e gera os graficos. Tempo aproximado: 50 minutos.
set -euo pipefail
cd "$(dirname "$0")/.."
./scripts/testes_corretude.sh
./scripts/experimentos_montanha.sh
./scripts/experimentos_babuinos.sh
./scripts/experimentos_h2o.sh
python3 scripts/graficos.py
