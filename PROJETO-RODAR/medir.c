/*
 * medir.c - Projeto 2 de Sistemas Operacionais (custo de um buffer de texto)
 *
 * UM programa so, para as cinco frentes. Cada frente mexe em UM argumento:
 *
 *   --estrutura array|lista   variavel do grupo (todas as frentes)
 *   --leitura   read|mmap     frente de persistencia
 *   --buffer    4K|64K|1M     frente de chamada de sistema (so vale com read)
 *   --threads   1|2|4         frente de concorrencia
 *   --cifrado                 frente de seguranca (arquivo cifrado com ChaCha20)
 *   (o tamanho do arquivo e simplesmente outro arquivo de entrada)
 *
 * Uso:
 *   medir gerar texto.txt 100         cria texto.txt e texto.txt.enc (100 MB)
 *   medir gerar texto.txt 100 so-texto   cria so o texto.txt (sem a versao cifrada)
 *   medir hash texto.txt              imprime tamanho, linhas e hash FNV-1a
 *   medir cabecalho                   imprime o cabecalho do CSV
 *   medir autoteste                   confere o ChaCha20 contra o vetor da RFC 8439
 *   medir ARQUIVO [opcoes]            carrega o arquivo e imprime UMA linha CSV
 *
 * Compilar:
 *   Linux:   gcc -O2 -Wall -pthread -o medir medir.c
 *   macOS:   clang -O2 -Wall -o medir medir.c
 *   Windows: gcc -O2 -Wall -o medir.exe medir.c -lpsapi      (MinGW-w64 / MSYS2)
 *            cl /O2 medir.c psapi.lib                          (compilador da Microsoft)
 *
 * Premissas: o texto nao tem bytes NUL (a lista de linhas guarda strings C).
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifdef _WIN32
#define __USE_MINGW_ANSI_STDIO 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SEEK64(f, o) _fseeki64((f), (__int64)(o), SEEK_SET)
#define SEEKEND64(f) _fseeki64((f), 0, SEEK_END)
#define TELL64(f) ((int64_t)_ftelli64(f))
#define NOME_SO "windows"
#else
#define SEEK64(f, o) fseeko((f), (off_t)(o), SEEK_SET)
#define SEEKEND64(f) fseeko((f), 0, SEEK_END)
#define TELL64(f) ((int64_t)ftello(f))
#if defined(__APPLE__)
#define NOME_SO "macos"
#elif defined(__linux__)
#define NOME_SO "linux"
#else
#define NOME_SO "posix"
#endif
#endif

#define MAX_THREADS 256

/* ------------------------------------------------------------------ */
/* ChaCha20 (RFC 8439), implementado aqui para nao depender de biblioteca */
/* ------------------------------------------------------------------ */

static const char CHAVE[] = "projeto2-so-chave-fixa-didatica!"; /* so os 32 primeiros bytes valem */
static const uint8_t NONCE[12] = {'s', 'o', '-', '2', '0', '2', '6', '-', 'n', 'o', 'n', 'c'};

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

#define QR(a, b, c, d)                                                         \
    a += b; d ^= a; d = rotl32(d, 16);                                         \
    c += d; b ^= c; b = rotl32(b, 12);                                         \
    a += b; d ^= a; d = rotl32(d, 8);                                          \
    c += d; b ^= c; b = rotl32(b, 7);

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void chacha_bloco(const uint8_t key[32], const uint8_t nonce[12], uint32_t ctr, uint8_t out[64]) {
    uint32_t s[16], x[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = le32(key + 4 * i);
    s[12] = ctr;
    for (int i = 0; i < 3; i++) s[13 + i] = le32(nonce + 4 * i);
    memcpy(x, s, sizeof s);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t w = x[i] + s[i];
        out[4 * i] = (uint8_t)w;
        out[4 * i + 1] = (uint8_t)(w >> 8);
        out[4 * i + 2] = (uint8_t)(w >> 16);
        out[4 * i + 3] = (uint8_t)(w >> 24);
    }
}

