# Trabalho Prático 3 — sched_profiler

## O programa

O `sched_profiler` cria a quantidade de threads informada pelo usuário e reserva um buffer com o tamanho solicitado. Cada thread recebe uma letra (`A`, `B`, `C`...) e escreve essa letra na próxima posição livre do buffer.

As threads compartilham o buffer e o ponteiro para a próxima posição. Um semáforo binário protege a escrita e o avanço do ponteiro, permitindo que apenas uma thread faça essas operações por vez.

Ao terminar, o programa imprime o buffer completo, uma versão resumida que junta letras consecutivas iguais e a quantidade de blocos escritos por cada letra.

## Parâmetros

```text
sched_profiler <tamanho_buffer> <numero_threads> <politica>
```

- `tamanho_buffer`: número de posições que serão preenchidas
- `numero_threads`: quantidade de threads, de 1 a 26, identificadas pelas letras `A` a `Z`
- `politica`: `SCHED_LOW_IDLE`, `SCHED_IDLE`, `SCHED_FIFO` ou `SCHED_RR`

Exemplo na target:

```bash
/bin/sched_profiler 1000000 4 SCHED_RR
```

O programa consulta uma prioridade válida para a política escolhida e aplica a mesma política às threads criadas. Se não conseguir aplicá-la, informa o erro e encerra a execução.

## Compilação e inicialização do QEMU

Na host, a partir de `/workspaces/labsisop-buildroot`, compilamos o programa para a target e atualizamos a imagem:

```bash
output/host/bin/i686-buildroot-linux-gnu-gcc -Wall -Wextra -pthread \
  -o output/target/bin/sched_profiler \
  custom-scripts/sched_profiler.c

make
```

Iniciamos o QEMU com uma CPU, como pedido para os testes na target:

```bash
qemu-system-i386 -M pc \
  -kernel output/images/bzImage \
  -drive file=output/images/rootfs.ext2,if=virtio,format=raw \
  -append "rootwait root=/dev/vda console=ttyS0" \
  -nographic \
  -smp cpus=1 \
  -net nic,model=virtio \
  -net user
```

Depois de iniciar, fazemos login como `root`. Os comandos usados para executar cada política e gravar os eventos estão na seção “Captura dos eventos”.

## Saída de exemplo

Esta é a parte final da saída de uma execução com `SCHED_RR`, quatro threads e buffer de 5.000.000 de posições:

```text
ABCDABCDABCDABCDA
A = 5
B = 4
C = 4
D = 4
```

Antes dessas linhas, o programa imprime o buffer completo. Essa primeira linha foi omitida aqui por ter 5 milhões de caracteres. Confirmamos sua impressão no teste com `SCHED_IDLE`: a primeira linha tinha 5.000.001 bytes, incluindo a quebra de linha.

Na target, fizemos essa conferência com:

```sh
head -n 1 /tmp/idle_5m.out | wc -c
```

O comando retornou `5000001`: 5.000.000 de letras e uma quebra de linha.

`ABCDABCDABCDABCDA` é o resultado do pós-processamento: cada sequência de letras iguais no buffer aparece apenas uma vez. Assim, `A = 5` significa que a letra A formou cinco blocos separados no buffer.

**Blocos no buffer e entradas na CPU são medidas diferentes.** Por exemplo, uma thread pode escrever `AAA`, sair da CPU para o `trace-cmd`, voltar e escrever `AA`. O buffer terá `AAAAA`, um único bloco de A, embora a thread tenha entrado na CPU duas vezes. Na execução de `SCHED_IDLE` com 5 milhões de posições, A formou 89 blocos; o trace registrou 190 entradas da primeira thread trabalhadora na CPU. Por isso, usamos o buffer para analisar a ordem das escritas e `sched_switch` para analisar as trocas reais de tarefa.

## Captura dos eventos

Na target, executamos uma vez cada política com **uma CPU, quatro threads e buffer de 5.000.000 de posições**. O `trace-cmd` gravou os eventos `sched:sched_switch` enquanto o programa executava. Redirecionamos o buffer impresso pelo programa para arquivos em `/tmp`, pois ele é grande:

```sh
trace-cmd record -o /root/idle_5m.dat -e sched:sched_switch /bin/sched_profiler 5000000 4 SCHED_IDLE > /tmp/idle_5m.out

trace-cmd record -o /root/low_idle_5m.dat -e sched:sched_switch /bin/sched_profiler 5000000 4 SCHED_LOW_IDLE > /tmp/low_idle_5m.out

trace-cmd record -o /root/fifo_5m.dat -e sched:sched_switch /bin/sched_profiler 5000000 4 SCHED_FIFO > /tmp/fifo_5m.out

trace-cmd record -o /root/rr_5m.dat -e sched:sched_switch /bin/sched_profiler 5000000 4 SCHED_RR > /tmp/rr_5m.out
```

Ainda na target, consultamos as últimas linhas de cada saída para registrar o resumo. Isso deve ser feito antes de desligar o QEMU, pois `/tmp` é temporário:

