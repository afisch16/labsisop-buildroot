#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * política LOW_IDLE foi criada no kernel do laboratório
 * se o compilador ainda não conhece o nome, usamos o número 7
 */
#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

/*
 * todas as threads vão escrever no mesmo buffer
 * a posição atual indica onde a próxima letra será colocada
 */
char *buffer;
char *posicao_atual;
size_t tamanho_buffer;

/*
 * o semáforo garante que só uma thread por vez escreva
 * no buffer e atualize a posição
 */
sem_t semaforo; // evitar perder uma letra, se 2 threads ler a posicap_atual ao mesmo tempo


/*
 * cada thread recebe uma letra e escreve no buffer enquanto
 * ainda houver espaço
 */
void *executar_thread(void *arg)
{
	/* le a letra atribuída a esta thread */
	char letra = *(char *)arg;

	while (1) {//enquanto tiver espaço
		/* espera sua vez para acessar o buffer compartilhado */
		if (sem_wait(&semaforo) != 0) {
			perror("Erro no aguardo pelo semaforo");
			return NULL;
		}

		/* se o buffer estiver cheio libera o semforo e termina */
		if (posicao_atual >= buffer + tamanho_buffer) {
			sem_post(&semaforo);
			break;
		}

		/* escreve a letra e avança para a próxima posição */
		*posicao_atual = letra;
		posicao_atual++;

		/* libera o acesso para as outras threads */
		sem_post(&semaforo);
	}

	return NULL;
}



/*
 * converte o nome recebido no terminal para a política
 * que será aplicada às threads
 */
int obter_politica(const char *nome)
{
	if (strcmp(nome, "SCHED_LOW_IDLE") == 0)
		return SCHED_LOW_IDLE;

	if (strcmp(nome, "SCHED_IDLE") == 0)
		return SCHED_IDLE;

	if (strcmp(nome, "SCHED_FIFO") == 0)
		return SCHED_FIFO;

	if (strcmp(nome, "SCHED_RR") == 0)
		return SCHED_RR;

    /* retorna -1 quando o nome informado não é reconhecido */
	return -1;
}



