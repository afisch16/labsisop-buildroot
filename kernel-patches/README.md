# Syscalls dos desafios do Tutorial 2.2

O kernel 4.13.9 é mantido separadamente do repositório Buildroot. Este patch contém as alterações dos desafios 1 e 2 sobre o kernel do Tutorial 2.2 (commit base 385fa30a, com listProcessInfo).

Para aplicá-lo na cópia dos fontes usada pelo Buildroot:

```sh
cd linux-4.13.9
git apply ../kernel-patches/tutorial-2.2-desafios.patch
```

Os programas de teste estão em custom-scripts/ e são compilados por board/qemu/x86/post-build.sh.
