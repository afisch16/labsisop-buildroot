#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 400

int main(void)
{
	int fd;
	ssize_t written, received;
	char command[BUFFER_SIZE];
	char response[BUFFER_SIZE];

	printf("Digite o comando XTEA: ");
	if (fgets(command, sizeof(command), stdin) == NULL)
		return 1;

	command[strcspn(command, "\n")] = '\0';

	fd = open("/dev/xtea_driver", O_RDWR);
	if (fd < 0) {
		perror("Não foi possível abrir /dev/xtea_driver");
		return 1;
	}

	written = write(fd, command, strlen(command));
	if (written < 0) {
		perror("Falha ao escrever no driver");
		close(fd);
		return 1;
	}

	received = read(fd, response, sizeof(response) - 1);
	if (received < 0) {
		perror("Falha ao ler do driver");
		close(fd);
		return 1;
	}

	response[received] = '\0';
	printf("Resultado: %s\n", response);

	close(fd);
	return 0;
}
