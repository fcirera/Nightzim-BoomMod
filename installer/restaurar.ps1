# Nightzim BoomMod v3.2c - restaurar os efeitos originais do DCS
param([string]$DCS = "", [switch]$Forcar)
$ErrorActionPreference = "Stop"
if ((Get-Process DCS -EA 0) -and -not $Forcar) { Write-Host "O DCS esta aberto. Feche o jogo e rode de novo." -ForegroundColor Yellow; exit 1 }
$RAIZ = Join-Path $env:USERPROFILE "Saved Games\DCS\Nightzim BoomMod"
$LOG = Join-Path $RAIZ "log.txt"
function L($m){ $s="$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')  $m"; Write-Host $s; Add-Content $LOG $s }
function Hsh($p){ (Get-FileHash $p -Algorithm SHA256).Hash }
# restaura em todas as instalacoes que tiverem backup (ou so na indicada em -DCS)
$pastas = Get-ChildItem (Join-Path $RAIZ "Backup") -Directory -EA 0 | Where-Object { Test-Path (Join-Path $_.FullName "ffx_originais") }
if ($DCS) { $alvo = ($DCS -replace "[:\\]+","_").Trim("_"); $pastas = $pastas | Where-Object { $_.Name -eq $alvo } }
if (-not $pastas) { L "restaurar: nenhum backup de efeitos encontrado - nada a fazer"; exit 0 }
foreach ($bk in $pastas) {
  $ORIG = Join-Path $bk.FullName "ffx_originais"
  # o nome da pasta de backup e o caminho do DCS com _ no lugar de : e \  (ex.: F_DCS World -> F:\DCS World)
  $cam = Join-Path $bk.FullName "caminho_dcs.txt"
  $d = if ($DCS) { $DCS } elseif (Test-Path $cam) { (Get-Content $cam -First 1).Trim() } else { $bk.Name.Substring(0,1) + ":\" + $bk.Name.Substring(2) }
  $FFX = Join-Path $d "Bazar\Effects\ParticleSystem2\ffx"
  if (-not (Test-Path $FFX)) { L "restaurar: nao achei $FFX (passe -DCS com a pasta certa)"; continue }
  L "BoomMod v3.2c - restaurar - DCS: $d"
  foreach ($b in Get-ChildItem $ORIG -File | Where-Object { $_.Extension -in ".ffx",".lua" }) {
    $h = "$($b.FullName).sha256"
    if ((Test-Path $h) -and ((Hsh $b.FullName) -ne (Get-Content $h -First 1).Trim())) { throw "o backup $($b.Name) nao confere com o hash - parei, nada mais foi alterado" }
    Copy-Item $b.FullName (Join-Path $FFX $b.Name) -Force
    L "restaurado $($b.Name)"
  }
  Get-ChildItem $ORIG -Filter "*.instalado.sha256" | Remove-Item
  # sobras de versoes antigas do mod (arquivos com nome proprio)
  Get-ChildItem $FFX -Filter "nightzim_*" -EA 0 | % { Remove-Item $_.FullName; L "removido $($_.Name)" }
  Get-ChildItem $bk.FullName -Filter "*.instalado_pelo_mod" -EA 0 | Remove-Item
}
L "RESTAURADO. Os backups continuam guardados em $RAIZ\Backup"
