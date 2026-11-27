/*
 * babuinos.c - Problema da travessia dos babuinos (Tanenbaum).
 *
 * Propriedades garantidas pela solucao "justa" (padrao):
 *   P1. nunca ha babuinos de sentidos opostos na corda;
 *   P2. nunca ha mais de 5 babuinos na corda;
 *   P3. um fluxo continuo em um sentido nao impede indefinidamente o outro.
 *
 * Solucao justa = interruptor de luz por sentido (lightswitch) + catraca
 * (turnstile) + multiplex(5), como no "No-starve unisex bathroom" de
 * Downey (sec. 7.2). O primeiro babuino de um sentido tranca a corda para
 * o sentido oposto; a catraca faz com que, assim que um babuino do outro
 * lado chega e fica esperando a corda esvaziar, os recem-chegados do
 * sentido atual fiquem retidos atras dele.
 *
 * A opcao -x ingenua remove a catraca e serve apenas para demonstrar a
 * inanicao que a catraca evita. A opcao -x defeituosa "esquece" de
 * destrancar a corda e serve para validar o watchdog de deadlock.
 *
 * Autores: Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima
 */
#include <unistd.h>
#include "common.h"

#define CAPACIDADE 5

/* ---------------- parametros ---------------- */
static int N = 50;                 /* numero de babuinos                    */
static double p0 = 0.5;            /* probabilidade de sentido 0            */
static double chegada_ms = 20;     /* intervalo medio entre chegadas        */
static double Tc = 100;            /* tempo de travessia (+-20%)            */
static int justa = 1;              /* 1 = com catraca, 0 = ingenua          */
static int defeituosa = 0;         /* 1 = esquece de destrancar (teste do watchdog) */
static int padrao_fluxo = 0;       /* 1 = fluxo continuo + minoria oposta   */
static unsigned long long semente = 42;
static const char *cenario = "manual";
static int rep = 0, trace = 0;

/* ---------------- sincronizacao (a solucao) ---------------- */
typedef struct { int contador; sem_t mutex; } lightswitch_t;

static void ls_lock(lightswitch_t *ls, sem_t *s) {
    sem_wait_chk(&ls->mutex);
    if (++ls->contador == 1) sem_wait_chk(s);   /* primeiro tranca          */
    sem_signal(&ls->mutex);
}
static void ls_unlock(lightswitch_t *ls, sem_t *s) {
    sem_wait_chk(&ls->mutex);
    if (--ls->contador == 0) sem_signal(s);     /* ultimo destranca         */
    sem_signal(&ls->mutex);
}

static sem_t catraca;              /* turnstile: ordem de chegada            */
static sem_t corda_livre;          /* 1 = nenhum sentido dono da corda       */
static sem_t multiplex;            /* no maximo 5 na corda                   */
static lightswitch_t sentido[2];

static void entra(int d) {
    if (justa) {
        sem_wait_chk(&catraca);
        ls_lock(&sentido[d], &corda_livre);
        sem_signal(&catraca);
    } else {
        ls_lock(&sentido[d], &corda_livre);
    }
    sem_wait_chk(&multiplex);
}

static void sai(int d) {
    sem_signal(&multiplex);
    if (!defeituosa) ls_unlock(&sentido[d], &corda_livre);
}

/* ---------------- oraculos (mutex independente) ---------------- */
static pthread_mutex_t omx = PTHREAD_MUTEX_INITIALIZER;
static int na_corda[2] = {0, 0}, max_total = 0, ultimo_sentido = -1;
static long violacoes = 0, trocas_sentido = 0;
static int esperando = 0, concluidos = 0, deadlock = 0;
static double ultima_conclusao;
static double t0;

typedef struct {
    int id, d;
    double t_prog;                 /* instante programado de chegada        */
    double t_chegada, t_entrada, t_saida;
    unsigned long long rng;
} babuino_t;
static babuino_t *bab;

static void atravessa(babuino_t *b) {
    pthread_mutex_lock(&omx);
    esperando--;
    na_corda[b->d]++;
    if (na_corda[1 - b->d] > 0) { violacoes++; fprintf(stderr, "VIOLACAO: sentidos opostos na corda\n"); }
    int total = na_corda[0] + na_corda[1];
    if (total > CAPACIDADE) { violacoes++; fprintf(stderr, "VIOLACAO: %d babuinos na corda\n", total); }
    if (total > max_total) max_total = total;
    if (ultimo_sentido != b->d) { if (ultimo_sentido != -1) trocas_sentido++; ultimo_sentido = b->d; }
    if (trace) fprintf(stderr, "[%8.1f ms] babuino %3d sentido %d ENTRA  (corda: %d->, %d<-)\n",
                       (now_s() - t0) * 1e3, b->id, b->d, na_corda[0], na_corda[1]);
    pthread_mutex_unlock(&omx);

    sleep_ms(Tc * (0.8 + 0.4 * rand01(&b->rng)));   /* Tc +- 20%            */

    pthread_mutex_lock(&omx);
    na_corda[b->d]--;
    concluidos++;
    ultima_conclusao = now_s();
    if (trace) fprintf(stderr, "[%8.1f ms] babuino %3d sentido %d SAI    (corda: %d->, %d<-)\n",
                       (now_s() - t0) * 1e3, b->id, b->d, na_corda[0], na_corda[1]);
    pthread_mutex_unlock(&omx);
}

