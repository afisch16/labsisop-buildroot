#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/sched.h>
/* Define LOW_IDLE para compilar com cabeçalhos que ainda não a conhecem. */
#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

volatile int running = 1;

void *run(void *data)
{
	while (running);

	return 0;
}

void print_sched(int policy)
{
	int priority_min, priority_max;

	switch(policy){
		case SCHED_DEADLINE:
			printf("SCHED_DEADLINE");
			break;
		case SCHED_FIFO:
			printf("SCHED_FIFO");
			break;
		case SCHED_RR:
			printf("SCHED_RR");
			break;
		case SCHED_NORMAL:
			printf("SCHED_OTHER");
			break;
		case SCHED_BATCH:
			printf("SCHED_BATCH");
			break;
		case SCHED_IDLE:
			printf("SCHED_IDLE");
			break;
		/* Mostra o nome da nova política ao consultar a thread. */
		case SCHED_LOW_IDLE:
			printf("SCHED_LOW_IDLE");
			break;
		default:
			printf("unknown\n");
	}
	priority_min = sched_get_priority_min(policy);
	priority_max = sched_get_priority_max(policy);
	printf(" PRI_MIN: %d PRI_MAX: %d\n", priority_min, priority_max);
}

int setpriority(pthread_t *thr, int newpolicy, int newpriority)
{
	int policy, ret;
	struct sched_param param;

	if (newpriority > sched_get_priority_max(newpolicy) || newpriority < sched_get_priority_min(newpolicy)){
		printf("Invalid priority: MIN: %d, MAX: %d", sched_get_priority_min(newpolicy), sched_get_priority_max(newpolicy));

		return -1;
	}

	pthread_getschedparam(*thr, &policy, &param);
	printf("current: ");
	print_sched(policy);

	param.sched_priority = newpriority;
	ret = pthread_setschedparam(*thr, newpolicy, &param);
	if (ret != 0)
		perror("perror(): ");

	pthread_getschedparam(*thr, &policy, &param);
	printf("new: ");
	print_sched(policy);

	return 0;
}

int main(int argc, char **argv)
{
	int timesleep;
	int policy;
	pthread_t thr;

	/* Recebe a política e o tempo de execução. */
	if (argc != 3) {
		printf("usage: ./%s <idle|low_idle> <execution_time>\n\n", argv[0]);
		return 1;
	}

	/* Converte o nome informado na política correspondente. */
	if (strcmp(argv[1], "idle") == 0)
		policy = SCHED_IDLE;
	else if (strcmp(argv[1], "low_idle") == 0)
		policy = SCHED_LOW_IDLE;
	else {
		printf("policy must be idle or low_idle\n");
		return 1;
	}

	timesleep = atoi(argv[2]);
	pthread_create(&thr, NULL, run, NULL);

	/* As duas políticas usam prioridade 0. */
	setpriority(&thr, policy, 0);

	sleep(timesleep);
	running = 0;
	pthread_join(thr, NULL);

	return 0;
}