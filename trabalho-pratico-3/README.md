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

Exemplo de execução na target:

```sh
/bin/sched_profiler 5000000 4 SCHED_RR
```

As quatro threads trabalhadoras recebem a política escolhida naquela execução. Se o programa não conseguir aplicar a política, informa o erro e encerra.

## Como os testes foram feitos

O arquivo `custom-scripts/sched_profiler.c` foi compilado para a target e executado no QEMU. Para cada política, foram usadas quatro threads e buffers de 1 milhão e de 5 milhões de posições com uma CPU. O teste de 5 milhões foi repetido com duas CPUs.

Nos testes de escalonamento, o evento `sched:sched_switch` foi gravado com `trace-cmd` e o arquivo resultante foi aberto no KernelShark. Abaixo está um exemplo da captura de `SCHED_IDLE` com duas CPUs; nas demais execuções, mudaram apenas a política e o nome do arquivo:

```sh
trace-cmd record -C global -o /root/idle_2cpu_5m_global.dat \
  -e sched:sched_switch \
  /bin/sched_profiler 5000000 4 SCHED_IDLE \
  > /tmp/idle_2cpu_5m_global.out
```

Na rodada de duas CPUs, a opção `-C global` permitiu consultar os eventos das duas CPUs em uma mesma linha de tempo. Foi aplicada uma política por execução.

## O que a saída do programa significa

Esta é a parte final de uma execução com `SCHED_RR`, quatro threads, uma CPU e buffer de 5 milhões de posições:

```text
ABCDABCDABCDABCDA
A = 5
B = 4
C = 4
D = 4
```

Antes dessas linhas, o programa imprimiu o buffer completo. A linha foi omitida do exemplo por conter 5 milhões de caracteres. A impressão foi conferida com `head -n 1 /tmp/idle_5m.out | wc -c`, que retornou `5000001`: as 5 milhões de letras mais a quebra de linha.

`ABCDABCDABCDABCDA` é o buffer após o pós-processamento: cada sequência contínua de letras iguais aparece uma vez. Portanto, `A = 5` quer dizer que A formou cinco blocos separados no buffer.

Blocos não são a mesma coisa que entradas na CPU. Uma thread pode escrever `AAA`, sair da CPU para outra tarefa do sistema, voltar e escrever `AA`. Se nenhuma outra thread escreveu nesse intervalo, o buffer contém `AAAAA`: um único bloco de A, embora a thread tenha entrado na CPU duas vezes. O buffer mostra a ordem das escritas; o trace mostra as trocas de tarefas feitas pelo kernel.

## Resultados com uma CPU

A tabela reúne as quantidades de blocos observadas no buffer. Houve uma execução por política em cada tamanho.

| Política | 1 milhão: A / B / C / D | 5 milhões: A / B / C / D |
|---|---:|---:|
| `SCHED_IDLE` | 20 / 20 / 20 / 18 | 89 / 91 / 89 / 90 |
| `SCHED_LOW_IDLE` | 20 / 19 / 17 / 19 | 89 / 90 / 92 / 87 |
| `SCHED_FIFO` | 1 / 0 / 0 / 0 | 1 / 0 / 0 / 0 |
| `SCHED_RR` | 2 / 2 / 2 / 1 | 5 / 4 / 4 / 4 |

Em `SCHED_IDLE` e `SCHED_LOW_IDLE`, as quatro letras apareceram e formaram quantidades próximas de blocos. Com 5 milhões de posições, foram 359 blocos em IDLE e 358 em LOW_IDLE. Esse resultado mostra alternância entre threads da mesma política em cada execução.

Em `SCHED_FIFO`, A preencheu o buffer inteiro nas duas rodadas. As outras trabalhadoras foram criadas, mas não escreveram antes de o buffer ficar cheio. Em `SCHED_RR`, as quatro letras apareceram; o buffer maior permitiu observar mais blocos: sete no teste de 1 milhão e 17 no de 5 milhões.

### O que apareceu no trace

A contagem de vezes que cada trabalhadora apareceu à direita de `==>` em `sched_switch` indica quantas entradas na CPU foram registradas. Na host, foi usado este comando para cada arquivo `.dat`, trocando apenas seu nome:

```bash
trace-cmd report -i idle_5m.dat |
  grep -oE '==> sched_profiler:[0-9]+' |
  sort | uniq -c
```

Cada resultado também incluiu a thread principal do programa. Na tabela abaixo constam apenas as quatro trabalhadoras, em ordem de TID.

| Política | TIDs das trabalhadoras | Entradas na CPU, por TID |
|---|---|---:|
| `SCHED_IDLE` | 86–89 | 190 / 194 / 194 / 196 |
| `SCHED_LOW_IDLE` | 96–99 | 207 / 204 / 214 / 210 |
| `SCHED_FIFO` | 104–107 | 7 / 2 / 2 / 2 |
| `SCHED_RR` | 112–115 | 14 / 14 / 13 / 13 |

