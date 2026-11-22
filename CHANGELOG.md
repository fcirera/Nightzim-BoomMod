# Nightzim BoomMod X - Changelog / Notas de atualização

Every update gets an entry here. *Cada atualização ganha uma entrada aqui.*

## Unreleased - in testing / Em teste (ainda não lançado)

**English**
- **Installer options cleaned up (v1.2.7):** the "Nuke 11kt (test)" choice was removed from the JDAM explosion (the nuke is now only on the SCUD), and the old Rocket and old Mk-82 rows were removed from the VRAM tab (the New Shader versions replace them).
- **Installer fix (v1.2.7): updates did not finish.** When updating over an existing install on the same drive, copying a file over the old one failed and the update stopped half way (the error lines with accents were also dropped from the log). Files are now replaced with the Windows copy/move calls and the log is written in UTF-8.
- **Changelog in the installer (v1.2.7):** new "WHAT'S NEW" tab with these notes.
- **SCUD smoke trail (v1.2.7):** the SCUD now leaves its own volumetric smoke trail instead of the game's thin line. Reddish just behind the nozzle (nitric acid fuel), light brown after a few seconds and greyish brown as it ages; it grows, rolls, drifts with the wind and starts forming about 10 m behind the nozzle. The flame is a cone of hot gas starting exactly at the nozzle (white core, yellow to orange envelope, turbulence and flicker) and always points backwards along the flight path. Same tone curve and haze as the explosions, also in VR. Tunable in `NightzimBoom.ini`, section `[rastro]`: `scud` (0/1), `densidade`, `tamanho`, `bocal_m`. Needs the updated hook and `nzboom.dll` from this package.
- **Nuclear dust skirt (v1.2.7):** new volume `nuke_dust.nzv` (3D simulation): a ring of raised dust that grows with the shock wave and slows down, up to about 4 km radius and about 100 m tall at the front, low in the middle. About 250 MB of VRAM with BC7 compression (about 1 GB without).
- **Nuclear explosion (v1.2.7):** the dust is always drawn under the mushroom (it used to cover it); the mushroom smoke is brown-red instead of looking like white vapour; the old dust wall of the shock wave was removed; camera shake and air refraction when the shock arrives (`[nuke]` `tranco`, `choque_refracao`).
- **SCUD = nuclear (v1.2.6):** the SCUD missile now detonates with the 11 kt nuclear explosion. Installer: Options > "SCUD missile explosion" (Nuke 11kt (test) / Same as JDAM), on by default. The JDAM keeps its own explosion.
- **Nuclear flash and lens effects (v1.2.6):** yellow-white flash over the whole screen at detonation, then the fireball glare (blinding core, bloom, light rays, horizontal lens streak, colored lens ring and lens ghosts that move and rotate as you turn your head), an orange haze and the view darkened while the eye readapts (about 10 s). Also in VR. Tunable in `NightzimBoom.ini`, section `[nuke]`: `clarao` (strength) and `adaptacao_s` (readaptation time).
- **Nuke sound (v1.2.6):** the nuclear blast is now heard up to 90 km away (other explosions stay at 20 km).
- **Missions can request explosions (v1.2.6):** a mission script can add `"x,y,z,type"` to the global table `nzx` and the mod draws that explosion (for example type 9 = nuclear, 0 = JDAM).
- **Fix (v1.2.5): VR explosions not following the head.** In some sessions (seen with Virtual Desktop) the add-on located the game's camera matrix while still in the menu and locked onto the wrong one, so the explosions did not track head movement. It now learns only with the mission running, accepts only the matrix stored together with the eye projection, and learns again by itself if the game recreates it (add-on v24).
- **Fix (v1.2.5): installer used `Saved Games\DCS.openbeta` on installs that use `Saved Games\DCS`.** The installer now reads the game's `dcs_variant.txt` to pick the right Saved Games folder. Thanks to the r/hoggit user who reported it.
- **New (v1.2.5): ReShade check in the installer.** It warns when ReShade is missing, is older than 6.0, is the build without full add-on support, or did not load the add-on, and says what to do in each case.
- **New (v1.2.5, test): "Nuke 11kt (test)" option for the JDAM.** A third choice in "JDAM explosion" swaps the JDAM for an 11 kt nuclear burst with a mushroom cloud about 1.3 km tall (new fluid simulation, about 240 MB of VRAM with BC7). Still in testing: no screen flash yet and the cloud looks coarse up close.
- **JDAM with the new fire (v1.2.3):** same simulation, size and mushroom as before, but the fireball stays white-hot longer and the flame fades slowly (about 5 s) through the smoke while the mushroom rises, instead of going dark in about 1 s.
- **Rocket New Shader (v1.2.3):** 4 new rocket explosion variations, with their own slider in the VRAM tab (Off, 1 to 4). They are mixed into the rocket draw.
- **New shaders on by default (v1.2.3):** Mk-82 New Shader (6) and Rocket New Shader (4) now come switched on, and the old Mk-82 and rocket explosions come switched off. Change it in the VRAM tab. If a new-shader file is missing, the old explosion is used automatically.
- **Explosion limit (v1.2.3):** new slider in the VRAM tab, 0 to 50 (default 30), for how many explosions may exist at the same time. The limit used to be fixed at 12; when it is reached, the oldest explosion gives way to the new one.
- **Update check (v1.2.2):** the installer now asks GitHub on start whether a newer release exists and shows "You have the latest version" or "New version X available - download" (click opens the releases page). If it cannot reach GitHub it just says so; nothing else changes.
- **VRAM tab uses sliders (v1.2.2):** each explosion type is now a slider (Off, 1 to 5) instead of buttons.
- **New: "Mk-82 New Shader" (v1.2.2):** a 7th line in the VRAM tab with 6 new Mk-82 variations (faster, more violent, more chaotic start). They are drawn in the same random draw as the normal Mk-82 variations. Off by default (add-on v21). Needs the `mk82n*.nzv` files that come in the package.
- **Fix (v1.2.1): game closing at start on some installs.** When another program in the DCS process created a tiny 64x64 test window before the game's own screen (seen on a Steam install), the add-on initialised on it and was then unloaded by ReShade while still loading the explosion volumes. The add-on now ignores screens smaller than 320x200 and stays in memory until DCS closes. Confirmed working on the developer's PC; thanks to the user who sent the logs.
- **Fix: A-4E-C Mk-77 napalm drew two explosions.** The mod's bomb body opens about 35 m above the ground and releases the napalm itself; the hook counted the body as an air burst on top of the napalm. It now counts only the napalm.
- **VR (OpenXR):** explosions are now drawn inside the headset, once per eye, with each eye's own off-centre projection. Confirmed on Quest 3 through Quest Link. No shock wave refraction in VR yet, and VR uses a lighter ray-march quality.
- **VR fixes:** the add-on no longer shuts itself down when the VR runtime destroys one of its own graphics devices, and the periodic camera search that caused stutter in VR is gone.
- **Targeting pod:** explosions are now also drawn in the targeting pod image (MFD). The add-on finds the pod's own render pass (narrow field of view) and draws the explosion there with the pod camera. Confirmed working in game (F-16C).
- **Night lighting:** with the sun below the horizon the smoke no longer receives daytime sky and ground light (it was showing up light grey at night), distance haze goes dark, and fire and fire-lit smoke get brighter, as seen by a dark-adapted eye. In testing.
- **New installer look:** dark custom interface with in-game screenshots, Install / Options / Log tabs, PT/EN switch, support by PayPal or Pix (QR Code) and Discord / Instagram / GitHub links.
- **Source in the repository:** the add-on, hook and installer sources plus the current test build are now in [`boommod-x/`](boommod-x/).
- **Video memory (VRAM) options in the installer** (Portuguese / English):
  - *Variety* - how many variations of each explosion may go to the GPU: Low (1 per type), Medium (2), High (all).
  - *Load variations only when used* - the first variation of each type loads at start; the others only the first time they are drawn at random.
  - *Compression* - Off, Half the memory (BC5, our own encoder) or One quarter (BC7).
  - The installer shows the estimated VRAM for the chosen settings.
