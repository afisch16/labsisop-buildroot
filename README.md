SystemInfo:
é um servidor HTTP (em python 3). É incluído na imagem 
Linux gerada pelo Buildroot e iniciado automaticamente durante o boot.
O servidor fica na porta 8080 e disponibiliza só o endpoint:

GET /status


A resposta é em JSON e tem a data e hora, o tempo desde o boot,
informações de CPU e memória, a versão do sistema, processos, discos,
dispositivos USB e interfaces de rede. Os valores são lidos novamente a cada
requisição.

Resposta do endpoint
Resposta obtida pelo endpoint /status:
(docs/status-response.png)

Origem das informações
- Data e hora: obtida com "datetime.now()", conforme o módulo já presente
  no código-base fornecido.
- Tempo desde o boot: o primeiro valor de "/proc/uptime" representa o
  número de segundos desde a inicialização.
- Modelo da CPU: lido de "/proc/cpuinfo".
- Frequência da CPU: lida de
  "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq". Quando esse arquivo
  não existe, é usado o campo "cpu MHz" de "/proc/cpuinfo".
- Uso da CPU: calculado pela diferença entre duas leituras da linha
  agregada "cpu" de "/proc/stat".
- Memória:" os valores são lidos de "/proc/meminfo". A memória usada é a
  diferença entre "MemTotal" e "MemAvailable".
- Versão do sistema: conteúdo de "/proc/version".
- Processos: cada diretório numérico de "/proc" representa um PID. O nome
  é lido do arquivo "/proc/<pid>/comm".
- Discos: os dispositivos são enumerados em "/sys/block". O arquivo
  "size" informa a quantidade de setores de 512 bytes.
- USB: os dispositivos são enumerados em "/sys/bus/usb/devices", usando
  os arquivos "manufacturer" e "product" para formar a descrição.
- Rede: as interfaces vêm de "/sys/class/net". Os endereços IPv4 locais
  são lidos de "/proc/net/fib_trie" e associados às interfaces com as rotas
  de "/proc/net/route".

Integração com o Buildroot

O diretório "systeminfo-overlay" é configurado como overlay do sistema de
arquivos. Ele instala "systeminfo.py" em "/usr/bin" e o script
"S99systeminfo" em "/etc/init.d". O BusyBox executa esse script durante o
boot, iniciando o servidor em segundo plano.