/* Cifra/decifra (e a mesma operacao) buf[0..n) que esta na posicao 'offset' do arquivo. */
static void chacha_xor(unsigned char *buf, size_t n, uint64_t offset) {
    uint8_t ks[64];
    while (n > 0) {
        uint64_t blk = offset / 64;
        size_t skip = (size_t)(offset % 64);
        chacha_bloco((const uint8_t *)CHAVE, NONCE, (uint32_t)blk, ks);
        size_t take = 64 - skip;
        if (take > n) take = n;
        for (size_t i = 0; i < take; i++) buf[i] ^= ks[skip + i];
        buf += take;
        n -= take;
        offset += take;
    }
}

static int autoteste(void) {
    uint8_t key[32], nonce[12] = {0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0}, out[64];
    static const uint8_t esperado[16] = {0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15,
                                         0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4};
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    chacha_bloco(key, nonce, 1, out);
    if (memcmp(out, esperado, 16) != 0) {
        printf("autoteste: FALHOU (ChaCha20 nao bate com o vetor da RFC 8439)\n");
        return 1;
    }
    /* cifrar em pedaços de tamanhos estranhos tem que dar o mesmo resultado que de uma vez */
    unsigned char a[1000], b[1000];
    for (int i = 0; i < 1000; i++) a[i] = b[i] = (unsigned char)(i * 7);
    chacha_xor(a, 1000, 33);
    chacha_xor(b, 13, 33);
    chacha_xor(b + 13, 500, 33 + 13);
    chacha_xor(b + 513, 487, 33 + 513);
    if (memcmp(a, b, 1000) != 0) {
        printf("autoteste: FALHOU (cifra em pedacos difere da cifra inteira)\n");
        return 1;
    }
    printf("autoteste: ok\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Relogio e memoria, por sistema                                       */
/* ------------------------------------------------------------------ */

/* Pico de memoria residente do processo ate agora, em MB. */
static double pico_mb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc))
        return (double)pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
    return -1.0;
#else
#if defined(__linux__)
    /* VmHWM e o pico real deste processo. O ru_maxrss pode herdar o pico do processo
       que nos iniciou (por causa do fork), entao preferimos o /proc. */
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char linha[256];
        while (fgets(linha, sizeof linha, f)) {
            unsigned long long kb;
            if (sscanf(linha, "VmHWM: %llu kB", &kb) == 1) {
                fclose(f);
                return (double)kb / 1024.0;
            }
        }
        fclose(f);
    }
#endif
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1.0;
#if defined(__APPLE__)
    return (double)ru.ru_maxrss / (1024.0 * 1024.0); /* macOS devolve bytes */
#else
    return (double)ru.ru_maxrss / 1024.0; /* Linux devolve KB */
#endif
#endif
}

/* ------------------------------------------------------------------ */
/* Estruturas                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *arquivo;
    int lista;     /* 0 = array contiguo, 1 = lista de linhas */
    int usa_mmap;  /* 0 = read (fread sem buffer), 1 = mmap */
    int threads;
    int cifrado;
    size_t buffer; /* tamanho de cada leitura, so para read */
    uint64_t tam;  /* tamanho do arquivo */
    unsigned char *base; /* mapeamento (mmap) */
    unsigned char *arr;  /* array contiguo (read + array) */
} Cfg;

typedef struct {
    char **v; /* o array de ponteiros */
    size_t n, cap;
    unsigned char *acum; /* parte de linha que atravessa dois blocos lidos */
    size_t acum_len, acum_cap;
    int sem_quebra; /* a ultima linha do arquivo nao termina em \n */
} Lista;

typedef struct {
    const Cfg *c;
    uint64_t ini, fim; /* faixa do arquivo, sempre comecando no inicio de uma linha */
    Lista L;
    int erro;
} Trab;

static void lista_add(Lista *L, char *p) {
    if (L->n == L->cap) {
        size_t nc = L->cap ? L->cap * 2 : 1024;
        char **nv = (char **)realloc(L->v, nc * sizeof(char *));
        if (!nv) {
            fprintf(stderr, "sem memoria na lista\n");
            exit(2);
        }
        L->v = nv;
        L->cap = nc;
    }
    L->v[L->n++] = p;
}