- **Napalm ignites progressively:** instead of the whole elongated strip lighting up at once, the fire now starts at the impact end and runs to the other end in about 0.75 s, along the direction the bomb was flying (r/hoggit feedback).
- **Per-explosion VRAM choice:** the installer's new VRAM tab lets you pick, for each explosion type (JDAM, air burst, rocket, Mk-82, napalm, tank gun), Off or 1 to 5 variations, and adds up the video memory as you choose.
- **Variations:** the add-on now accepts extra volumes per type (`jdam_2.nzv`, `jdam_3.nzv`, ...). The extra volumes themselves are not shipped yet.
- **Dust skirt (preview):** a separate dust layer raised by the shock wave running along the ground, on top of the existing explosions. Offline preview only, not in the mod yet.
- **Blender:** `nightzim_boom` add-on 0.2.0 opens the mod's `.nzv` volumes as animated OpenVDB volumes.

**Português**
- **Opções do instalador enxutas (v1.2.7):** saiu a opção "Nuke 11kt (teste)" da explosão da JDAM (a nuke agora fica só no SCUD), e saíram da aba VRAM as linhas do Foguete antigo e da Mk-82 antiga (as versões Novo Shader substituem).
- **Correção no instalador (v1.2.7): atualizações não terminavam.** Ao atualizar por cima de uma instalação no mesmo disco, a cópia de um arquivo por cima do antigo falhava e a atualização parava no meio (e as linhas de erro com acento sumiam do registro). Agora os arquivos são trocados pelas funções de cópia do Windows e o registro é gravado em UTF-8.
- **Notas de atualização no instalador (v1.2.7):** nova aba "NOVIDADES" com estas notas.
- **Rastro de fumaça do SCUD (v1.2.7):** o SCUD agora deixa um rastro de fumaça volumétrico próprio no lugar da linha fina do jogo. Avermelhado logo atrás do bocal (combustível de ácido nítrico), marrom-claro depois de alguns segundos e pardo-acinzentado ao envelhecer; engorda, forma rolos, é levado pelo vento e começa a se formar uns 10 m atrás do bocal. A chama é um cone de gás quente que nasce exatamente no bocal (núcleo branco, envelope amarelo a laranja, turbulência e cintilação) e aponta sempre para trás do voo. Mesma curva de tom e névoa das explosões, também no VR. Ajustável no `NightzimBoom.ini`, seção `[rastro]`: `scud` (0/1), `densidade`, `tamanho`, `bocal_m`. Precisa do hook e do `nzboom.dll` novos deste pacote.
- **Saia de poeira nuclear (v1.2.7):** novo volume `nuke_dust.nzv` (simulação 3D): um anel de poeira levantada que cresce com a onda de choque e vai freando, até uns 4 km de raio e uns 100 m de altura na frente, baixo no meio. Cerca de 250 MB de VRAM com compressão BC7 (cerca de 1 GB sem).
- **Explosão nuclear (v1.2.7):** a poeira é desenhada sempre por baixo do cogumelo (antes cobria); a fumaça do cogumelo ficou marrom-avermelhada em vez de parecer vapor branco; saiu a parede de poeira antiga da onda de choque; tranco da câmera e refração do ar na chegada da onda (`[nuke]` `tranco`, `choque_refracao`).
- **SCUD = nuclear (v1.2.6):** o míssil SCUD agora detona com a explosão nuclear de 11 kt. Instalador: Opções > "Explosão do míssil SCUD" (Nuke 11kt (teste) / Igual à JDAM), ligado por padrão. A JDAM continua com a explosão dela.
- **Clarão nuclear e efeitos de lente (v1.2.6):** clarão branco amarelado na tela inteira na detonação; depois o brilho da bola de fogo (núcleo cegante, brilho espalhado, raios de luz, risco horizontal de lente, anel colorido e reflexos de lente que correm e giram quando você mexe a cabeça), uma neblina laranja e a vista escurecida se readaptando (cerca de 10 s). Funciona também no VR. Ajustável no `NightzimBoom.ini`, seção `[nuke]`: `clarao` (força) e `adaptacao_s` (tempo de readaptação).
- **Som da nuke (v1.2.6):** o estrondo nuclear agora é ouvido até 90 km de distância (as outras explosões continuam em 20 km).
- **Missões podem pedir explosões (v1.2.6):** um script de missão pode acrescentar `"x,y,z,tipo"` à tabela global `nzx` e o mod desenha a explosão (por exemplo tipo 9 = nuclear, 0 = JDAM).
- **Correção (v1.2.5): explosões no VR não acompanhavam a cabeça.** Em algumas sessões (visto com o Virtual Desktop) o add-on localizava a matriz de câmera do jogo ainda no menu e travava na matriz errada, e as explosões não acompanhavam o movimento da cabeça. Agora ele só aprende com a missão rodando, só aceita a matriz guardada junto com a projeção do olho e aprende de novo sozinho se o jogo a recriar (add-on v24).
- **Correção (v1.2.5): o instalador usava `Saved Games\DCS.openbeta` em instalações que usam `Saved Games\DCS`.** O instalador agora lê o `dcs_variant.txt` do jogo para escolher a pasta certa do Saved Games. Obrigado ao usuário do r/hoggit que avisou.
- **Novo (v1.2.5): verificação do ReShade no instalador.** Ele avisa quando o ReShade está ausente, é anterior ao 6.0, é a versão sem suporte completo a add-ons ou não carregou o add-on, e diz o que fazer em cada caso.
- **Novo (v1.2.5, em teste): opção "Nuke 11kt (teste)" para a JDAM.** Uma terceira escolha em "Explosão JDAM" troca a JDAM por uma explosão nuclear de 11 kt com cogumelo de cerca de 1,3 km de altura (simulação de fluido nova, cerca de 240 MB de VRAM com BC7). Ainda em teste: sem clarão na tela e a nuvem fica grosseira vista de perto.
- **JDAM com o fogo novo (v1.2.3):** mesma simulação, tamanho e cogumelo de antes, mas a bola de fogo fica branca por mais tempo e a chama some aos poucos (cerca de 5 s) por dentro da fumaça enquanto o cogumelo sobe, em vez de escurecer em cerca de 1 s.
- **Foguete Novo Shader (v1.2.3):** 4 variações novas da explosão de foguete, com barra própria na aba VRAM (Não, 1 a 4). Elas entram no sorteio do foguete.
- **Novos shaders ligados por padrão (v1.2.3):** o Mk-82 Novo Shader (6) e o Foguete Novo Shader (4) agora vêm ligados, e as explosões antigas da Mk-82 e do foguete vêm desligadas. Dá para mudar na aba VRAM. Se faltar o arquivo do novo shader, a explosão antiga é usada automaticamente.
- **Limite de explosões (v1.2.3):** nova barra na aba VRAM, de 0 a 50 (padrão 30), para quantas explosões podem existir ao mesmo tempo. O limite era fixo em 12; quando ele é atingido, a explosão mais antiga dá lugar à nova.
- **Verificação de atualização (v1.2.2):** o instalador agora consulta o GitHub ao abrir e mostra "Você está na última versão" ou "Nova versão X disponível - baixar" (o clique abre a página de releases). Se não conseguir falar com o GitHub, apenas avisa; nada mais muda.
- **Aba VRAM com barras deslizantes (v1.2.2):** cada tipo de explosão virou uma barra (Não, 1 a 5) no lugar dos botões.
- **Novo: "Mk-82 Novo Shader" (v1.2.2):** 7ª linha na aba VRAM com 6 novas variações da Mk-82 (mais rápidas, mais violentas e com início mais caótico), sorteadas junto com as variações normais da Mk-82. Desligada por padrão (add-on v21). Usa os arquivos `mk82n*.nzv` que vêm no pacote.
- **Correção (v1.2.1): jogo fechando ao abrir em algumas instalações.** Quando outro programa dentro do processo do DCS criava uma janela de teste de 64x64 antes da tela do jogo (visto numa instalação Steam), o add-on inicializava nela e depois era descarregado pelo ReShade ainda carregando os volumes das explosões. Agora o add-on ignora telas menores que 320x200 e fica na memória até o DCS fechar. Confirmado no PC do desenvolvedor; obrigado ao usuário que mandou os logs.
- **Correção: o napalm Mk-77 do A-4E-C desenhava duas explosões.** O corpo da bomba do mod se abre a cerca de 35 m do chão e solta o napalm; o hook contava o corpo como explosão aérea além do napalm. Agora conta só o napalm.
- **VR (OpenXR):** as explosões agora são desenhadas dentro do óculos, uma vez por olho, com a projeção descentrada de cada olho. Confirmado no Quest 3 pelo Quest Link. Ainda sem a refração da onda de choque no VR, e o VR usa uma qualidade de traçado mais leve.
- **Correções no VR:** o add-on não se desliga mais quando o runtime de VR destrói um dispositivo gráfico próprio, e a busca periódica da câmera que causava engasgos no VR acabou.
- **Pod de mira (targeting pod):** as explosões passam a ser desenhadas também na imagem do pod (MFD). O add-on acha a passada própria do pod (campo de visão estreito) e desenha a explosão ali com a câmera do pod. Confirmado no jogo (F-16C).
- **Iluminação noturna:** com o sol abaixo do horizonte a fumaça deixa de receber a luz diurna do céu e do chão (ela aparecia cinza-clara à noite), a névoa de distância escurece, e o fogo e a fumaça iluminada por ele ficam mais fortes, como para um olho adaptado ao escuro. Em teste.
- **Instalador com visual novo:** interface escura própria com capturas do jogo, abas Instalação / Opções / Registro, troca PT/EN, apoio por PayPal ou Pix (QR Code) e links de Discord / Instagram / GitHub.
- **Código no repositório:** os fontes do add-on, do hook e do instalador, mais a build de teste atual, estão em [`boommod-x/`](boommod-x/).
- **Opções de memória de vídeo (VRAM) no instalador** (português / inglês):
  - *Variedade* - quantas variações de cada explosão podem ir para a placa: Baixa (1 por tipo), Média (2), Alta (todas).
  - *Carregar variações só quando usadas* - a primeira de cada tipo carrega na abertura; as outras só na primeira vez que forem sorteadas.
  - *Compressão* - Desligada, Metade da memória (BC5, codificador próprio) ou Um quarto (BC7).
  - O instalador mostra a VRAM estimada para as opções escolhidas.
