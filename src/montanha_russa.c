/*
 * montanha_russa.c - Problema da Montanha-Russa com m carros.
 *
 * Restricoes adicionais em relacao ao caso de 1 carro:
 *   - apenas um carro embarca por vez;
 *   - varios carros podem estar na pista ao mesmo tempo;
 *   - os carros desembarcam na mesma ordem em que embarcaram;
 *   - todas as threads de uma carga desembarcam antes de qualquer thread
 *     das cargas seguintes.
 *
 * Ideia (Downey, sec. 5.8.3, com uma correcao): cada carro i possui
 * loadingArea[i] e unloadingArea[i]; o carro i, ao terminar de embarcar
 * (ou desembarcar), sinaliza a area do carro (i+1) mod m, formando um
 * "anel de bastao" que impoe a ordem FIFO nas duas plataformas.
 * Diferente da versao do livro, a fila de desembarque e POR CARRO
 * (unboardQueue[i]): o passageiro guarda o id do carro em que entrou e so
 * pode ser liberado por ele. Com uma fila unica, um passageiro do carro
 * j (ainda na pista) poderia consumir a permissao do carro i.
 *
 * Saida: uma linha CSV com as metricas da execucao (ver -H).
 *
 * Autores: Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima
 */
#include <unistd.h>
#include "common.h"

/* ---------------- parametros ---------------- */
static int m = 2, C = 4, n = 16, R = 20;
static double tL = 1, tR = 10, tU = 1;           /* em ms                 */
static int trace = 0;
static const char *cenario = "manual";
static int rep = 0;

/* ---------------- sincronizacao (a solucao) ---------------- */
static sem_t mutex, mutex2;          /* protegem boarders / unboarders     */
static sem_t boardQueue;             /* espera para embarcar               */
static sem_t *unboardQueue;          /* [m] espera para desembarcar        */
static sem_t allAboard, allAshore;   /* ultimo passageiro avisa o carro    */
static sem_t *loadingArea;           /* [m] vez de embarcar (anel)         */
static sem_t *unloadingArea;         /* [m] vez de desembarcar (anel)      */
static int boarders = 0, unboarders = 0;
static int carro_embarcando = -1;    /* carro com a plataforma de embarque */
static volatile int done = 0;
static pthread_barrier_t largada;    /* todos comecam juntos               */

/* ---------------- oraculo (independente da solucao) ---------------- */
enum { PARADO, CARREGANDO, PISTA, DESCARREGANDO };
static pthread_mutex_t omx = PTHREAD_MUTEX_INITIALIZER;
static int *estado, *bcount, *ucount, *viagem_do_carro;
static int *viagem_do_pass;
static int carregando_agora = 0, descarregando_agora = 0;
static int na_pista = 0, max_na_pista = 0;
static int prox_viagem = 0, prox_desembarque = 0;
static long violacoes = 0;
static double *latencias; static long nlat = 0, cap_lat = 0;
static double t0;

static void violacao(const char *msg, int a, int b) {
    if (violacoes < 10) fprintf(stderr, "VIOLACAO: %s (%d, %d)\n", msg, a, b);
    violacoes++;
}

static void tr(const char *quem, int id, const char *acao, int car) {
    if (trace)
        fprintf(stderr, "[%9.3f ms] %-10s %3d %-8s carro %d\n",
                (now_s() - t0) * 1e3, quem, id, acao, car);
}

/* ---------------- acoes do carro ---------------- */
static void load(int i) {
    pthread_mutex_lock(&omx);
    if (carregando_agora != 0) violacao("dois carros embarcando ao mesmo tempo", i, carregando_agora);
    if (estado[i] != PARADO) violacao("load fora do estado parado", i, estado[i]);
    carregando_agora++;
    estado[i] = CARREGANDO; bcount[i] = 0; ucount[i] = 0;
    viagem_do_carro[i] = prox_viagem++;
    tr("CARRO", i, "load", i);
    pthread_mutex_unlock(&omx);
    sleep_ms(tL);
}

/* Instrumentacao: fim do embarque, antes de passar a vez ao proximo carro. */
static void parte(int i) {
    pthread_mutex_lock(&omx);
    if (bcount[i] != C) violacao("carro partiu sem estar cheio", i, bcount[i]);
    estado[i] = PISTA; carregando_agora--;
    if (++na_pista > max_na_pista) max_na_pista = na_pista;
    tr("CARRO", i, "run", i);
    pthread_mutex_unlock(&omx);
}

