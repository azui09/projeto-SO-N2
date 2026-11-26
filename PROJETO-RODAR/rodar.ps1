# Roda uma frente do Projeto 2 no Windows. Faz tudo sozinho:
# usa o programa pronto (ou compila), gera o arquivo de teste, mede e mostra o resumo.
#
#   .\rodar.ps1 -Frente basico
#
# Frentes: basico, tamanho, buffer, threads, leitura, cifra
# Cada condicao: 1 rodada de aquecimento (descartada) + 10 medidas (3 no "basico").
# Metrica: pico de memoria (MB). Saida bruta em dados\FRENTE_windows.csv.
param(
    [string]$Frente = "basico",
    [int]$Repeticoes = 0
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if ($Repeticoes -le 0) { $Repeticoes = if ($Frente -eq "basico") { 3 } else { 10 } }
$MB = 100
$Tamanhos = @(10, 100, 500)

# --- programa: usa o pronto (bin\medir_windows.exe) ou compila ---
if (-not (Test-Path ".\medir.exe")) {
    if (Test-Path ".\bin\medir_windows.exe") {
        Copy-Item ".\bin\medir_windows.exe" ".\medir.exe"
    } elseif (Get-Command gcc -ErrorAction SilentlyContinue) {
        Write-Host "compilando medir.c..."
        & gcc -O2 -Wall -o medir.exe medir.c -lpsapi
        if ($LASTEXITCODE -ne 0) { throw "falha ao compilar" }
    } else {
        throw "Nao achei medir.exe nem gcc. Use a pasta completa que recebeu (com a pasta bin)."
    }
}
& .\medir.exe autoteste | Out-Null
if ($LASTEXITCODE -ne 0) { throw "o programa nao passou no autoteste" }

New-Item -ItemType Directory -Force -Path "dados" | Out-Null
$csv = "dados\${Frente}_windows.csv"
$log = "dados\${Frente}_windows.log"

function Garantir-Arquivo([int]$tam, [int]$cifrado) {
    $f = "dados\texto_$tam.txt"
    if (-not (Test-Path $f) -or (($cifrado -eq 1) -and -not (Test-Path "$f.enc"))) {
        Write-Host "gerando $f ($tam MB), pode levar alguns segundos..."
        if ($cifrado -eq 1) { & .\medir.exe gerar $f $tam } else { & .\medir.exe gerar $f $tam so-texto }
    }
}

$script:ArqMB = $MB; $script:Leit = "read"; $script:Buf = "64K"; $script:Thr = 1; $script:Cif = 0

$cabecalho = (& .\medir.exe cabecalho)
Set-Content -Path $csv -Value "frente,rodada,$cabecalho" -Encoding ascii
Set-Content -Path $log -Value "" -Encoding ascii

function Rodar-Condicao([string]$est) {
    Garantir-Arquivo $script:ArqMB $script:Cif
    $arq = "dados\texto_$($script:ArqMB).txt"
    if ($script:Cif -eq 1) { $arq = "$arq.enc" }
    $opcoes = @("--estrutura", $est, "--leitura", $script:Leit, "--buffer", $script:Buf, "--threads", "$($script:Thr)")
    if ($script:Cif -eq 1) { $opcoes += "--cifrado" }
    Add-Content -Path $log -Value ".\medir.exe $arq $($opcoes -join ' ')" -Encoding ascii
    Write-Host ("  {0,-5} arquivo={1}MB leitura={2} buffer={3} threads={4} cifrado={5} ... " -f $est, $script:ArqMB, $script:Leit, $script:Buf, $script:Thr, $script:Cif) -NoNewline
    for ($r = 0; $r -le $Repeticoes; $r++) {
        $rot = if ($r -eq 0) { "aquecimento" } else { "$r" }
        $linha = (& .\medir.exe $arq @opcoes)
        Add-Content -Path $csv -Value "$Frente,$rot,$linha" -Encoding ascii
    }
    Write-Host "ok"
}

Write-Host "Frente: $Frente (windows), $Repeticoes medidas por condicao + 1 de aquecimento"
foreach ($est in @("array", "lista")) {
    switch ($Frente) {
        "basico"  { Rodar-Condicao $est }
        "tamanho" { foreach ($v in $Tamanhos) { $script:ArqMB = $v; Rodar-Condicao $est }; $script:ArqMB = $MB }
        "buffer"  { foreach ($v in @("4K", "64K", "1M")) { $script:Buf = $v; Rodar-Condicao $est }; $script:Buf = "64K" }
        "threads" { foreach ($v in @(1, 2, 4)) { $script:Thr = $v; Rodar-Condicao $est }; $script:Thr = 1 }
        "leitura" { foreach ($v in @("read", "mmap")) { $script:Leit = $v; Rodar-Condicao $est }; $script:Leit = "read" }
        "cifra"   { foreach ($v in @(0, 1)) { $script:Cif = $v; Rodar-Condicao $est }; $script:Cif = 0 }
        default   { throw "frente desconhecida: $Frente (use: basico tamanho buffer threads leitura cifra)" }
    }
}

function Desvio([double[]]$x) {
    if ($x.Count -lt 2) { return 0.0 }
    $m = ($x | Measure-Object -Average).Average
    $s = 0.0
    foreach ($v in $x) { $s += ($v - $m) * ($v - $m) }
    return [math]::Sqrt($s / ($x.Count - 1))
}

Write-Host ""
Write-Host "RESULTADO (pico de memoria em MB, media +- desvio padrao de $Repeticoes medidas):"
$dados = Import-Csv $csv | Where-Object { $_.rodada -ne "aquecimento" }
$grupos = $dados | Group-Object estrutura, leitura, buffer_bytes, threads, cifrado, arquivo_bytes
foreach ($g in $grupos) {
    $p = @($g.Group | ForEach-Object { [double]::Parse($_.pico_mb, [cultureinfo]::InvariantCulture) })
    $d = @($g.Group | ForEach-Object { [double]::Parse($_.delta_mb, [cultureinfo]::InvariantCulture) })
    $f = $g.Group[0]
    $mbArq = [math]::Round([double]$f.arquivo_bytes / 1048576)
    "{0,-5} {1,-4} buf={2} thr={3} cif={4} {5}MB | pico_mb {6:N2} +- {7:N2} | delta_mb (o texto) {8:N2} +- {9:N2}" -f `
        $f.estrutura, $f.leitura, $f.buffer_bytes, $f.threads, $f.cifrado, $mbArq,
        ($p | Measure-Object -Average).Average, (Desvio $p),
        ($d | Measure-Object -Average).Average, (Desvio $d)
}

$conf = $dados | Group-Object leitura, buffer_bytes, threads, cifrado, arquivo_bytes
$ok = $true
foreach ($g in $conf) {
    $distintos = $g.Group | ForEach-Object { "$($_.linhas)/$($_.soma)" } | Sort-Object -Unique
    if (@($distintos).Count -gt 1) { Write-Host "ERRO: array e lista carregaram textos diferentes em $($g.Name)"; $ok = $false }
}
if ($ok) { Write-Host ""; Write-Host "conferencia ok: array e lista leram exatamente o mesmo texto." }
Write-Host ""
Write-Host "Mande este arquivo pro grupo / cole no M3: $csv"