static void *babuino(void *arg) {
    babuino_t *b = arg;
    double alvo = t0 + b->t_prog / 1e3;
    double falta = alvo - now_s();
    if (falta > 0) sleep_ms(falta * 1e3);

    pthread_mutex_lock(&omx);
    esperando++;
    pthread_mutex_unlock(&omx);

    b->t_chegada = now_s();
    entra(b->d);
    b->t_entrada = now_s();
    atravessa(b);
    sai(b->d);
    b->t_saida = now_s();
    return NULL;
}

/* Watchdog: suspeita de deadlock se ninguem conclui em 10 s havendo espera. */
static volatile int fim = 0;
static void *watchdog(void *arg) {
    (void)arg;
    while (!fim) {
        sleep_ms(100);
        pthread_mutex_lock(&omx);
        int susp = esperando > 0 && now_s() - ultima_conclusao > 10.0;
        int esp = esperando;
        pthread_mutex_unlock(&omx);
        if (susp) {
            deadlock = 1;
            fprintf(stderr, "WATCHDOG: suspeita de deadlock (%d babuinos esperando ha >10 s)\n", esp);
            fflush(stderr);
            printf("%s,%s,%d,%.2f,%g,%g,%d,NA,NA,NA,NA,NA,NA,NA,NA,%d,%ld,%ld,1\n",
                   cenario, defeituosa ? "defeituosa" : justa ? "justa" : "ingenua", N, p0, chegada_ms, Tc, rep,
                   max_total, violacoes, trocas_sentido);
            exit(3);
        }
    }
    return NULL;
}

static const char *CABECALHO =
    "cenario,solucao,N,p0,chegada_ms,Tc,rep,T_s,vazao,espera_media_ms,espera_p95_ms,"
    "espera_max_ms,espera_max_d0_ms,espera_max_d1_ms,cpu_pct,max_total,violacoes,"
    "trocas_sentido,deadlock";

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "N:p:a:c:x:fS:s:r:vH")) != -1) {
        switch (opt) {
        case 'N': N = atoi(optarg); break;
        case 'p': p0 = atof(optarg); break;
        case 'a': chegada_ms = atof(optarg); break;
        case 'c': Tc = atof(optarg); break;
        case 'x':
            justa = strcmp(optarg, "ingenua") != 0;
            defeituosa = strcmp(optarg, "defeituosa") == 0;
            break;
        case 'f': padrao_fluxo = 1; break;
        case 'S': semente = strtoull(optarg, NULL, 10); break;
        case 's': cenario = optarg; break;
        case 'r': rep = atoi(optarg); break;
        case 'v': trace = 1; break;
        case 'H': puts(CABECALHO); return 0;
        default:
            fprintf(stderr,
                "uso: %s [-N babuinos] [-p prob_sentido0] [-a intervalo_medio_ms]\n"
                "          [-c Tc_ms] [-x justa|ingenua|defeituosa] [-f] [-S semente] [-s cenario] [-r rep] [-v] [-H]\n"
                "  -f  fluxo continuo no sentido 0 com 1 babuino do sentido 1 a cada 20\n", argv[0]);
            return 1;
        }
    }

    sem_init_chk(&catraca, 1);
    sem_init_chk(&corda_livre, 1);
    sem_init_chk(&multiplex, CAPACIDADE);
    for (int d = 0; d < 2; d++) { sentido[d].contador = 0; sem_init_chk(&sentido[d].mutex, 1); }

    /* gera a carga: instantes de chegada (Poisson) e sentidos */
    bab = calloc(N, sizeof(babuino_t));
    unsigned long long rng = semente * 2654435761ULL + (unsigned long long)rep * 97 + 1;
    double t = 0;
    for (int i = 0; i < N; i++) {
        bab[i].id = i;
        bab[i].d = padrao_fluxo ? (i % 20 == 10) : (rand01(&rng) < p0 ? 0 : 1);
        bab[i].t_prog = t;
        bab[i].rng = rng ^ (0x9E3779B97F4A7C15ULL * (i + 1));
        if (chegada_ms > 0)
            t += padrao_fluxo ? chegada_ms : -chegada_ms * log1p(-rand01(&rng) * 0.999999);
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    pthread_t *th = malloc(sizeof(pthread_t) * N), wd;
    usage_t u0 = get_usage();
    t0 = now_s();
    ultima_conclusao = t0;
    CHECK(pthread_create(&wd, NULL, watchdog, NULL));
    for (int i = 0; i < N; i++) CHECK(pthread_create(&th[i], &attr, babuino, &bab[i]));
    for (int i = 0; i < N; i++) CHECK(pthread_join(th[i], NULL));
    double T = now_s() - t0;
    fim = 1;
    pthread_join(wd, NULL);
    usage_t u1 = get_usage();

    /* metricas de espera (t_entrada - t_chegada) */
    double *w = malloc(sizeof(double) * N), soma = 0, maxd[2] = {0, 0};
    for (int i = 0; i < N; i++) {
        w[i] = (bab[i].t_entrada - bab[i].t_chegada) * 1e3;
        soma += w[i];
        if (w[i] > maxd[bab[i].d]) maxd[bab[i].d] = w[i];
    }
    qsort(w, N, sizeof(double), cmp_double);
    double cpu = (u1.user_s - u0.user_s) + (u1.sys_s - u0.sys_s);
    printf("%s,%s,%d,%.2f,%g,%g,%d,%.4f,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%ld,%ld,%d\n",
           cenario, defeituosa ? "defeituosa" : justa ? "justa" : "ingenua", N, p0, chegada_ms, Tc, rep, T, N / T,
           soma / N, percentile(w, N, 95), w[N - 1], maxd[0], maxd[1],
           100.0 * cpu / T, max_total, violacoes, trocas_sentido, deadlock);
    return violacoes ? 2 : 0;
}
