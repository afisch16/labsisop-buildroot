#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SYSCALL_SLEEP_PROCESSES 386
#define BUFFER_SIZE 4096

int main(void)
{
    char buf[BUFFER_SIZE];
    long ret;

    printf("Invoking 'listSleepProcesses' system call.\n");

    ret = syscall(
        SYSCALL_SLEEP_PROCESSES,
        buf,
        sizeof(buf)
    );

    if (ret < 0) {
        perror("System call 'listSleepProcesses' failed");
        return 1;
    }

    printf("%s", buf);
    return 0;
}
