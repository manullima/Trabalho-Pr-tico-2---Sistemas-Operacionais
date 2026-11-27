CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE -pthread
LDLIBS  = -pthread -lm
BIN     = bin
PROGS   = $(BIN)/montanha_russa_1carro $(BIN)/montanha_russa $(BIN)/babuinos $(BIN)/h2o

all: $(PROGS)

$(BIN)/%: src/%.c src/common.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BIN):
	mkdir -p $(BIN)

# testes rapidos de corretude (alguns segundos)
test: all
	./scripts/testes_corretude.sh

clean:
	rm -rf $(BIN)

.PHONY: all test clean
