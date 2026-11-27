/*
 * h2o.c - Construindo H2O (barreira para moleculas de agua).
 *
 * Threads de oxigenio (O) e hidrogenio (H) so passam pela barreira em
 * grupos completos de 1 O + 2 H, e todas as threads de uma molecula
 * invocam bond() antes de qualquer thread da molecula seguinte.
 *
 * Solucao (baseada em Downey, sec. 5.6): contadores oxygen/hydrogen
 * protegidos por mutex, filas oxyQueue/hydroQueue e uma barreira de 3
 * threads. A thread que completa o grupo libera 1 O e 2 H e NAO devolve o
 * mutex; quem devolve e a ultima thread a sair da barreira (no livro, o
 * oxigenio - ver comentario da barreira). Assim nenhuma molecula nova
 * pode ser formada enquanto a atual nao terminou bond().
 *
 * Uso:
 *   ./h2o -i OOHHHH            entrada explicita (ordem de chegada)
 *   ./h2o -g 1000 -S 7         1000 moleculas em ordem aleatoria
 *   ./h2o -g 1000 -q -r 3      modo CSV (benchmark), repeticao 3
 *   ./h2o -g 300 -B livro      barreira do livro (reproduz o defeito)
 *
 * Autores: Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima
 */
#include <unistd.h>
#include "common.h"

/* ---------------- barreira de 3 threads ----------------
 * barrier_wait() devolve 1 para a ULTIMA thread a sair da barreira.
 *
 * Por que nao a barreira reutilizavel de duas catracas de Downey?
 * Ela supoe que as MESMAS n threads voltam a cada rodada. Aqui cada
 * rodada tem threads novas: em testes com milhares de threads, uma
 * thread da molecula k+1 consumia a permissao da 2a catraca deixada por
 * um H da molecula k; se fosse o O, ele devolvia o mutex cedo, a
 * molecula k+2 entrava na barreira junto com a k+1 e os grupos se
 * misturavam (saidas como HHH) ou o programa travava.
 * Correcao: quem libera a proxima molecula e a ultima thread a sair
 * (todas as permissoes ja foram consumidas), e nao o oxigenio.        */
typedef struct {
    int n, count;
    sem_t mutex, turnstile;
} barrier_t;

static void barrier_init(barrier_t *b, int n) {
    b->n = n; b->count = 0;
    sem_init_chk(&b->mutex, 1);
    sem_init_chk(&b->turnstile, 0);
}

static int barrier_wait(barrier_t *b) {
    sem_wait_chk(&b->mutex);                       /* chegada              */
    if (++b->count == b->n) sem_signal_n(&b->turnstile, b->n);
    sem_signal(&b->mutex);
    sem_wait_chk(&b->turnstile);

    sem_wait_chk(&b->mutex);                       /* saida                */
    int ultimo = (--b->count == 0);
    sem_signal(&b->mutex);
    return ultimo;
}

/* Barreira reutilizavel de duas catracas, como no livro (opcao -B livro).
 * Mantida apenas para reproduzir o defeito descrito acima.             */
typedef struct { int n, count; sem_t mutex, t1, t2; } barrier_livro_t;
static barrier_livro_t bl;
static int usar_livro = 0;

static void barrier_livro_wait(barrier_livro_t *b) {
    sem_wait_chk(&b->mutex);
    if (++b->count == b->n) sem_signal_n(&b->t1, b->n);
    sem_signal(&b->mutex);
    sem_wait_chk(&b->t1);
    sem_wait_chk(&b->mutex);
    if (--b->count == 0) sem_signal_n(&b->t2, b->n);
    sem_signal(&b->mutex);
    sem_wait_chk(&b->t2);
}

/* ---------------- sincronizacao (a solucao) ---------------- */
static sem_t mutex, oxyQueue, hydroQueue;
static int oxygen = 0, hydrogen = 0;
static barrier_t barreira;

/* ---------------- registro da saida ---------------- */
static pthread_mutex_t omx = PTHREAD_MUTEX_INITIALIZER;
static char *saida;
static long nsaida = 0;
static sem_t ligacoes;             /* main conta os bond() concluidos       */
static double bond_ms = 0;

static void bond(char tipo) {
    sleep_ms(bond_ms);
    pthread_mutex_lock(&omx);
    saida[nsaida++] = tipo;
    pthread_mutex_unlock(&omx);
    sem_signal(&ligacoes);
}

static void *oxigenio(void *arg) {
    (void)arg;
    sem_wait_chk(&mutex);
    oxygen++;
    if (hydrogen >= 2) {                /* forma a molecula agora           */
        sem_signal_n(&hydroQueue, 2); hydrogen -= 2;
        sem_signal(&oxyQueue);        oxygen -= 1;
    } else {
        sem_signal(&mutex);             /* espera por dois H                */
    }
    sem_wait_chk(&oxyQueue);
    bond('O');
    if (usar_livro) {                   /* versao do livro: O devolve o mutex */
        barrier_livro_wait(&bl);
        sem_signal(&mutex);
    } else if (barrier_wait(&barreira)) {
        sem_signal(&mutex);             /* molecula completa: libera a proxima */
    }
    return NULL;
}