- **Napalm acende aos poucos:** em vez de a faixa inteira acender de uma vez, o fogo começa na ponta do impacto e corre até a outra em cerca de 0,75 s, na direção em que a bomba vinha (sugestão do r/hoggit).
- **VRAM por explosão:** a nova aba VRAM do instalador deixa escolher, para cada tipo (JDAM, aérea, foguete, Mk-82, napalm, tiro de tanque), Não ou 1 a 5 variações, e vai somando a memória de vídeo conforme a escolha.
- **Variações:** o add-on aceita volumes extras por tipo (`jdam_2.nzv`, `jdam_3.nzv`, ...). Os volumes extras ainda não acompanham o pacote.
- **Saia de poeira (prévia):** camada separada de poeira levantada pela onda de choque rente ao chão, por cima das explosões existentes. Só em prévia offline, ainda fora do mod.
- **Blender:** add-on `nightzim_boom` 0.2.0 abre os volumes `.nzv` do mod como volumes OpenVDB animados.

## X v1.0b - 2026-11-13

- Realistic colour shader (early white-yellow fireball, deep red late fire, bluish soot, lighter dust) with a Classic option.
- New JDAM volume matched to real footage; the classic one stays selectable (installer or F10 menu).
- Installer with activate / disable / uninstall, backups with SHA-256 check, options, Portuguese and English.
- Tank gun muzzle blast oriented to the barrel. Stock explosions replaced by blank models (optional).