As entradas incluem a criação e o encerramento das threads, não apenas as escritas. Isso explica por que as três trabalhadoras que formaram zero blocos em FIFO ainda aparecem duas vezes cada no trace.

Em IDLE, por exemplo, a primeira trabalhadora entrou na CPU 190 vezes, enquanto a letra A formou 89 blocos. Uma saída e uma volta à CPU não criam necessariamente um bloco novo: outra letra precisa ser escrita no intervalo para separar os blocos de A.

As capturas também permitem examinar períodos de execução. Em FIFO, foi observado um trecho do TID 104 entre `405,174773 s` e `406,127676 s`, aproximadamente 0,953 s. Nessa rodada surgiu a mensagem `sched: RT throttling activated`, indicando que o limite de tempo de CPU para tarefas de tempo real foi atingido. Em RR, dois trechos delimitados por trocas entre trabalhadoras duraram aproximadamente 0,098 s e 0,100 s, coerentes com a alternância observada no buffer.

## Resultados com duas CPUs

As quatro políticas foram testadas novamente com buffer de 5 milhões de posições e quatro threads. O código do programa continuou o mesmo; mudou apenas a quantidade de CPUs disponibilizadas ao QEMU.

| Política | Uma CPU: A / B / C / D | Duas CPUs: A / B / C / D |
|---|---:|---:|
| `SCHED_IDLE` | 89 / 91 / 89 / 90 | 524210 / 531601 / 540204 / 1268714 |
| `SCHED_LOW_IDLE` | 89 / 90 / 92 / 87 | 1817339 / 652741 / 648221 / 653905 |
| `SCHED_FIFO` | 1 / 0 / 0 / 0 | 1070198 / 1048545 / 1049671 / 1167811 |
| `SCHED_RR` | 5 / 4 / 4 / 4 | 1200978 / 1164220 / 1054412 / 1113778 |

Com duas CPUs, as quatro letras apareceram em todas as políticas. Também houve muito mais mudanças de letra no buffer. O semáforo continuou protegendo cada escrita, mas as threads podiam executar em CPUs diferentes e disputar o acesso ao buffer compartilhado. Assim, uma escrita de A podia ser seguida rapidamente por uma de B, criando dois blocos mesmo sem uma troca de tarefa entre A e B na mesma CPU.

Para verificar a diferença entre essas mudanças de letra e as trocas do kernel, foram contadas as entradas na CPU nos quatro traces de duas CPUs:

| Política | TIDs das trabalhadoras | Entradas na CPU, por TID |
|---|---|---:|
| `SCHED_IDLE` | 105–108 | 15488 / 15658 / 15308 / 2558 |
| `SCHED_LOW_IDLE` | 115–118 | 5346 / 10824 / 10939 / 10607 |
| `SCHED_FIFO` | 124–127 | 3859 / 3985 / 3878 / 4130 |
| `SCHED_RR` | 133–136 | 1306 / 1371 / 1241 / 1350 |

O contraste é grande em RR: o buffer apresentou 4.533.388 blocos, mas o trace registrou 5.268 entradas das quatro trabalhadoras na CPU. Os blocos contam mudanças de letra a cada escrita; `sched_switch` registra as trocas de tarefa em cada CPU.

Nas capturas do KernelShark, a linha CPU 0 aparece acima da CPU 1. A coluna `CPU` da tabela identifica onde cada evento ocorreu, e os TIDs permitem acompanhar as trabalhadoras. As cores ajudam a localizar períodos de execução, mas não identificam diretamente as letras A, B, C e D.

## Medição complementar do tempo de execução

Para entender melhor o efeito da quantidade de CPUs, foi feita uma medição complementar, separada das capturas pedidas. O mesmo executável foi rodado três vezes por política com uma CPU e três vezes com duas CPUs. Todas as execuções usaram quatro threads e buffer de 5 milhões de posições. O comando `/usr/bin/time` mediu o tempo decorrido em segundos; a saída extensa do programa foi redirecionada para um arquivo na target. Não houve gravação com `trace-cmd` nessa rodada.

Em cada configuração do QEMU (`-smp cpus=1` e `-smp cpus=2`), foi executado o mesmo comando:

```sh
for p in SCHED_IDLE SCHED_LOW_IDLE SCHED_FIFO SCHED_RR; do
    for n in 1 2 3; do
        /usr/bin/time -f "$p run$n %e s" \
            /bin/sched_profiler 5000000 4 "$p" \
            > /tmp/sched_profiler_speed.out
    done
done
```

