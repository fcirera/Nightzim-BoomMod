# Compatibilidade: ReShade, OptiScaler e outros proxies (v1.2.8)

O ReShade entra no DCS como `dxgi.dll` em `<DCS>\bin` (ou `bin-mt`). O OptiScaler (FSR 4 em placas AMD) também. O instalador do BoomMod X resolve o conflito: OptiScaler como `dxgi.dll`, ReShade como `ReShade64.dll` e `LoadReshade=true` no `OptiScaler.ini` (o OptiScaler carrega o ReShade).

## Cenários

| | Situação em `bin` | O que o instalador faz |
|---|---|---|
| A | nenhum proxy | avisa para instalar o ReShade 6.x "with full add-on support" (o instalador não traz o ReShade) |
| B | ReShade = `dxgi.dll` | mantém; o botão **Verificar compatibilidade** oferece o modo D |
| C | OptiScaler = `dxgi.dll` | ReShade entra como `ReShade64.dll` (o seu arquivo); no `OptiScaler.ini` só a chave `LoadReshade` vira `true` |
| D | ReShade = `dxgi.dll` + quer o OptiScaler | backup em `bin\_nightzim_backup\<data>`, ReShade renomeado para `ReShade64.dll`, OptiScaler copiado da pasta escolhida, `LoadReshade=true` |
| E | `dxgi.dll` desconhecido | nunca sobrescreve; mostra o produto do arquivo e oferece o ReShade como `d3d11.dll` |

Proxies procurados: `dxgi`, `d3d11`, `d3d12`, `dinput8`, `winmm`, `version`, `dbghelp`, `wininet`, `winhttp` e `ReShade64` (.dll), identificados pelo VersionInfo. `bin` ou `bin-mt`: pelos atalhos do DCS na Área de Trabalho e no Menu Iniciar; na dúvida o instalador pergunta. Com `bin-mt`, o `ReShade.ini` de `bin-mt` recebe `AddonPath` apontando para `bin`.

Desinstalar: remove só o que está no manifesto (`Saved Games\DCS\Nightzim BoomMod X\compat_manifest.json`, com nome, SHA-256 e cenário); arquivo alterado depois da instalação fica; se o OptiScaler fica, o `ReShade64.dll` colocado pelo instalador sai e `LoadReshade` volta a `false` (ou ao valor anterior, se o `ReShade64.dll` já era seu); o backup é restaurado.

## Verificações

| Item | Situação |
|---|---|
| ReShade carregado como `ReShade64.dll` lê `ReShade.ini`, grava `ReShade.log` e carrega o add-on de `bin` | **conferido** no PC do autor (ReShade 6.8.0, OptiScaler 0.9.5.5, 22/11/2026): o log mostra `loaded from ...\ReShade64.dll`, `Loading add-on from ...\NightzimBoom.addon64` e `runtime ... ('...\ReShade.ini')` |
| Cenários A a E (arquivos, `OptiScaler.ini`, manifesto, desinstalação) | **conferido** em teste automático com os arquivos reais em pastas de teste: só a linha `LoadReshade` muda no `.ini`; desinstalar volta ao estado anterior |
| Profundidade com upscaler (DLSS/FSR via OptiScaler): o add-on aceita profundidade menor que a tela (mesma proporção, 1/3 a 1x) e escala as coordenadas pelo tamanho da profundidade | implementado; **falta testar no jogo** com DLSS Quality e Performance |
| VR (OpenXR, Quest 3), dois olhos, upscaler ligado | implementado (rastro do SCUD, alvos de tela larga não viram olho, conferência dos eixos da câmera); **falta testar no óculos** |
| Integrity check (escudo verde) nos cenários A–D | o mod não mexe em arquivos verificados pelo IC além dos que já mexia; **falta conferir no servidor** |
| Pod de mira no MFD | caminho do pod não mudou; **falta conferir no jogo** |
