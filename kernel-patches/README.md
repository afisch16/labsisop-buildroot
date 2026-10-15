# Syscalls dos desafios do Tutorial 2.2

O kernel 4.13.9 é mantido separadamente do repositório Buildroot. Este patch contém as alterações dos desafios 1 e 2 sobre o kernel do Tutorial 2.2 (commit base 385fa30a, com listProcessInfo).

Para aplicá-lo na cópia dos fontes usada pelo Buildroot:

```sh
cd linux-4.13.9
git apply ../kernel-patches/tutorial-2.2-desafios.patch
```

Os programas de teste estão em custom-scripts/ e são compilados por board/qemu/x86/post-build.sh.

## Política SCHED_LOW_IDLE do Tutorial 3.1

O kernel é um repositório separado. O patch `kernel-patches/tutorial-3.1-sched-low-idle.patch` contém as quatro alterações do escalonador feitas sobre a branch dos desafios 1 e 2 do Linux 4.13.9. Para aplicá-lo a uma cópia equivalente:

```sh
cd linux-4.13.9
git apply ../kernel-patches/tutorial-3.1-sched-low-idle.patch
```

O programa `custom-scripts/thread_runner.c` é compilado pelo script de pós-build para a imagem do QEMU.
