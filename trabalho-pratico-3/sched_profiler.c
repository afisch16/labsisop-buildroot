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

/* guarda a configuracao e os resultados de cada thread */
struct dados_thread {
	char letra;
	int politica;
	int prioridade;
	size_t caracteres;
	unsigned long long escalonamentos;
	/* indica se a contagem foi lida do kernel */
	int estatistica_ok;
};


/*
 * cada thread recebe uma letra e escreve no buffer enquanto
 * ainda houver espaço
 */
void *executar_thread(void *arg)
{
	/* recebe os dados desta thread enviados pela main */
	struct dados_thread *dados = arg;
	FILE *arquivo;
	unsigned long long tempo_cpu;
	unsigned long long tempo_espera;

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
		*posicao_atual = dados->letra;
		posicao_atual++;

		/* conta a letra que acabou de escrever */
		dados->caracteres++;

		/* libera o acesso para as outras threads */
		sem_post(&semaforo);
	}

	/* abre as estatisticas da thread que esta executando */
	arquivo = fopen("/proc/thread-self/schedstat", "r");
	if (arquivo != NULL) {
		/* os dois primeiros numeros sao tempos e o terceiro conta execucoes */
		if (fscanf(arquivo, "%llu %llu %llu",
			   &tempo_cpu, &tempo_espera,
			   &dados->escalonamentos) == 3) {
			/* so aceita o resultado se conseguiu ler os tres numeros */
			dados->estatistica_ok = 1;
		}

		fclose(arquivo);
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

/* devolve o nome da politica usado na saida */
const char *nome_politica(int politica)
{
	switch (politica) {
	case SCHED_LOW_IDLE:
		return "SCHED_LOW_IDLE";
	case SCHED_IDLE:
		return "SCHED_IDLE";
	case SCHED_FIFO:
		return "SCHED_FIFO";
	case SCHED_RR:
		return "SCHED_RR";
	default:
		return "desconhecida";
	}
}

/* pega a politica e a prioridade do argumento */
int ler_configuracao(const char *texto, int *politica, int *prioridade)
{
	const char *separador = strchr(texto, ':');
	char nome[32];
	char *fim;
	size_t tamanho_nome;
	long valor;
	int minimo;
	int maximo;

	/* sem : nao tem como separar os valores */
	if (separador == NULL)
		return -1;

	/* copia so o nome para consultar o numero da politica */
	tamanho_nome = (size_t)(separador - texto);
	if (tamanho_nome == 0 || tamanho_nome >= sizeof(nome))
		return -1;

	memcpy(nome, texto, tamanho_nome);
	nome[tamanho_nome] = '\0';

	/* nao aceita nomes fora das quatro politicas do trabalho */
	*politica = obter_politica(nome);
	if (*politica == -1)
		return -1;

	/* converte o texto depois do : em numero */
	errno = 0;
	valor = strtol(separador + 1, &fim, 10);
	if (errno != 0 || fim == separador + 1 || *fim != '\0')
		return -1;

	/* confere se a prioridade pode ser usada nessa politica */
	minimo = sched_get_priority_min(*politica);
	maximo = sched_get_priority_max(*politica);
	if (minimo == -1 || maximo == -1 ||
	    valor < minimo || valor > maximo)
		return -1;

	/* guarda a prioridade depois da conferencia */
	*prioridade = (int)valor;
	return 0;
}



int main(int argc, char *argv[])
{
	long tamanho_informado;
	long numero_threads; //qtd trabalhadoras
	char *fim; //verifica as entradas
    pthread_t *threads;
	struct dados_thread *dados;
	struct sched_param parametros = {0};
	long criadas = 0;
	int erro = 0;

    /* recebe tamanho quantidade de threads e suas politicas */
	if (argc < 4) {
		printf("Uso: %s <tamanho_buffer> <numero_threads> <politica:prioridade>...\n",
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

	/* confere se foi dada uma configuracao para cada thread */
	if (argc != numero_threads + 3) {
		printf("Informe uma politica e prioridade para cada thread.\n");
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

    /* reserva espaco para os ids e os dados das threads */
	threads = calloc(numero_threads, sizeof(*threads));
	dados = calloc(numero_threads, sizeof(*dados));

	/* se qualquer coisa falhar, libera e termina */
	if (threads == NULL || dados == NULL) {
		printf("Não foi possível reservar espaço para as threads.\n");
		free(threads);
		free(dados);
		sem_destroy(&semaforo);
		free(buffer);
		return 1;
	}
	/* cria as threads e configura a politica antes da escrita */
	while (criadas < numero_threads) {
		/* identifica esta thread pela sua letra */
		dados[criadas].letra = (char)('A' + criadas);

		/* define a politica e a prioridade desta thread */
		if (ler_configuracao(argv[3 + criadas],
				     &dados[criadas].politica,
				     &dados[criadas].prioridade) != 0) {
			fprintf(stderr, "Configuracao invalida: %s\n",
				argv[3 + criadas]);
			erro = EINVAL;
			break;
		}

		erro = pthread_create(&threads[criadas], NULL, executar_thread,
				      &dados[criadas]);
		if (erro != 0) {
			fprintf(stderr, "Não foi possível criar a thread: %s\n",
				strerror(erro));
			break;
		}

		/* inclui a thread na lista para aguardar mesmo se a configuração falhar */
		criadas++;

		/* aplica a politica e a prioridade desta thread antes da escrita */
		parametros.sched_priority = dados[criadas - 1].prioridade;
		erro = pthread_setschedparam(threads[criadas - 1],
					     dados[criadas - 1].politica,
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
		free(dados);
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
			/* conta inicios de blocos, nao todas as trocas de cpu */
			if (buffer[j] == dados[i].letra &&
			    (j == 0 || buffer[j - 1] != dados[i].letra))
				blocos++;
		}

		/* mostra a configuracao e os resultados desta thread */
		if (dados[i].estatistica_ok) {
			printf("%c: politica=%s prioridade=%d blocos=%zu caracteres=%zu escalonamentos=%llu\n",
			       dados[i].letra, nome_politica(dados[i].politica),
			       dados[i].prioridade, blocos, dados[i].caracteres,
			       dados[i].escalonamentos);
		} else {
			printf("%c: politica=%s prioridade=%d blocos=%zu caracteres=%zu escalonamentos=indisponivel\n",
			       dados[i].letra, nome_politica(dados[i].politica),
			       dados[i].prioridade, blocos, dados[i].caracteres);
		}
	}

	/* libera tudo */
	free(threads);
	free(dados);
	sem_destroy(&semaforo);
	free(buffer);

	return 0;
}