| Política | 1 CPU: três execuções (s) | Média (s) | 2 CPUs: três execuções (s) | Média (s) |
|---|---:|---:|---:|---:|
| `SCHED_IDLE` | 2,52 / 2,55 / 2,81 | 2,63 | 6,32 / 6,22 / 6,11 | 6,22 |
| `SCHED_LOW_IDLE` | 2,54 / 2,56 / 2,66 | 2,59 | 6,13 / 6,46 / 6,45 | 6,35 |
| `SCHED_FIFO` | 5,95 / 6,71 / 6,05 | 6,24 | 6,68 / 6,63 / 6,54 | 6,62 |
| `SCHED_RR` | 2,40 / 3,87 / 2,20 | 2,82 | 6,54 / 6,92 / 6,66 | 6,71 |

Nas execuções medidas, as médias com uma CPU foram menores em todas as políticas. Em RR com uma CPU, os tempos variaram de 2,20 a 3,87 s, o que mostra por que a média deve ser lida junto dos valores individuais. A diferença foi maior em IDLE, LOW_IDLE e RR. Em FIFO, apareceu a mensagem `sched: RT throttling activated`, que também faz parte das condições em que seu tempo foi medido. A disputa pelo semáforo compartilhado e o custo de emular duas CPUs podem ter contribuído para os tempos com duas CPUs, mas esses fatores não foram medidos separadamente. Os valores descrevem esta rodada de testes, sem estabelecer uma regra geral de velocidade para cada política.

## Conclusões

SCHED_IDLE. Com uma CPU, as quatro trabalhadoras se alternaram e produziram quantidades próximas de blocos. Com duas CPUs, todas continuaram escrevendo, mas D formou mais blocos que as demais nesta execução. Isso indica mais mudanças de letra envolvendo D no buffer, mas não demonstra que D recebeu mais tempo de CPU.

SCHED_LOW_IDLE. Também houve alternância entre as quatro trabalhadoras com uma CPU. Com duas CPUs, A formou mais blocos nesta execução e as quatro letras apareceram. Os testes mostram o comportamento de threads que compartilham essa política.

SCHED_FIFO. Foi a mudança mais clara entre uma e duas CPUs. Com uma CPU, a primeira trabalhadora escreveu sozinha até preencher o buffer, mesmo após o aumento do buffer de 1 milhão para 5 milhões. Com duas CPUs, as quatro conseguiram escrever. A captura mostra eventos da aplicação em ambas as CPUs. Portanto, o resultado de uma CPU não deve ser entendido como “só A pode executar em FIFO”: ele descreve o que aconteceu naquela configuração, com aquelas threads e aquele buffer.

SCHED_RR. Com uma CPU, todas escreveram, mas o buffer teve poucos blocos relativamente longos: 17 no teste de 5 milhões. No trace, foram observados trechos entre trocas de trabalhadoras próximos de 0,1 s. Com duas CPUs, todas também escreveram e o buffer passou a alternar letras com muita frequência. A contagem de `sched_switch` mostra que cada uma dessas mudanças não corresponde necessariamente a uma nova escolha do escalonador.

O experimento evidencia dois efeitos diferentes. A política mudou o padrão de escrita na configuração com uma CPU: FIFO concentrou a escrita em A, enquanto as outras políticas permitiram que as quatro letras aparecessem. A quantidade de CPUs também mudou o padrão: com duas CPUs, as quatro letras apareceram até em FIFO, e o buffer compartilhado passou a alternar letras muito mais vezes.

As capturas do KernelShark mostram as trocas de tarefas, enquanto o buffer mostra a ordem das escritas. A medição separada de tempo mostrou que, nesta rodada, as quatro políticas terminaram mais rápido com uma CPU. As contagens de blocos e de entradas na CPU explicam o padrão de execução observado, mas não identificam sozinhas a causa da diferença de tempo.

## Capturas do KernelShark

| Política | Uma CPU, 1 milhão | Uma CPU, 5 milhões | Duas CPUs, 5 milhões |
|---|---|---|---|
| `SCHED_IDLE` | [idle.png](imagens/idle.png) | [idle_5m.png](imagens/idle_5m.png) | [idle_2cpu_5m.png](imagens/idle_2cpu_5m.png) |
| `SCHED_LOW_IDLE` | [low_idle.png](imagens/low_idle.png) | [low_idle_5m.png](imagens/low_idle_5m.png) | [low_idle_2cpu_5m.png](imagens/low_idle_2cpu_5m.png) |
| `SCHED_FIFO` | [fifo.png](imagens/fifo.png) | [fifo_5m.png](imagens/fifo_5m.png) | [fifo_2cpu_5m.png](imagens/fifo_2cpu_5m.png) |
| `SCHED_RR` | [rr.png](imagens/rr.png) | [rr_5m.png](imagens/rr_5m.png) | [rr_2cpu_5m.png](imagens/rr_2cpu_5m.png) |