int main(int argc, char *argv[])
{
	int politica;
	long tamanho_informado;
	long numero_threads; //qtd trabalhadoras
	char *fim; //verifica as entradas
    pthread_t *threads;
	char *letras;
	struct sched_param parametros = {0};
	int prioridade;
	long criadas = 0;
	int erro = 0;

    /* precisa receber tamanho, numero de threads e política */
	if (argc != 4) {
		printf("Uso: %s <tamanho_buffer> <numero_threads> <politica>\n",
		       argv[0]);
		return 1;
	}

	/* lê o tamanho e verifica se é um número inteiro positivo */
	errno = 0;
	tamanho_informado = strtol(argv[1], &fim, 10);

	if (errno != 0 || fim == argv[1] || *fim != '\0' ||
	    tamanho_informado <= 0) {
		printf("O tamanho do buffer deve ser um inteiro positivo.\n");
		return 1;
	}

	/* lê a quantidade de threads e faz a mesma verificação */
	errno = 0;
	numero_threads = strtol(argv[2], &fim, 10);

	if (errno != 0 || fim == argv[2] || *fim != '\0' ||
	    numero_threads <= 0) {
		printf("O número de threads deve ser um inteiro positivo.\n");
		return 1;
	}

	/* usa as letras de A a Z para identificar até 26 threads */
	if (numero_threads > 26) {
		printf("O número de threads deve ser no máximo 26.\n");
		return 1;
	}

	/* verifica se a política informada é válida */
	politica = obter_politica(argv[3]);

	if (politica == -1) {
		printf("Política inválida: %s\n", argv[3]);
		return 1;
	}

    /* reserva o espaço onde as threads vao escrever as letras */
	tamanho_buffer = (size_t)tamanho_informado;
	buffer = malloc(tamanho_buffer); //reserva mem pras letras

	if (buffer == NULL) {
		printf("Não foi possível reservar o buffer.\n");
		return 1;
	}

	/* primeira escrita no começo do buffer */
	posicao_atual = buffer;

	/* começa fechado; é liberado depois de configurar as threads */
	/* 0 indica compartilhamento entre threads do mesmo processo */
	if (sem_init(&semaforo, 0, 0) != 0) {
		perror("Não foi possível iniciar o semaforo");
		free(buffer);
		return 1;
	}

    /* reserva espaço pros identificadores e as letras das threads */
	threads = calloc(numero_threads, sizeof(*threads));
	letras = calloc(numero_threads, sizeof(*letras));

	/* se qualquer coisa falhar, libera e termina */
	if (threads == NULL || letras == NULL) {
		printf("Não foi possível reservar espaço para as threads.\n");
		free(threads);
		free(letras);
		sem_destroy(&semaforo);
		free(buffer);
		return 1;
	}
	/* consulta uma prioridade valida para a politica escolhida */
	prioridade = sched_get_priority_min(politica);

	/* se qualquer coisa falhar, libera e termina */
	if (prioridade == -1) {
		perror("Não foi possível consultar a prioridade");
		free(threads);
		free(letras);
		sem_destroy(&semaforo);
		free(buffer);
		return 1;
	}

	/* idle e low_idle usam prioridade 0; fifo e rr começam em 1 */
	parametros.sched_priority = prioridade;

	/* cria as threads e configura a política antes de liberar a escrita */
	while (criadas < numero_threads) {
		letras[criadas] = (char)('A' + criadas);

		erro = pthread_create(&threads[criadas], NULL, executar_thread,
				      &letras[criadas]);
		if (erro != 0) {
			fprintf(stderr, "Não foi possível criar a thread: %s\n",
				strerror(erro));
			break;
		}

		/* inclui a thread na lista para aguardar mesmo se a configuração falhar */
		criadas++;

		/* aplica a mesma politica a cada thread antes de liberar o sem */
		erro = pthread_setschedparam(threads[criadas - 1], politica,
					     &parametros);
		if (erro != 0) {
			fprintf(stderr, "Não foi possível aplicar a política: %s\n",
				strerror(erro));
			break;
		}
	}


	/* se der erro, faz as threads criadas encontrarem o buffer cheio */
	if (erro != 0)
		posicao_atual = buffer + tamanho_buffer;

	/* libera a primeira thread, as outras recebem a vez pelo semaforo */
	sem_post(&semaforo);

	/* espera todas as threads criadas terminarem */
	for (long i = 0; i < criadas; i++)
		pthread_join(threads[i], NULL);

	if (erro != 0) {
		free(threads);
		free(letras);
		sem_destroy(&semaforo);
		free(buffer);
		return 1;
	}

	/* mostra todas as letras na ordem em que foram escritas */
	/* escreve exatamente o tamanho do buffer */
	fwrite(buffer, 1, tamanho_buffer, stdout);
	putchar('\n');


	/* resume cada sequencia de letras iguais */
	for (size_t i = 0; i < tamanho_buffer; i++) {
		/* imprime apenas a primeira letra de cada sequência */
		if (i == 0 || buffer[i] != buffer[i - 1])
			putchar(buffer[i]);
	}
	putchar('\n');

	/* conta quantos blocos cada thread formou no buffer */
	for (long i = 0; i < numero_threads; i++) {
		size_t blocos = 0;

		for (size_t j = 0; j < tamanho_buffer; j++) {
			/* conta inicios de blocos, não todas as trocas de cpu */
			if (buffer[j] == letras[i] &&
			    (j == 0 || buffer[j - 1] != letras[i]))
				blocos++;
		}

		printf("%c = %zu\n", letras[i], blocos);
	}

	/* libera tudo */
	free(threads);
	free(letras);
	sem_destroy(&semaforo);
	free(buffer);

	return 0;
}