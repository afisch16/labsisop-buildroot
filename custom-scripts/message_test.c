#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SYSCALL_PRINTMESSAGE 387

int main(int argc, char **argv)
{
        long ret;

        if (argc < 2) {
                printf("Usage: %s <message>\n", argv[0]);
                return 1;
        }

        printf("Invoking 'printMessage' system call.\n");

        ret = syscall(SYSCALL_PRINTMESSAGE, argv[1]);

        printf("System call returned %ld.\n", ret);

        return 0;
}