static void run(int i) { (void)i; sleep_ms(tR); }

static void unload(int i) {
    pthread_mutex_lock(&omx);
    if (descarregando_agora != 0) violacao("dois carros desembarcando ao mesmo tempo", i, 0);
    if (estado[i] != PISTA) violacao("unload fora da pista", i, estado[i]);
    if (viagem_do_carro[i] != prox_desembarque)
        violacao("desembarque fora da ordem de embarque", viagem_do_carro[i], prox_desembarque);
    descarregando_agora++;
    estado[i] = DESCARREGANDO;
    tr("CARRO", i, "unload", i);
    pthread_mutex_unlock(&omx);
    sleep_ms(tU);
}

/* Instrumentacao: fim do desembarque, antes de passar a vez ao proximo. */
static void esvaziou(int i) {
    pthread_mutex_lock(&omx);
    if (ucount[i] != C) violacao("carro liberou a plataforma sem esvaziar", i, ucount[i]);
    estado[i] = PARADO; descarregando_agora--; na_pista--;
    prox_desembarque++;
    pthread_mutex_unlock(&omx);
}

/* ---------------- acoes do passageiro ---------------- */
static void board(int p, int car, double t_chegada) {
    double agora = now_s();
    pthread_mutex_lock(&omx);
    if (estado[car] != CARREGANDO) violacao("embarque sem load()", p, car);
    if (++bcount[car] > C) violacao("carro com mais de C passageiros", car, bcount[car]);
    viagem_do_pass[p] = viagem_do_carro[car];
    if (nlat < cap_lat) latencias[nlat++] = (agora - t_chegada) * 1e3;
    tr("passageiro", p, "board", car);
    pthread_mutex_unlock(&omx);
}

static void unboard(int p, int car) {
    pthread_mutex_lock(&omx);
    if (estado[car] != DESCARREGANDO) violacao("desembarque sem unload()", p, car);
    if (viagem_do_pass[p] != viagem_do_carro[car])
        violacao("passageiro desembarcou de carga errada", p, car);
    if (viagem_do_pass[p] != prox_desembarque)
        violacao("passageiro de carga posterior desembarcou antes", p, viagem_do_pass[p]);
    ucount[car]++;
    tr("passageiro", p, "unboard", car);
    pthread_mutex_unlock(&omx);
}

/* ---------------- threads ---------------- */
static void *carro(void *arg) {
    int i = (int)(long)arg;                  /* identificador 0..m-1      */
    int prox = (i + 1) % m;
    pthread_barrier_wait(&largada);
    for (int v = 0; v < R; v++) {
        sem_wait_chk(&loadingArea[i]);       /* minha vez de embarcar     */
        load(i);
        carro_embarcando = i;                /* lido pelos passageiros    */
        sem_signal_n(&boardQueue, C);
        sem_wait_chk(&allAboard);
        parte(i);
        sem_signal(&loadingArea[prox]);      /* passa a vez de embarque   */
        run(i);

        sem_wait_chk(&unloadingArea[i]);     /* minha vez de desembarcar  */
        unload(i);
        sem_signal_n(&unboardQueue[i], C);
        sem_wait_chk(&allAshore);
        esvaziou(i);
        sem_signal(&unloadingArea[prox]);    /* passa a vez de desembarque*/
    }
    return NULL;
}

static void *passageiro(void *arg) {
    int p = (int)(long)arg;
    pthread_barrier_wait(&largada);
    for (;;) {
        double t_chegada = now_s();          /* passageiro quer embarcar  */
        sem_wait_chk(&boardQueue);
        if (done) break;
        int car = carro_embarcando;          /* estavel ate allAboard     */
        board(p, car, t_chegada);
        sem_wait_chk(&mutex);
        if (++boarders == C) { sem_signal(&allAboard); boarders = 0; }
        sem_signal(&mutex);

        sem_wait_chk(&unboardQueue[car]);    /* so o MEU carro me libera  */
        unboard(p, car);
        sem_wait_chk(&mutex2);
        if (++unboarders == C) { sem_signal(&allAshore); unboarders = 0; }
        sem_signal(&mutex2);
    }
    return NULL;
}

static void uso(const char *prog) {
    fprintf(stderr,
        "uso: %s [-m carros] [-C capacidade] [-n passageiros] [-R voltas]\n"
        "          [-L tL_ms] [-T tR_ms] [-U tU_ms] [-s cenario] [-r rep] [-v] [-H]\n"
        "  -v  imprime o rastro de eventos em stderr\n"
        "  -H  imprime apenas o cabecalho CSV\n", prog);
    exit(1);
}