```sh
tail -n 5 /tmp/idle_5m.out
tail -n 5 /tmp/low_idle_5m.out
tail -n 5 /tmp/fifo_5m.out
tail -n 5 /tmp/rr_5m.out
```

Depois de executar `poweroff` na target e voltar à host, copiamos os traces da imagem do QEMU com o `e2cp`, fornecido pelo pacote `e2tools`:

```bash
e2cp output/images/rootfs.ext2:/root/idle_5m.dat idle_5m.dat
e2cp output/images/rootfs.ext2:/root/low_idle_5m.dat low_idle_5m.dat
e2cp output/images/rootfs.ext2:/root/fifo_5m.dat fifo_5m.dat
e2cp output/images/rootfs.ext2:/root/rr_5m.dat rr_5m.dat
```

Para abrir o KernelShark no Codespace, usamos o comando disponibilizado no Moodle:

```bash
curl -fsSL https://gist.githubusercontent.com/miguelxvr/3c123ef67611652af9d2e45eccb8fc0a/raw/ | bash -s -- start
```

No VS Code, encaminhamos a porta `6080`, abrimos `/vnc.html` no navegador e, no KernelShark, usamos **File → Open** para abrir cada `.dat`. As capturas da rodada inicial de 1 milhão e da rodada de 5 milhões estão na pasta `imagens`.

## O que observamos

Fizemos uma execução por política em cada tamanho de buffer, sempre com quatro threads e uma CPU no QEMU. Em cada execução, as quatro threads trabalhadoras receberam a mesma política. Os resultados podem variar em outra execução.

### Padrão de escrita no buffer

Os valores de A, B, C e D abaixo são **quantidades de blocos consecutivos de letras**, calculadas pelo programa após preencher o buffer.

| Política | 1 milhão: A / B / C / D | 5 milhões: A / B / C / D |
|---|---:|---:|
| `SCHED_IDLE` | 20 / 20 / 20 / 18 | 89 / 91 / 89 / 90 |
| `SCHED_LOW_IDLE` | 20 / 19 / 17 / 19 | 89 / 90 / 92 / 87 |
| `SCHED_FIFO` | 1 / 0 / 0 / 0 | 1 / 0 / 0 / 0 |
| `SCHED_RR` | 2 / 2 / 2 / 1 | 5 / 4 / 4 / 4 |

Com 5 milhões de posições, `SCHED_IDLE` produziu 359 blocos no total e `SCHED_LOW_IDLE`, 358. Nas duas políticas, as quatro letras apareceram e cada thread formou uma quantidade próxima de blocos. Isso mostra alternância **entre threads da mesma política**. Não demonstra que `SCHED_IDLE` e `SCHED_LOW_IDLE` tenham a mesma prioridade: elas não disputaram a CPU entre si nesses testes.

Em `SCHED_FIFO`, a primeira thread preencheu o buffer inteiro nas duas rodadas. As outras foram criadas, mas não escreveram antes de o buffer ficar cheio. Já em `SCHED_RR`, todas escreveram; o resumo passou de sete blocos no teste de 1 milhão para 17 no teste de 5 milhões. O buffer maior tornou mais períodos de escrita visíveis, sem mudar o padrão de FIFO observado nessas execuções.

Os números de 1 milhão vêm das saídas anotadas na rodada inicial. As imagens desse tamanho mostram a mesma configuração, mas não devem ser apresentadas como se fossem necessariamente da execução exata que produziu cada número da tabela. Na rodada de 5 milhões, as saídas e os traces foram obtidos juntos por meio dos comandos da seção anterior.

### Trocas de tarefa registradas pelo kernel

Também contamos quantas vezes cada tarefa `sched_profiler` apareceu **à direita de `==>`** em um evento `sched_switch`: essa é uma entrada na CPU. Na host, usamos os comandos abaixo, um para cada trace:

```bash
trace-cmd report -i idle_5m.dat | grep -oE '==> sched_profiler:[0-9]+' | sort | uniq -c
trace-cmd report -i low_idle_5m.dat | grep -oE '==> sched_profiler:[0-9]+' | sort | uniq -c
trace-cmd report -i fifo_5m.dat | grep -oE '==> sched_profiler:[0-9]+' | sort | uniq -c
trace-cmd report -i rr_5m.dat | grep -oE '==> sched_profiler:[0-9]+' | sort | uniq -c
```

Cada resultado trouxe a thread principal e as quatro trabalhadoras. Na tabela abaixo, deixamos de fora a thread principal e mantivemos as contagens das trabalhadoras em ordem de TID.

Para estimar o intervalo de cada execução registrada, mostramos o primeiro e o último evento que mencionavam `sched_profiler` em cada arquivo:

```bash
trace-cmd report -i idle_5m.dat | grep 'sched_profiler:' | sed -n '1p;$p'
trace-cmd report -i low_idle_5m.dat | grep 'sched_profiler:' | sed -n '1p;$p'
trace-cmd report -i fifo_5m.dat | grep 'sched_profiler:' | sed -n '1p;$p'
trace-cmd report -i rr_5m.dat | grep 'sched_profiler:' | sed -n '1p;$p'
```

