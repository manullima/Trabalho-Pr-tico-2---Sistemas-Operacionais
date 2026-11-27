# TP2 — Sincronização com Semáforos

Sistemas Operacionais · CEFET-MG · Profa. Michelle Hanne
Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima

Implementação, em C com `pthread` e semáforos POSIX (`sem_t`), de três problemas clássicos de sincronização:

| Problema | Programa | Ideia central |
| --- | --- | --- |
| Montanha-russa (1 carro) | `bin/montanha_russa_1carro` | `boardQueue`/`unboardQueue` liberam exatamente C passageiros; o último avisa o carro (`allAboard`/`allAshore`) |
| Montanha-russa (m carros) | `bin/montanha_russa` | Anéis de bastões `loadingArea[i]` e `unloadingArea[i]`; fila de desembarque **por carro** |
| Travessia dos babuínos | `bin/babuinos` | Lightswitch por sentido + catraca (sem inanição) + multiplex(5) |
| Construindo H2O | `bin/h2o` | Contadores + filas `oxyQueue`/`hydroQueue` + barreira de 3 em que a última thread a sair libera a próxima molécula |

## Compilar e testar

```bash
make            # compila tudo em bin/
make test       # testes de corretude (~1 minuto)
scripts/rodar_tudo.sh   # todos os experimentos + gráficos (~50 minutos)
```

Requisitos: Linux, gcc, make. Para os gráficos: Python 3 com pandas e matplotlib.

## Exemplos

```bash
# 1 carro, rastro de eventos
./bin/montanha_russa_1carro -n 6 -C 3 -R 2

# m carros com rastro (stderr) e uma linha CSV de métricas (stdout)
./bin/montanha_russa -m 3 -C 2 -n 12 -R 2 -v
./bin/montanha_russa -H        # cabeçalho do CSV

# babuínos: 20 babuínos chegando a cada ~20 ms, com rastro
./bin/babuinos -N 20 -a 20 -v
# fluxo contínuo num sentido: compare a solução justa com a ingênua
./bin/babuinos -N 200 -a 25 -f -x justa
./bin/babuinos -N 200 -a 25 -f -x ingenua

# H2O: exemplos do enunciado e uma entrada aleatória grande
./bin/h2o -i HOH
./bin/h2o -i OOHHHH
./bin/h2o -g 3000 -S 7
```

## Parâmetros

`montanha_russa`: `-m` carros, `-C` capacidade, `-n` passageiros, `-R` voltas por carro, `-L/-T/-U` duração de load/run/unload em ms, `-s` rótulo do cenário, `-r` repetição, `-v` rastro.

`babuinos`: `-N` babuínos, `-p` probabilidade do sentido 0, `-a` intervalo médio entre chegadas (ms, Poisson; 0 = rajada), `-c` Tc em ms (padrão 100, ±20%), `-x justa|ingenua|defeituosa`, `-f` fluxo contínuo (1 babuíno do sentido 1 a cada 20), `-S` semente.

`h2o`: `-i` sequência de chegada (ex.: `OOHHHH`) ou `-g k` (k moléculas em ordem aleatória), `-S` semente, `-d` atraso entre criações (ms), `-q` saída CSV, `-B livro` usa a barreira reutilizável do livro (reproduz o defeito descrito no relatório).

## Verificação

Cada programa tem oráculos com mutex próprio, independentes da solução testada, que contam violações das restrições do enunciado. Os programas terminam com código 2 se houver violação. O watchdog dos babuínos encerra com código 3 se ninguém concluir a travessia em 10 s havendo babuínos esperando.

## Estrutura

```
src/        código-fonte (common.h + um .c por problema)
scripts/    testes de corretude, experimentos e gráficos
results/    CSVs brutos, resumos e figuras
```

## Referências

- A. B. Downey, *The Little Book of Semaphores*, 2ª ed.
- G. R. Andrews, *Concurrent Programming: Principles and Practice*, 1991.
- A. S. Tanenbaum, *Modern Operating Systems*, 2ª ed., 2001.
