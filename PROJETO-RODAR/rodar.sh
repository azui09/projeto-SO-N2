#!/usr/bin/env bash
# Roda uma frente do Projeto 2 (Linux e macOS). Faz tudo sozinho:
# compila (ou usa o programa pronto), gera o arquivo de teste, mede e mostra o resumo.
#
#   ./rodar.sh FRENTE        (sem FRENTE roda "basico", a primeira medicao do M3)
#
# FRENTE:
#   basico    array vs lista, tudo no padrao           (3 medidas)
#   tamanho   arquivo de 10, 100 e 500 MB
#   buffer    buffer de leitura de 4K, 64K e 1M
#   threads   1, 2 e 4 threads
#   leitura   read vs mmap
#   cifra     texto puro vs texto cifrado
#
# Cada condicao: 1 rodada de aquecimento (descartada) + 10 medidas (3 no "basico").
# Metrica: pico de memoria (MB). Saida bruta em dados/FRENTE_SO.csv.
set -euo pipefail
cd "$(dirname "$0")"

FRENTE="${1:-basico}"
if [ "$FRENTE" = basico ]; then REPS="${2:-3}"; else REPS="${2:-10}"; fi
MB_PADRAO=100
TAMANHOS="10 100 500"

case "$(uname -s)" in
  Linux) SO=linux ;;
  Darwin) SO=macos ;;
  *) SO=posix ;;
esac

# --- programa: compila se tiver compilador, senao usa o pronto da pasta bin/ ---
if [ ! -x ./medir ]; then
  if command -v cc >/dev/null 2>&1; then
    echo "compilando medir.c..."
    if [ "$SO" = linux ]; then cc -O2 -Wall -pthread -o medir medir.c; else cc -O2 -Wall -o medir medir.c; fi
  elif [ "$SO" = macos ]; then
    if [ "$(uname -m)" = arm64 ]; then cp bin/medir_macos_arm64 ./medir; else cp bin/medir_macos_x64 ./medir; fi
    chmod +x ./medir
    xattr -d com.apple.quarantine ./medir 2>/dev/null || true
  else
    echo "Nao achei compilador (gcc/cc). Instale o gcc e rode de novo."; exit 1
  fi
fi
./medir autoteste >/dev/null || { echo "o programa nao passou no autoteste"; exit 1; }

mkdir -p dados
CSV="dados/${FRENTE}_${SO}.csv"
: > "$CSV"

garantir_arquivo() { # $1 = MB, $2 = 1 se precisa da versao cifrada
  local f="dados/texto_$1.txt"
  if [ ! -f "$f" ] || { [ "$2" = 1 ] && [ ! -f "$f.enc" ]; }; then
    echo "gerando $f ($1 MB), pode levar alguns segundos..."
    if [ "$2" = 1 ]; then ./medir gerar "$f" "$1"; else ./medir gerar "$f" "$1" so-texto; fi
  fi
}

ARQ_MB=$MB_PADRAO; LEIT=read; BUF=64K; THR=1; CIF=0

echo "frente,rodada,$(./medir cabecalho)" > "$CSV"

rodar_condicao() { # $1 = estrutura
  local est="$1" arq opcoes
  garantir_arquivo "$ARQ_MB" "$CIF"
  if [ "$CIF" = 1 ]; then arq="dados/texto_${ARQ_MB}.txt.enc"; else arq="dados/texto_${ARQ_MB}.txt"; fi
  opcoes=(--estrutura "$est" --leitura "$LEIT" --buffer "$BUF" --threads "$THR")
  [ "$CIF" = 1 ] && opcoes+=(--cifrado)
  echo "./medir $arq ${opcoes[*]}" >> "dados/${FRENTE}_${SO}.log"
  printf "  %-5s arquivo=%sMB leitura=%s buffer=%s threads=%s cifrado=%s ... " "$est" "$ARQ_MB" "$LEIT" "$BUF" "$THR" "$CIF"
  local r rot
  for r in $(seq 0 "$REPS"); do
    rot="$r"; [ "$r" = 0 ] && rot=aquecimento
    echo "$FRENTE,$rot,$(./medir "$arq" "${opcoes[@]}")" >> "$CSV"
  done
  echo ok
}

: > "dados/${FRENTE}_${SO}.log"
echo "Frente: $FRENTE ($SO), $REPS medidas por condicao + 1 de aquecimento"
for est in array lista; do
  case "$FRENTE" in
    basico) rodar_condicao "$est" ;;
    tamanho) for v in $TAMANHOS; do ARQ_MB=$v; rodar_condicao "$est"; done; ARQ_MB=$MB_PADRAO ;;
    buffer) for v in 4K 64K 1M; do BUF=$v; rodar_condicao "$est"; done; BUF=64K ;;
    threads) for v in 1 2 4; do THR=$v; rodar_condicao "$est"; done; THR=1 ;;
    leitura) for v in read mmap; do LEIT=$v; rodar_condicao "$est"; done; LEIT=read ;;
    cifra) for v in 0 1; do CIF=$v; rodar_condicao "$est"; done; CIF=0 ;;
    *) echo "frente desconhecida: $FRENTE (use: basico tamanho buffer threads leitura cifra)"; exit 1 ;;
  esac
done

echo
echo "RESULTADO (pico de memoria em MB, media +- desvio padrao de $REPS medidas):"
awk -F, '
NR == 1 || $2 == "aquecimento" { next }
{
  k = $4 " | " $5 " | buf=" $6 " | thr=" $7 " | cif=" $8 " | " int($9 / 1048576 + 0.5) "MB"
  if (!(k in n)) ordem[++m] = k
  n[k]++
  pico[k] += $13; pico2[k] += $13 * $13
  delta[k] += $14; delta2[k] += $14 * $14
  chave = $5 "|" $6 "|" $7 "|" $8 "|" $9
  conf = $10 "/" $11
  if (!(chave in ref)) ref[chave] = conf
  else if (ref[chave] != conf) { print "ERRO: array e lista carregaram textos diferentes em " chave; erro = 1 }
}
function sd(s, s2, c) { if (c < 2) return 0; v = (s2 - s * s / c) / (c - 1); return v > 0 ? sqrt(v) : 0 }
END {
  printf "%-46s %-20s %-20s\n", "condicao", "pico_mb", "delta_mb (o texto)"
  for (i = 1; i <= m; i++) {
    k = ordem[i]; c = n[k]
    printf "%-46s %8.2f +- %-8.2f %8.2f +- %-8.2f\n", k, pico[k] / c, sd(pico[k], pico2[k], c), delta[k] / c, sd(delta[k], delta2[k], c)
  }
  if (!erro) print "\nconferencia ok: array e lista leram exatamente o mesmo texto."
}' "$CSV"
echo
echo "Mande este arquivo pro grupo / cole no M3: $CSV"