Subtraímos os dois horários de cada par. Por exemplo, em `SCHED_IDLE`, `272,870086 − 270,553245 = 2,316841 s`, arredondados para `2,317 s` na tabela. É um intervalo aproximado do programa no trace, não uma medição isolada do tempo gasto escrevendo no buffer.

| Política | TIDs das trabalhadoras | Entradas na CPU, por TID | Intervalo observado |
|---|---|---:|---:|
| `SCHED_IDLE` | 86–89 | 190 / 194 / 194 / 196 | 2,317 s |
| `SCHED_LOW_IDLE` | 96–99 | 207 / 204 / 214 / 210 | 2,373 s |
| `SCHED_FIFO` | 104–107 | 7 / 2 / 2 / 2 | 5,543 s |
| `SCHED_RR` | 112–115 | 14 / 14 / 13 / 13 | 2,793 s |

Essa contagem inclui entradas na CPU durante a criação e o encerramento das threads, além do preenchimento do buffer. Ela também explica por que zero blocos não significa zero escalonamentos: em FIFO, as três trabalhadoras que não escreveram ainda apareceram duas vezes cada no trace.

Uma thread pode entrar na CPU, escrever `AAA`, sair para outra tarefa do sistema, voltar e escrever `AA`. Se nenhuma outra thread escreveu no intervalo, o buffer contém `AAAAA`: **um bloco**, embora a thread tenha entrado na CPU **duas vezes**. No teste de 5 milhões com `SCHED_IDLE`, por exemplo, a primeira trabalhadora apareceu 190 vezes entrando na CPU, enquanto a letra A formou 89 blocos. Usamos o buffer para estudar a ordem das escritas e o trace para estudar as trocas de CPU.

### Trechos de tempo nas capturas

Na captura de FIFO, depois de configuradas as threads, o TID 104 entrou na CPU em `405,174773 s` e saiu no evento de `406,127676 s`: um trecho de aproximadamente **0,953 s**. Nesse teste apareceu `sched: RT throttling activated`, aviso de que o limite de tempo de CPU das tarefas de tempo real foi atingido. Esse trecho é próximo do orçamento padrão de 0,95 s por período de 1 s, mas não basta, sozinho, para explicar toda a duração da execução.

Na captura de RR, duas trocas consecutivas entre trabalhadoras delimitam trechos de aproximadamente **0,098 s** e **0,100 s**. São exemplos observados no trace, coerentes com a alternância entre threads de mesma prioridade em `SCHED_RR`. Não calculamos uma média de duração dos trechos.

Para calcular esses trechos, lemos os horários de eventos consecutivos nas tabelas das capturas e subtraímos o início do fim. Em FIFO, `406,127676 − 405,174773 = 0,952903 s`. Em RR, `481,084710 − 480,986738 = 0,097972 s` e `481,184691 − 481,084710 = 0,099981 s`. Os valores no texto foram arredondados; são exemplos de trechos específicos, não médias da execução.

O aviso de *RT throttling* foi impresso durante FIFO. Tanto FIFO quanto RR são políticas de tempo real, e a mensagem do kernel é emitida apenas na primeira ocorrência por inicialização. Portanto, não aparecer de novo durante RR **não comprova** que RR não sofreu essa limitação. Da mesma forma, os intervalos de 2,317 s para IDLE e 2,373 s para LOW_IDLE diferem apenas 0,056 s nesta rodada; uma execução por política não permite afirmar que uma seja mais rápida.

Conferimos isso no código do kernel 4.13.9 usado nos testes. Na host, executamos:

```bash
grep -n -B 4 -A 4 'RT throttling activated' linux-4.13.9/kernel/sched/rt.c
```

A linha 936 mostrou `printk_deferred_once("sched: RT throttling activated\n");`. O `once` explica por que a ausência de uma segunda mensagem, na mesma inicialização do QEMU, não serve para concluir que RR não sofreu limitação.

As cores e a largura dos trechos no KernelShark ajudam a localizar tarefas e intervalos. Não tratamos as cores como medida de percentual de uso de CPU nem chamamos um trecho largo de “pico” sem medir seus tempos. As capturas mostram apenas a CPU 0, conforme a configuração do QEMU.

### Capturas do KernelShark

| Política | Buffer de 1 milhão | Buffer de 5 milhões |
|---|---|---|
| `SCHED_IDLE` | [idle.png](imagens/idle.png) | [idle_5m.png](imagens/idle_5m.png) |
| `SCHED_LOW_IDLE` | [low_idle.png](imagens/low_idle.png) | [low_idle_5m.png](imagens/low_idle_5m.png) |
| `SCHED_FIFO` | [fifo.png](imagens/fifo.png) | [fifo_5m.png](imagens/fifo_5m.png) |
| `SCHED_RR` | [rr.png](imagens/rr.png) | [rr_5m.png](imagens/rr_5m.png) |

Estes testes com uma política por execução mostram como as threads da aplicação se comportaram em cada configuração. Eles não medem diretamente a prioridade relativa entre `SCHED_IDLE` e `SCHED_LOW_IDLE`, porque não colocamos threads dessas duas políticas para disputar a CPU na mesma execução.