*Shader de cores realistas (com opção Clássica); JDAM nova baseada em vídeo real, com a clássica selecionável; instalador com ativar / desativar / desinstalar, backup, opções, em português e inglês; tiro de tanque orientado pelo cano; explosões do jogo trocadas por modelos em branco (opcional).*

## X v1.0 - 2026-11-13

- First volumetric release: ReShade add-on that ray-marches simulated explosion volumes on top of the game, with scene depth, sun self-shadowing, wind drift and shock waves.
- Triggered by real bombs, rockets, missiles and tank shells, in single player and multiplayer.

*Primeira versão volumétrica: add-on do ReShade que desenha por cima do jogo os volumes simulados, com profundidade da cena, sombra do sol, deriva com o vento e ondas de choque; acionada por bombas, foguetes, mísseis e tiros de tanque reais, em single player e multiplayer.*

## Acknowledgements / Agradecimentos

- **Richard Geldreich, Jr.** - [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo) (MIT / public domain), the BC7 encoder used by the "One quarter" VRAM compression option. Thank you! *O compressor BC7 usado na opção "Um quarto" de compressão de VRAM. Obrigado!*
- **crosire and the ReShade contributors** - [ReShade](https://reshade.me) and its add-on API, which the volumetric renderer is built on. *O ReShade e a API de add-ons, base do renderizador volumétrico.*
