# Nightzim BoomMod v3.2c - instalar (metodo que passa na verificacao de integridade)
# Nao toca em Config\Effects. Os efeitos novos entram em Bazar\Effects\ParticleSystem2\ffx com os NOMES dos originais;
# os originais ficam guardados, com hash, em Saved Games\DCS\Nightzim BoomMod\Backup\<pasta do DCS>\ffx_originais.
# Uso: instalar.bat      ou      powershell -ExecutionPolicy Bypass -File instalar.ps1 -DCS "D:\DCS World"
param([string]$DCS = "", [switch]$Forcar)
$ErrorActionPreference = "Stop"
function AchaDCS {
  $c = @()
  foreach ($k in "HKCU:\Software\Eagle Dynamics\DCS World", "HKCU:\Software\Eagle Dynamics\DCS World OpenBeta") {
    try { $c += (Get-ItemProperty $k -EA Stop).Path } catch {}
  }
  $c += "F:\DCS World", "C:\Program Files\Eagle Dynamics\DCS World", "D:\DCS World", "E:\DCS World", "C:\Program Files (x86)\Steam\steamapps\common\DCSWorld", "D:\SteamLibrary\steamapps\common\DCSWorld"
  foreach ($p in $c) { if ($p -and (Test-Path (Join-Path $p "Bazar\Effects\ParticleSystem2\ffx"))) { return $p.TrimEnd("\") } }
  return ""
}
if ((Get-Process DCS -EA 0) -and -not $Forcar) { Write-Host "O DCS esta aberto. Feche o jogo e rode de novo." -ForegroundColor Yellow; exit 1 }
if (-not $DCS) { $DCS = AchaDCS }
if (-not $DCS) { $DCS = (Read-Host "Nao achei o DCS. Digite a pasta de instalacao (ex.: D:\DCS World)").Trim('"').TrimEnd("\") }
$FFX = Join-Path $DCS "Bazar\Effects\ParticleSystem2\ffx"
if (-not (Test-Path $FFX)) { throw "Pasta de efeitos nao encontrada: $FFX" }
$MOD = Split-Path -Parent $MyInvocation.MyCommand.Path
$RAIZ = Join-Path $env:USERPROFILE "Saved Games\DCS\Nightzim BoomMod"
$BKP = Join-Path $RAIZ ("Backup\" + ($DCS -replace "[:\\]+","_").Trim("_"))
$ORIG = Join-Path $BKP "ffx_originais"
$LOG = Join-Path $RAIZ "log.txt"
New-Item -ItemType Directory -Force $ORIG | Out-Null
Set-Content (Join-Path $BKP "caminho_dcs.txt") $DCS
function L($m){ $s="$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')  $m"; Write-Host $s; Add-Content $LOG $s }
function Hsh($p){ (Get-FileHash $p -Algorithm SHA256).Hash }
L "BoomMod v3.2c - instalar - DCS: $DCS"

# 1) se uma versao antiga do mod alterou Config\Effects, devolve os originais (e o que reprova na verificacao)
$CFG = Join-Path $DCS "Config\Effects\ParticleSystem2"
foreach ($c in "airExplosion.lua","groundExplosion.lua","tankShot.lua") {
  $b = Join-Path $BKP $c; $a = Join-Path $CFG $c
  if ((Test-Path $b) -and (Test-Path $a) -and (Select-String $a -Pattern "nightzim" -Quiet)) { Copy-Item $b $a -Force; L "Config: $c original restaurado" }
}

# 2) efeitos: original -> arquivo do mod
$PARES = @(
  @("airExplosion_01",        "nightzim_air_02"),
  @("groundExplosion_big",    "nightzim_jdam_01"),
  @("groundExplosion_03_01",  "nightzim_mk82_01"),
  @("groundExplosion_nar",    "nightzim_nar_01"),
  @("groundExplosion_napalm", "nightzim_napalm_01"),
  @("T90A_shot_exp_b",        "nightzim_tankshot_01")
)
foreach ($p in $PARES) {
  $o, $n = $p
  $srcF = Join-Path $MOD "$n.ffx"; $srcL = Join-Path $MOD "$n.lua"
  if (-not (Test-Path $srcF) -or -not (Test-Path $srcL)) { throw "Faltam arquivos do mod nesta pasta: $n" }
  $dstF = Join-Path $FFX "$o.ffx"; $dstL = Join-Path $FFX "$o.lua"
  if (-not (Test-Path $dstF)) { L "$o : nao existe neste DCS - pulado"; continue }
  $marca = Join-Path $ORIG "$o.instalado.sha256"
  $cur = Hsh $dstF
  $nosso = ((Test-Path $marca) -and ($cur -eq (Get-Content $marca -First 1).Trim())) -or ($cur -eq (Hsh $srcF))
  if (-not $nosso) {
    # o que esta no jogo e um original (primeira vez, ou o DCS atualizou/reparou): guarda antes de trocar
    foreach ($ext in ".ffx",".lua") {
      $a = Join-Path $FFX "$o$ext"; $b = Join-Path $ORIG "$o$ext"
      if ((Test-Path $b) -and ((Hsh $b) -ne (Hsh $a))) { Copy-Item $b "$b.anterior" -Force }
      Copy-Item $a $b -Force; (Hsh $b) | Set-Content "$b.sha256"
    }
    L "backup do original: $o"
  }
  Copy-Item $srcF $dstF -Force
  $t = [IO.File]::ReadAllText($srcL)
  if (-not $t.Contains("[[$n]]")) { throw "$n.lua nao tem o nome esperado" }
  [IO.File]::WriteAllText($dstL, $t.Replace("[[$n]]", "[[$o]]"))
  (Hsh $dstF) | Set-Content $marca
  L "$o <- $n"
}
L "INSTALADO. Abra o DCS: o escudo do multiplayer deve continuar verde. Para desfazer: rode o .bat de novo e escolha 2"
L "Obs.: atualizacao ou reparo do DCS devolve os efeitos originais; basta rodar o .bat de novo."
