/*
 * common.h - utilitarios compartilhados pelos tres problemas do TP2.
 * Sistemas Operacionais - CEFET-MG
 * Autores: Pedro Ferreira Lopes e Emanuelle Camile de Paulo Lima
 */
#ifndef TP2_COMMON_H
#define TP2_COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <math.h>

/* Aborta com mensagem se uma chamada da biblioteca pthread falhar. */
#define CHECK(call)                                                          \
    do {                                                                     \
        int _rc = (call);                                                    \
        if (_rc != 0) {                                                      \
            fprintf(stderr, "%s:%d: %s falhou: %s\n", __FILE__, __LINE__,    \
                    #call, strerror(_rc == -1 ? errno : _rc));               \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

/* Wrappers de semaforo POSIX com a nomenclatura de Downey (wait/signal). */
static inline void sem_init_chk(sem_t *s, unsigned v) { CHECK(sem_init(s, 0, v)); }
static inline void sem_signal(sem_t *s) { CHECK(sem_post(s)); }
static inline void sem_signal_n(sem_t *s, int n) {
    for (int i = 0; i < n; i++) CHECK(sem_post(s));
}
static inline void sem_wait_chk(sem_t *s) {
    while (sem_wait(s) == -1) {
        if (errno != EINTR) { perror("sem_wait"); exit(EXIT_FAILURE); }
    }
}

/* Relogio monotonico em segundos. */
static inline double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Dorme ms milissegundos (aceita fracao). ms <= 0 nao dorme nem cede a CPU. */
static inline void sleep_ms(double ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000.0);
    ts.tv_nsec = (long)((ms - (double)ts.tv_sec * 1000.0) * 1e6);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

/* Gerador pseudoaleatorio por thread (xorshift64*), sem estado global. */
static inline double rand01(unsigned long long *state) {
    unsigned long long x = *state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *state = x;
    return (double)((x * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}

static inline int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Percentil p (0..100) de um vetor ja ordenado (interpolacao linear). */
static inline double percentile(const double *v, int n, double p) {
    if (n <= 0) return 0.0;
    double idx = p / 100.0 * (n - 1);
    int lo = (int)idx, hi = lo + 1 < n ? lo + 1 : lo;
    double f = idx - lo;
    return v[lo] * (1 - f) + v[hi] * f;
}

/* Uso de CPU e trocas de contexto do processo (mesmos campos de /usr/bin/time -v). */
typedef struct {
    double user_s, sys_s;
    long vol_cs, invol_cs;
} usage_t;

static inline usage_t get_usage(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    usage_t u;
    u.user_s = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec * 1e-6;
    u.sys_s = (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec * 1e-6;
    u.vol_cs = ru.ru_nvcsw;
    u.invol_cs = ru.ru_nivcsw;
    return u;
}

#endif