static const char *CABECALHO =
    "cenario,m,C,n,R,tL,tR,tU,rep,T_s,vazao,lat_media_ms,lat_p95_ms,lat_max_ms,"
    "ms_por_volta,user_s,sys_s,cpu_pct,trocas_vol,trocas_invol,max_na_pista,violacoes";

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "m:C:n:R:L:T:U:s:r:vH")) != -1) {
        switch (opt) {
        case 'm': m = atoi(optarg); break;
        case 'C': C = atoi(optarg); break;
        case 'n': n = atoi(optarg); break;
        case 'R': R = atoi(optarg); break;
        case 'L': tL = atof(optarg); break;
        case 'T': tR = atof(optarg); break;
        case 'U': tU = atof(optarg); break;
        case 's': cenario = optarg; break;
        case 'r': rep = atoi(optarg); break;
        case 'v': trace = 1; break;
        case 'H': puts(CABECALHO); return 0;
        default: uso(argv[0]);
        }
    }
    if (m < 1 || C < 1 || R < 1 || n < C) uso(argv[0]);
    if (n < m * C)
        fprintf(stderr, "aviso: n < m*C, alguns carros nunca estarao cheios simultaneamente\n");

    /* alocacao */
    unboardQueue = calloc(m, sizeof(sem_t));
    loadingArea = calloc(m, sizeof(sem_t));
    unloadingArea = calloc(m, sizeof(sem_t));
    estado = calloc(m, sizeof(int)); bcount = calloc(m, sizeof(int));
    ucount = calloc(m, sizeof(int)); viagem_do_carro = calloc(m, sizeof(int));
    viagem_do_pass = calloc(n, sizeof(int));
    cap_lat = (long)m * R * C;
    latencias = malloc(sizeof(double) * cap_lat);

    sem_init_chk(&mutex, 1); sem_init_chk(&mutex2, 1);
    sem_init_chk(&boardQueue, 0);
    sem_init_chk(&allAboard, 0); sem_init_chk(&allAshore, 0);
    for (int i = 0; i < m; i++) {
        sem_init_chk(&unboardQueue[i], 0);
        sem_init_chk(&loadingArea[i], i == 0);   /* carro 0 comeca         */
        sem_init_chk(&unloadingArea[i], i == 0);
    }
    CHECK(pthread_barrier_init(&largada, NULL, (unsigned)(n + m + 1)));

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    pthread_t *cars = malloc(sizeof(pthread_t) * m), *pas = malloc(sizeof(pthread_t) * n);
    for (long p = 0; p < n; p++) CHECK(pthread_create(&pas[p], &attr, passageiro, (void *)p));
    for (long i = 0; i < m; i++) CHECK(pthread_create(&cars[i], &attr, carro, (void *)i));

    usage_t u0 = get_usage();
    t0 = now_s();
    pthread_barrier_wait(&largada);              /* largada                */
    for (int i = 0; i < m; i++) CHECK(pthread_join(cars[i], NULL));
    double T = now_s() - t0;
    usage_t u1 = get_usage();

    done = 1;
    sem_signal_n(&boardQueue, n);
    for (int p = 0; p < n; p++) CHECK(pthread_join(pas[p], NULL));

    if (prox_viagem != m * R || prox_desembarque != m * R)
        violacao("numero de viagens incorreto", prox_viagem, prox_desembarque);

    qsort(latencias, nlat, sizeof(double), cmp_double);
    double soma = 0;
    for (long k = 0; k < nlat; k++) soma += latencias[k];
    double cpu = (u1.user_s - u0.user_s) + (u1.sys_s - u0.sys_s);

    printf("%s,%d,%d,%d,%d,%g,%g,%g,%d,%.6f,%.3f,%.4f,%.4f,%.4f,%.5f,%.4f,%.4f,%.2f,%ld,%ld,%d,%ld\n",
           cenario, m, C, n, R, tL, tR, tU, rep, T,
           (double)m * R * C / T,
           nlat ? soma / nlat : 0, percentile(latencias, (int)nlat, 95),
           nlat ? latencias[nlat - 1] : 0,
           T * 1e3 / ((double)m * R),
           u1.user_s - u0.user_s, u1.sys_s - u0.sys_s, 100.0 * cpu / T,
           u1.vol_cs - u0.vol_cs, u1.invol_cs - u0.invol_cs,
           max_na_pista, violacoes);
    return violacoes ? 2 : 0;
}