static void *hidrogenio(void *arg) {
    (void)arg;
    sem_wait_chk(&mutex);
    hydrogen++;
    if (hydrogen >= 2 && oxygen >= 1) {
        sem_signal_n(&hydroQueue, 2); hydrogen -= 2;
        sem_signal(&oxyQueue);        oxygen -= 1;
    } else {
        sem_signal(&mutex);             /* espera por O e outro H           */
    }
    sem_wait_chk(&hydroQueue);
    bond('H');
    if (usar_livro) barrier_livro_wait(&bl);
    else if (barrier_wait(&barreira))
        sem_signal(&mutex);             /* molecula completa: libera a proxima */
    return NULL;
}

/* Verifica a saida: cada grupo de 3 tem exatamente 1 O e 2 H. */
static long grupos_invalidos(const char *s, long len) {
    long ruins = 0;
    for (long g = 0; g + 3 <= len; g += 3) {
        int o = (s[g] == 'O') + (s[g + 1] == 'O') + (s[g + 2] == 'O');
        if (o != 1) ruins++;
    }
    return ruins;
}

int main(int argc, char **argv) {
    const char *entrada_fixa = NULL;
    long gerar = 0;
    unsigned long long semente = 1;
    int quieto = 0, rep = 0, opt;
    double atraso_ms = 0;              /* intervalo entre criacoes           */
    while ((opt = getopt(argc, argv, "i:g:S:d:b:qr:B:")) != -1) {
        switch (opt) {
        case 'i': entrada_fixa = optarg; break;
        case 'g': gerar = atol(optarg); break;
        case 'S': semente = strtoull(optarg, NULL, 10); break;
        case 'd': atraso_ms = atof(optarg); break;
        case 'b': bond_ms = atof(optarg); break;
        case 'q': quieto = 1; break;
        case 'r': rep = atoi(optarg); break;
        case 'B': usar_livro = strcmp(optarg, "livro") == 0; break;
        default:
            fprintf(stderr, "uso: %s (-i SEQUENCIA | -g moleculas) [-S semente] [-d atraso_ms] [-b bond_ms] [-q] [-r rep] [-B livro]\n", argv[0]);
            return 1;
        }
    }

    /* monta a sequencia de chegada */
    char *entrada;
    long len;
    if (entrada_fixa) {
        len = (long)strlen(entrada_fixa);
        entrada = strdup(entrada_fixa);
    } else {
        if (gerar <= 0) gerar = 2;
        len = 3 * gerar;
        entrada = malloc(len + 1);
        for (long i = 0; i < len; i++) entrada[i] = i < gerar ? 'O' : 'H';
        unsigned long long rng = semente * 6364136223846793005ULL + (unsigned long long)rep + 1;
        for (long i = len - 1; i > 0; i--) {            /* Fisher-Yates      */
            long j = (long)(rand01(&rng) * (i + 1));
            char t = entrada[i]; entrada[i] = entrada[j]; entrada[j] = t;
        }
        entrada[len] = '\0';
    }
    long nO = 0, nH = 0;
    for (long i = 0; i < len; i++) {
        if (entrada[i] == 'O') nO++;
        else if (entrada[i] == 'H') nH++;
        else { fprintf(stderr, "caractere invalido '%c' (use apenas O e H)\n", entrada[i]); return 1; }
    }
    long moleculas = nO < nH / 2 ? nO : nH / 2;

    sem_init_chk(&mutex, 1);
    sem_init_chk(&oxyQueue, 0);
    sem_init_chk(&hydroQueue, 0);
    sem_init_chk(&ligacoes, 0);
    barrier_init(&barreira, 3);
    bl.n = 3; bl.count = 0;
    sem_init_chk(&bl.mutex, 1); sem_init_chk(&bl.t1, 0); sem_init_chk(&bl.t2, 0);
    saida = calloc(len + 1, 1);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    double t0 = now_s();
    for (long i = 0; i < len; i++) {
        pthread_t t;
        CHECK(pthread_create(&t, &attr, entrada[i] == 'O' ? oxigenio : hidrogenio, NULL));
        sleep_ms(atraso_ms);
    }
    for (long k = 0; k < 3 * moleculas; k++) sem_wait_chk(&ligacoes);
    double T = now_s() - t0;

    pthread_mutex_lock(&omx);
    long ruins = grupos_invalidos(saida, nsaida);
    int ok = ruins == 0 && nsaida == 3 * moleculas;
    if (quieto) {
        printf("%ld,%d,%.6f,%.1f,%ld,%s\n", moleculas, rep, T, moleculas / T, ruins, ok ? "valida" : "INVALIDA");
    } else {
        printf("Entrada : %s\n", len <= 120 ? entrada : "(longa demais para exibir)");
        if (nsaida <= 120) {
            printf("Saida   : ");
            for (long i = 0; i < nsaida; i++) printf("%c%s", saida[i], (i % 3 == 2 && i + 1 < nsaida) ? " " : "");
            printf("\n");
        }
        printf("Moleculas formadas: %ld | grupos invalidos: %ld | sobraram %ld O e %ld H sem par -> %s\n",
               moleculas, ruins, nO - moleculas, nH - 2 * moleculas, ok ? "SAIDA VALIDA" : "SAIDA INVALIDA");
    }
    pthread_mutex_unlock(&omx);
    fflush(stdout);
    _exit(ok ? 0 : 2);                 /* threads que sobraram nao bloqueiam o termino */
}