static void acum_append(Lista *L, const unsigned char *s, size_t n) {
    if (L->acum_len + n > L->acum_cap) {
        size_t nc = L->acum_cap ? L->acum_cap : 256;
        while (nc < L->acum_len + n) nc *= 2;
        unsigned char *na = (unsigned char *)realloc(L->acum, nc);
        if (!na) {
            fprintf(stderr, "sem memoria no acumulador\n");
            exit(2);
        }
        L->acum = na;
        L->acum_cap = nc;
    }
    memcpy(L->acum + L->acum_len, s, n);
    L->acum_len += n;
}

/* Fecha uma linha: junta o que sobrou do bloco anterior com o segmento atual,
   e faz UMA alocacao propria para ela (esse e o custo que a pergunta mede). */
static void emite_linha(Lista *L, const unsigned char *seg, size_t len) {
    size_t total = L->acum_len + len;
    char *p = (char *)malloc(total + 1);
    if (!p) {
        fprintf(stderr, "sem memoria para uma linha\n");
        exit(2);
    }
    if (L->acum_len) memcpy(p, L->acum, L->acum_len);
    if (len) memcpy(p + L->acum_len, seg, len);
    p[total] = '\0';
    L->acum_len = 0;
    lista_add(L, p);
}

static void parse_bloco(Lista *L, const unsigned char *buf, size_t n, int fim) {
    size_t ini = 0;
    while (ini < n) {
        const unsigned char *q = (const unsigned char *)memchr(buf + ini, '\n', n - ini);
        if (!q) break;
        size_t i = (size_t)(q - buf);
        emite_linha(L, buf + ini, i - ini);
        ini = i + 1;
    }
    if (ini < n) acum_append(L, buf + ini, n - ini);
    if (fim && L->acum_len > 0) {
        emite_linha(L, NULL, 0);
        L->sem_quebra = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Leitura                                                              */
/* ------------------------------------------------------------------ */

static int64_t tamanho_arquivo(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (SEEKEND64(f) != 0) {
        fclose(f);
        return -1;
    }
    int64_t t = TELL64(f);
    fclose(f);
    return t;
}

/* Le n bytes a partir de pos (e decifra, se for o caso). Usado so para achar fronteiras de linha. */
static size_t ler_em(const Cfg *c, uint64_t pos, unsigned char *buf, size_t n) {
    FILE *f = fopen(c->arquivo, "rb");
    if (!f) return 0;
    if (SEEK64(f, pos) != 0) {
        fclose(f);
        return 0;
    }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (c->cifrado) chacha_xor(buf, got, pos);
    return got;
}

/* Primeiro inicio de linha em posicao >= pos. */
static uint64_t proximo_inicio(const Cfg *c, uint64_t pos) {
    if (pos == 0) return 0;
    if (pos >= c->tam) return c->tam;
    unsigned char tmp[4096];
    uint64_t p = pos - 1;
    for (;;) {
        size_t got = ler_em(c, p, tmp, sizeof tmp);
        if (got == 0) return c->tam;
        for (size_t i = 0; i < got; i++)
            if (tmp[i] == '\n') return p + i + 1;
        p += got;
        if (p >= c->tam) return c->tam;
    }
}

static void trabalhar(Trab *t) {
    const Cfg *c = t->c;
    uint64_t ini = t->ini, fim = t->fim;
    if (ini >= fim) return;

    if (c->usa_mmap) {
        unsigned char *p = c->base + ini;
        size_t n = (size_t)(fim - ini);
        if (c->cifrado) chacha_xor(p, n, ini); /* mapeamento privado: copia na escrita */
        if (c->lista) parse_bloco(&t->L, p, n, fim == c->tam);
        return;
    }

    FILE *f = fopen(c->arquivo, "rb");
    if (!f || SEEK64(f, ini) != 0) {
        if (f) fclose(f);
        t->erro = 1;
        return;
    }
    setvbuf(f, NULL, _IONBF, 0); /* sem buffer do stdio: cada fread vira uma chamada de sistema */
    unsigned char *buf = NULL;
    if (c->lista) {
        buf = (unsigned char *)malloc(c->buffer);
        if (!buf) {
            fclose(f);
            t->erro = 1;
            return;
        }
    }
    uint64_t pos = ini;
    while (pos < fim) {
        size_t q = c->buffer;
        if (fim - pos < q) q = (size_t)(fim - pos);
        unsigned char *dst = c->lista ? buf : c->arr + pos;
        size_t got = fread(dst, 1, q, f);
        if (got == 0) {
            t->erro = 1;
            break;
        }
        if (c->cifrado) chacha_xor(dst, got, pos);
        if (c->lista) parse_bloco(&t->L, dst, got, (pos + got == fim) && fim == c->tam);
        pos += got;
    }
    fclose(f);
    free(buf);
}

#ifdef _WIN32
static DWORD WINAPI tramp(LPVOID p) {
    trabalhar((Trab *)p);
    return 0;
}
#else
static void *tramp(void *p) {
    trabalhar((Trab *)p);
    return NULL;
}
#endif

static int mapear(Cfg *c) {
#ifdef _WIN32
    HANDLE h = CreateFileA(c->arquivo, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 1;
    HANDLE m = CreateFileMappingA(h, NULL, c->cifrado ? PAGE_WRITECOPY : PAGE_READONLY, 0, 0, NULL);
    if (!m) {
        CloseHandle(h);
        return 1;
    }
    c->base = (unsigned char *)MapViewOfFile(m, c->cifrado ? FILE_MAP_COPY : FILE_MAP_READ, 0, 0, 0);
    CloseHandle(m);
    CloseHandle(h);
    return c->base ? 0 : 1;
#else
    int fd = open(c->arquivo, O_RDONLY);
    if (fd < 0) return 1;
    void *p = mmap(NULL, (size_t)c->tam, c->cifrado ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return 1;
    c->base = (unsigned char *)p;
    return 0;
#endif
}

static int carregar(Cfg *c, Trab **out_t) {
    int T = c->threads;
    uint64_t b[MAX_THREADS + 1];
    b[0] = 0;
    for (int i = 1; i < T; i++) {
        b[i] = proximo_inicio(c, (c->tam / (uint64_t)T) * (uint64_t)i);
        if (b[i] < b[i - 1]) b[i] = b[i - 1];
    }
    b[T] = c->tam;

    if (c->usa_mmap) {
        if (mapear(c) != 0) {
            fprintf(stderr, "falha ao mapear o arquivo\n");
            return 1;
        }
    } else if (!c->lista) {
        c->arr = (unsigned char *)malloc((size_t)c->tam);
        if (!c->arr) {
            fprintf(stderr, "sem memoria para o array de %llu bytes\n", (unsigned long long)c->tam);
            return 1;
        }
    }

    Trab *tr = (Trab *)calloc((size_t)T, sizeof(Trab));
    if (!tr) return 1;
    for (int i = 0; i < T; i++) {
        tr[i].c = c;
        tr[i].ini = b[i];
        tr[i].fim = b[i + 1];
    }

    if (T == 1) {
        trabalhar(&tr[0]);
    } else {
#ifdef _WIN32
        HANDLE h[MAX_THREADS];
        for (int i = 0; i < T; i++) h[i] = CreateThread(NULL, 0, tramp, &tr[i], 0, NULL);
        for (int i = 0; i < T; i++) {
            WaitForSingleObject(h[i], INFINITE);
            CloseHandle(h[i]);
        }
#else
        pthread_t h[MAX_THREADS];
        for (int i = 0; i < T; i++) pthread_create(&h[i], NULL, tramp, &tr[i]);
        for (int i = 0; i < T; i++) pthread_join(h[i], NULL);
#endif
    }
    for (int i = 0; i < T; i++)
        if (tr[i].erro) {
            fprintf(stderr, "erro de leitura na faixa %d\n", i);
            return 1;
        }
    *out_t = tr;
    return 0;
}

/* Percorre a estrutura uma vez: conta linhas e soma os bytes (confere que todas as
   variacoes carregaram exatamente o mesmo texto, e obriga o mmap a tocar as paginas
   antes de medirmos o pico de memoria). */
static void varrer(const Cfg *c, Trab *tr, unsigned long long *linhas, unsigned long long *soma) {
    unsigned long long l = 0, s = 0;
    if (!c->lista) {
        const unsigned char *p = c->usa_mmap ? c->base : c->arr;
        for (uint64_t i = 0; i < c->tam; i++) {
            s += p[i];
            l += (p[i] == '\n');
        }
        if (c->tam > 0 && p[c->tam - 1] != '\n') l++;
    } else {
        int sem_quebra = 0;
        for (int k = 0; k < c->threads; k++) {
            Lista *L = &tr[k].L;
            for (size_t i = 0; i < L->n; i++) {
                const unsigned char *q = (const unsigned char *)L->v[i];
                while (*q) s += *q++;
            }
            l += L->n;
            if (L->sem_quebra) sem_quebra = 1;
        }
        s += 10ULL * (l - (sem_quebra ? 1 : 0)); /* os '\n' que nao ficaram guardados */
    }
    *linhas = l;
    *soma = s;
}

/* ------------------------------------------------------------------ */
/* Gerar o arquivo de teste e conferir hash                             */
/* ------------------------------------------------------------------ */

static uint64_t xs_estado = 0x9E3779B97F4A7C15ULL;
static uint64_t xs_prox(void) {
    xs_estado ^= xs_estado >> 12;
    xs_estado ^= xs_estado << 25;
    xs_estado ^= xs_estado >> 27;
    return xs_estado * 0x2545F4914F6CDD1DULL;
}

static uint64_t fnv_passo(uint64_t h, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

#define BLK (1u << 20)

static int cmd_gerar(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "uso: medir gerar ARQUIVO MB [so-texto]\n");
        return 1;
    }
    int com_enc = !(argc >= 5 && strcmp(argv[4], "so-texto") == 0);
    unsigned long long mb = strtoull(argv[3], NULL, 10);
    if (mb == 0) {
        fprintf(stderr, "MB invalido\n");
        return 1;
    }
    char nome_enc[1024];
    snprintf(nome_enc, sizeof nome_enc, "%s.enc", argv[2]);
    FILE *fp = fopen(argv[2], "wb");
    FILE *fe = com_enc ? fopen(nome_enc, "wb") : NULL;
    if (!fp || (com_enc && !fe)) {
        fprintf(stderr, "nao consegui criar %s\n", argv[2]);
        return 1;
    }
    unsigned char *blk = (unsigned char *)malloc(BLK + 256);
    unsigned char *enc = (unsigned char *)malloc(BLK + 256);
    if (!blk || !enc) return 1;
    static const char alfabeto[] = "abcdefghijklmnopqrstuvwxyz ";
    unsigned long long alvo = mb * 1024ULL * 1024ULL, total = 0, linhas = 0;
    uint64_t hash = 0xCBF29CE484222325ULL;
    size_t bn = 0;
    xs_estado = 0x9E3779B97F4A7C15ULL;
    while (total < alvo) {
        size_t len = 20 + (size_t)((xs_prox() >> 33) % 101);
        if (bn + len + 1 > BLK) {
            fwrite(blk, 1, bn, fp);
            hash = fnv_passo(hash, blk, bn);
            if (com_enc) {
                memcpy(enc, blk, bn);
                chacha_xor(enc, bn, total - bn);
                fwrite(enc, 1, bn, fe);
            }
            bn = 0;
        }
        for (size_t i = 0; i < len; i++) blk[bn++] = (unsigned char)alfabeto[(xs_prox() >> 40) % 27];
        blk[bn++] = '\n';
        total += len + 1;
        linhas++;
    }
    if (bn) {
        fwrite(blk, 1, bn, fp);
        hash = fnv_passo(hash, blk, bn);
        if (com_enc) {
            memcpy(enc, blk, bn);
            chacha_xor(enc, bn, total - bn);
            fwrite(enc, 1, bn, fe);
        }
    }
    fclose(fp);
    if (fe) fclose(fe);
    printf("%s: %llu bytes, %llu linhas, fnv1a=%016llx\n", argv[2], total, linhas, (unsigned long long)hash);
    if (com_enc) printf("%s: cifrado com ChaCha20\n", nome_enc);
    return 0;
}

static int cmd_hash(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: medir hash ARQUIVO\n");
        return 1;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
        fprintf(stderr, "nao consegui abrir %s\n", argv[2]);
        return 1;
    }
    unsigned char *buf = (unsigned char *)malloc(BLK);
    uint64_t h = 0xCBF29CE484222325ULL;
    unsigned long long total = 0, linhas = 0;
    size_t n;
    while ((n = fread(buf, 1, BLK, f)) > 0) {
        h = fnv_passo(h, buf, n);
        total += n;
        for (size_t i = 0; i < n; i++) linhas += (buf[i] == '\n');
    }
    fclose(f);
    printf("%s: %llu bytes, %llu linhas, fnv1a=%016llx\n", argv[2], total, linhas, (unsigned long long)h);
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

#define CABECALHO                                                                                  \
    "so,estrutura,leitura,buffer_bytes,threads,cifrado,arquivo_bytes,linhas,soma,base_mb,pico_mb," \
    "delta_mb"

static size_t parse_tam(const char *s) {
    char *fim;
    unsigned long long v = strtoull(s, &fim, 10);
    if (*fim == 'K' || *fim == 'k') v *= 1024ULL;
    else if (*fim == 'M' || *fim == 'm') v *= 1024ULL * 1024ULL;
    return (size_t)v;
}

static void uso(void) {
    fprintf(stderr,
            "uso:\n"
            "  medir gerar ARQUIVO MB [so-texto]\n"
            "  medir hash ARQUIVO\n"
            "  medir cabecalho\n"
            "  medir autoteste\n"
            "  medir ARQUIVO [--estrutura array|lista] [--leitura read|mmap] [--buffer 64K]\n"
            "                [--threads N] [--cifrado] [--cabecalho]\n");
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "gerar") == 0) return cmd_gerar(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "hash") == 0) return cmd_hash(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "autoteste") == 0) return autoteste();
    if (argc >= 2 && strcmp(argv[1], "cabecalho") == 0) {
        puts(CABECALHO);
        return 0;
    }
    if (argc < 2) {
        uso();
        return 1;
    }

    Cfg c;
    memset(&c, 0, sizeof c);
    c.arquivo = argv[1];
    c.buffer = 64 * 1024;
    c.threads = 1;
    int cabecalho = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--estrutura") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "array") == 0) c.lista = 0;
            else if (strcmp(v, "lista") == 0) c.lista = 1;
            else { uso(); return 1; }
        } else if (strcmp(a, "--leitura") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "read") == 0) c.usa_mmap = 0;
            else if (strcmp(v, "mmap") == 0) c.usa_mmap = 1;
            else { uso(); return 1; }
        } else if (strcmp(a, "--buffer") == 0 && i + 1 < argc) {
            c.buffer = parse_tam(argv[++i]);
        } else if (strcmp(a, "--threads") == 0 && i + 1 < argc) {
            c.threads = atoi(argv[++i]);
        } else if (strcmp(a, "--cifrado") == 0) {
            c.cifrado = 1;
        } else if (strcmp(a, "--cabecalho") == 0) {
            cabecalho = 1;
        } else {
            uso();
            return 1;
        }
    }
    if (c.buffer < 1) c.buffer = 1;
    if (c.threads < 1 || c.threads > MAX_THREADS) {
        fprintf(stderr, "threads deve estar entre 1 e %d\n", MAX_THREADS);
        return 1;
    }
    int64_t tam = tamanho_arquivo(c.arquivo);
    if (tam <= 0) {
        fprintf(stderr, "nao consegui abrir %s (ou esta vazio)\n", c.arquivo);
        return 1;
    }
    c.tam = (uint64_t)tam;

    double base = pico_mb();
    Trab *tr = NULL;
    if (carregar(&c, &tr) != 0) return 1;
    unsigned long long linhas, soma;
    varrer(&c, tr, &linhas, &soma);
    double pico = pico_mb(); /* depois de tudo carregado e percorrido */

    if (cabecalho) puts(CABECALHO);
    printf("%s,%s,%s,%llu,%d,%d,%llu,%llu,%llu,%.2f,%.2f,%.2f\n", NOME_SO,
           c.lista ? "lista" : "array", c.usa_mmap ? "mmap" : "read",
           (unsigned long long)(c.usa_mmap ? 0 : c.buffer), c.threads, c.cifrado,
           (unsigned long long)c.tam, linhas, soma, base, pico, pico - base);
    return 0;
}
