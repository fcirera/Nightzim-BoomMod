# Nightzim BoomMod X - Changelog / Notas de atualização

Every update gets an entry here. *Cada atualização ganha uma entrada aqui.*

## Unreleased - in testing / Em teste (ainda não lançado)

**English**
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
