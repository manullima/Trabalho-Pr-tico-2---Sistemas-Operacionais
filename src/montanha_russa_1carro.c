/*
 * montanha_russa_1carro.c - Problema da Montanha-Russa (1 carro).
 *
 * n threads de passageiros e 1 thread de carro de capacidade C (C < n).
 * Passageiros: board() e unboard(). Carro: load(), run() e unload().
 * Restricoes garantidas:
 *   - passageiros so embarcam depois de load();
 *   - o carro so parte com C passageiros a bordo;
 *   - passageiros so desembarcam depois de unload().
 *
 * Solucao baseada em Downey, "The Little Book of Semaphores", sec. 5.8.
 *
 * Uso: ./montanha_russa_1carro [-n passageiros] [-C capacidade] [-R voltas]
 *
 * Autores: Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima
 */
#include <unistd.h>
#include "common.h"

static int n = 8, C = 4, R = 5;

/* ---- sincronizacao ---- */
static sem_t mutex, mutex2;        /* protegem boarders / unboarders        */
static sem_t boardQueue;           /* passageiros aguardando embarque        */
static sem_t unboardQueue;         /* passageiros aguardando desembarque     */
static sem_t allAboard;            /* ultimo a embarcar avisa o carro        */
static sem_t allAshore;            /* ultimo a desembarcar avisa o carro     */
static int boarders = 0, unboarders = 0;
static volatile int done = 0;      /* fim da simulacao (todas as voltas)     */

/* ---- oraculo de verificacao (independente da solucao) ---- */
static pthread_mutex_t omx = PTHREAD_MUTEX_INITIALIZER;
static int estado = 0;             /* 0 parado, 1 carregando, 2 andando, 3 descarregando */
static int a_bordo = 0, desembarcados = 0, violacoes = 0;
static double t0;

static void log_evt(const char *quem, int id, const char *acao) {
    printf("[%8.3f ms] %-10s %2d %s\n", (now_s() - t0) * 1000.0, quem, id, acao);
}

static void violacao(const char *msg) {
    violacoes++;
    fprintf(stderr, "VIOLACAO: %s\n", msg);
}

/* ---- acoes do carro ---- */
static void load(int volta) {
    pthread_mutex_lock(&omx);
    if (estado != 0) violacao("load com carro fora do estado parado");
    estado = 1; a_bordo = 0;
    log_evt("CARRO", volta, "load()");
    pthread_mutex_unlock(&omx);
    sleep_ms(1);
}

static void run(int volta) {
    pthread_mutex_lock(&omx);
    if (a_bordo != C) violacao("carro partiu sem estar cheio");
    estado = 2;
    log_evt("CARRO", volta, "run()  -- carro cheio, percorrendo os trilhos");
    pthread_mutex_unlock(&omx);
    sleep_ms(10);
}

static void unload(int volta) {
    pthread_mutex_lock(&omx);
    estado = 3; desembarcados = 0;
    log_evt("CARRO", volta, "unload()");
    pthread_mutex_unlock(&omx);
    sleep_ms(1);
}

/* ---- acoes do passageiro ---- */
static void board(int id) {
    pthread_mutex_lock(&omx);
    if (estado != 1) violacao("embarque antes de load()");
    if (++a_bordo > C) violacao("mais de C passageiros a bordo");
    log_evt("passageiro", id, "board()");
    pthread_mutex_unlock(&omx);
}

static void unboard(int id) {
    pthread_mutex_lock(&omx);
    if (estado != 3) violacao("desembarque antes de unload()");
    desembarcados++;
    log_evt("passageiro", id, "unboard()");
    pthread_mutex_unlock(&omx);
}

static void *carro(void *arg) {
    (void)arg;
    for (int v = 1; v <= R; v++) {
        load(v);
        sem_signal_n(&boardQueue, C);   /* libera exatamente C embarques     */
        sem_wait_chk(&allAboard);       /* espera o carro encher             */
        run(v);
        unload(v);
        sem_signal_n(&unboardQueue, C); /* libera exatamente C desembarques  */
        sem_wait_chk(&allAshore);       /* espera todos descerem             */
        pthread_mutex_lock(&omx);
        if (desembarcados != C) violacao("carro recarregou antes de todos descerem");
        estado = 0;
        pthread_mutex_unlock(&omx);
    }
    return NULL;
}

static void *passageiro(void *arg) {
    int id = (int)(long)arg;
    for (;;) {
        sem_wait_chk(&boardQueue);
        if (done) break;                /* acordado pelo encerramento        */
        board(id);
        sem_wait_chk(&mutex);
        if (++boarders == C) { sem_signal(&allAboard); boarders = 0; }
        sem_signal(&mutex);

        sem_wait_chk(&unboardQueue);
        unboard(id);
        sem_wait_chk(&mutex2);
        if (++unboarders == C) { sem_signal(&allAshore); unboarders = 0; }
        sem_signal(&mutex2);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "n:C:R:")) != -1) {
        switch (opt) {
        case 'n': n = atoi(optarg); break;
        case 'C': C = atoi(optarg); break;
        case 'R': R = atoi(optarg); break;
        default:
            fprintf(stderr, "uso: %s [-n passageiros] [-C capacidade] [-R voltas]\n", argv[0]);
            return 1;
        }
    }
    if (C <= 0 || n <= C || R <= 0) { fprintf(stderr, "exige 0 < C < n e R > 0\n"); return 1; }

    sem_init_chk(&mutex, 1); sem_init_chk(&mutex2, 1);
    sem_init_chk(&boardQueue, 0); sem_init_chk(&unboardQueue, 0);
    sem_init_chk(&allAboard, 0); sem_init_chk(&allAshore, 0);

    printf("Montanha-russa (1 carro): n=%d passageiros, C=%d, R=%d voltas\n", n, C, R);
    t0 = now_s();
    pthread_t car, *pas = malloc(sizeof(pthread_t) * n);
    for (long i = 0; i < n; i++) CHECK(pthread_create(&pas[i], NULL, passageiro, (void *)i));
    CHECK(pthread_create(&car, NULL, carro, NULL));
    CHECK(pthread_join(car, NULL));

    done = 1;                           /* encerra passageiros bloqueados    */
    sem_signal_n(&boardQueue, n);
    for (int i = 0; i < n; i++) CHECK(pthread_join(pas[i], NULL));

    printf("Fim: %d voltas, %d violacoes -> %s\n", R, violacoes, violacoes ? "FALHOU" : "OK");
    free(pas);
    return violacoes ? 2 : 0;
}
