#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

static atomic_int running = 1;

static void *run(void *arg)
{
    uint64_t *iterations = arg;

    while (atomic_load_explicit(&running, memory_order_relaxed))
        ++*iterations;

    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t thread;
    struct sched_param param = { .sched_priority = 0 };
    uint64_t iterations = 0;
    char *end;
    long seconds;
    int policy;
    int current_policy;
    int ret;

    if (argc != 3 || (strcmp(argv[1], "idle") && strcmp(argv[1], "low_idle"))) {
        fprintf(stderr, "usage: %s <idle|low_idle> <seconds>\n", argv[0]);
        return 1;
    }

    seconds = strtol(argv[2], &end, 10);
    if (*end || seconds < 1 || seconds > 60) {
        fprintf(stderr, "seconds must be between 1 and 60\n");
        return 1;
    }

    policy = strcmp(argv[1], "idle") == 0 ? SCHED_IDLE : SCHED_LOW_IDLE;
    ret = pthread_create(&thread, NULL, run, &iterations);
    if (ret) {
        fprintf(stderr, "pthread_create: %s\n", strerror(ret));
        return 1;
    }

    ret = pthread_setschedparam(thread, policy, &param);
    if (ret) {
        fprintf(stderr, "pthread_setschedparam(%s): %s\n", argv[1], strerror(ret));
        atomic_store(&running, 0);
        pthread_join(thread, NULL);
        return 1;
    }

    ret = pthread_getschedparam(thread, &current_policy, &param);
    if (ret) {
        fprintf(stderr, "pthread_getschedparam: %s\n", strerror(ret));
        atomic_store(&running, 0);
        pthread_join(thread, NULL);
        return 1;
    }

    printf("thread policy: %s (%d), priority: %d\n",
           argv[1], current_policy, param.sched_priority);
    sleep((unsigned int)seconds);
    atomic_store(&running, 0);
    pthread_join(thread, NULL);
    printf("iterations: %llu\n", (unsigned long long)iterations);
    return current_policy == policy && param.sched_priority == 0 ? 0 : 1;
}
