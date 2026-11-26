# Projeto 2 de SO: custo de um buffer de texto

Instrucoes para o grupo: `COMECE_AQUI.txt`. Este arquivo e a referencia tecnica.

**Pergunta do grupo:** quanta memoria um buffer de texto consome a mais quando e armazenado como
lista de linhas em vez de um unico array contiguo, medido em pico de memoria (MB).

## O que cada frente muda

Um programa so (`medir.c`). Cada frente muda UM argumento; o resto fica no padrao do grupo
(arquivo de 100 MB, leitura `read`, buffer 64K, 1 thread, texto puro).

| Frente | Pilar | Argumento | Valores |
|---|---|---|---|
| `tamanho` | Virtualizacao de memoria | arquivo de entrada | 10, 100, 500 MB |
| `buffer` | Virtualizacao de CPU (chamada de sistema) | `--buffer` | 4K, 64K, 1M |
| `threads` | Concorrencia | `--threads` | 1, 2, 4 |
| `leitura` | Persistencia | `--leitura` | `read` vs `mmap` |
| `cifra` | Seguranca | `--cifrado` | puro vs ChaCha20 |
| `basico` | primeira medicao (M3) | nenhum | padrao |

## Arquivo de teste

O `rodar` gera `dados/texto_100.txt` sozinho. O gerador e deterministico: o arquivo e identico em
qualquer maquina. Confiram o hash (impresso na geracao; depois com `medir hash ARQUIVO`):

| Tamanho | Bytes | Linhas | fnv1a |
|---|---|---|---|
| 10 MB | 10485861 | 147821 | `d22b8df13ce0b19c` |
| 100 MB | 104857625 | 1477525 | `51e8d96146c2fc49` |
| 500 MB | 524288023 | 7385218 | `afbd5f096f216be9` |

## Colunas do CSV

| Coluna | Significado |
|---|---|
| `base_mb` | pico de memoria antes de carregar o texto (o programa em si) |
| `pico_mb` | pico de memoria depois de carregar e percorrer o texto |
| `delta_mb` | `pico_mb - base_mb`: o que o texto custou (melhor para comparar) |
| `linhas`, `soma` | tem que ser iguais em todas as variacoes (prova que leram o mesmo texto) |

Como o pico e lido: Linux usa `VmHWM` de `/proc/self/status`; macOS usa `getrusage` (em bytes);
Windows usa `PeakWorkingSetSize`. Os tres medem memoria residente de pico, mas nao sao identicos:
entre maquinas, comparem pela razao `delta_mb(lista) / delta_mb(array)` e registrem isso nos limites.

## Como cada condicao e implementada

- **array**: um unico bloco com o arquivo inteiro (`malloc` + leituras de `--buffer` bytes; no `mmap`, o proprio mapeamento).
- **lista**: um `malloc` por linha + vetor de ponteiros `char **`. Com varias threads, cada uma monta o vetor da sua faixa.
- **read**: `fread` sem buffer do stdio, entao cada `--buffer` vira uma chamada de sistema `read`.
- **mmap**: arquivo mapeado inteiro; as paginas contam no pico quando sao tocadas.
- **cifrado**: arquivo em disco cifrado com ChaCha20, decifrado na memoria (com `mmap`, mapeamento privado e decifragem no lugar).
- **threads**: o arquivo e dividido em N faixas que comecam em inicio de linha; `threads=1` nao cria thread.

## Compilar na mao (so se precisar)

| Sistema | Comando |
|---|---|
| Linux | `gcc -O2 -Wall -pthread -o medir medir.c` |
| macOS | `clang -O2 -Wall -o medir medir.c` |
| Windows (MinGW-w64 / MSYS2) | `gcc -O2 -Wall -o medir.exe medir.c -lpsapi` |
| Windows (Microsoft) | `cl /O2 medir.c psapi.lib` |

A pasta `bin/` tem versoes prontas (Windows, Mac Apple Silicon, Mac Intel), usadas pelos scripts
quando nao ha compilador. `medir autoteste` confere o ChaCha20 (o script roda isso sozinho).

## Limites que vale citar no relatorio

- Texto sintetico (linhas de 20 a 120 caracteres, letras minusculas e espaco); outros textos mudam o custo da lista.
- Cada `malloc` tem cabecalho e arredondamento, entao a lista paga mais que "8 bytes por linha".
- Com `threads > 1` o alocador cria uma area por thread, e isso tambem entra no pico.
- Pouca memoria livre (swap, cache de disco sendo descartado) distorce o pico. Fechem outros programas e meçam na tomada.
- O programa assume texto sem bytes NUL.
