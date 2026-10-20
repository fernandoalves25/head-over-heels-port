# hoh-port

Port nativo do **Head Over Heels** (remake da Retrospec, 2003) para Windows 10/11 x64,
Linux x86_64 e Linux aarch64 (R36S), em C++17 + SDL2.

O port **não inclui nenhum arquivo do jogo**: ele lê os dados originais do usuário
(`HoHOriginal.dat`, `Sound/Sound.dat`, `Sound/Theme.ogg`, `Sound/Game.ogg`).
A engenharia reversa (formatos, mapa do executável, decompilação) está em
`../HeadOverHeels/re/` (`FORMATOS.md`, `PLANO_PORT.md`).

## Estado

Marco 1 — fundação:

- `src/compat/`: o subconjunto do Allegro 4.0 que o `HoH.exe` usa (67 funções), com a mesma
  semântica, para que a lógica decompilada seja portada quase linha a linha.
  - núcleo sem dependências: bitmaps RGB565, blit/masked_blit/stretch_sprite, FBlend
    (trans/add), fonte de depuração, packfiles (cifra `19h28o37h` + LZSS, leitura e escrita
    compatíveis com o jogo original), datafiles;
  - SDL2: janela/tela cheia com escala 4:3, teclado com a numeração `KEY_*` do Allegro 4.0,
    controles (`joy[]` no layout do Allegro), timers em thread como no Allegro, mixer de
    vozes e música Ogg (vorbisfile) no lugar do alogg.
- `src/data/`: leitor do `HoHOriginal.dat` (todos os gráficos) e das 276 salas.
- `src/main.cpp`: visualizador (tela-título, música, sprites, sons) para validar tudo acima.
- `tests/datatest.cpp`: confere o leitor contra os arquivos originais.

Verificado no Linux x86_64 (WSL Ubuntu 24.04, SDL 2.30, vorbisfile 1.3.7): build sem avisos,
`hoh-datatest` OK, `scripts/smoke-test.sh` (sem janela, capturas em `build/smoke/`) OK e
janela real via WSLg a ~60 FPS.

Marco 2 — front-end original portado (`src/game/`): `game_main` e o laço de passo fixo
(30 ticks/s), abertura, tela-título, menus, fundo de água, transições, fontes em bitmap,
recoloração HSL, efeitos sonoros, música, `Config.dat` compatível com o original.

Marco 3 — **motor isométrico e jogabilidade portados**: as 308 funções de jogo do `HoH.exe`
estão em C++ (cada uma com o endereço original no comentário), em módulos: `engine_data`
(blocos, pisos, paredes, mundos), `room_load` (`load_room` lendo o blob original), `room_render`
(projeção isométrica, oclusão, sombras, iluminação), `physics` (colisão, gravidade, empurrar,
esteiras), `controllers` (os 22 comportamentos dos objetos), `gameplay` (`update_state_game`,
troca de sala, Head/Heels, itens) e `screens` (HUD, mapa dos planetas, pausa, fim de jogo,
saves `Save%i.dat` compatíveis). Tipos em `engine_types.h` com o layout original verificado
(offsets em 32 bits). O envio de recordes ao servidor da Retrospec (extinto) não abre rede.

Próximo: jogar o jogo inteiro e corrigir diferenças; build Web (WebAssembly).

## Compilar (Linux / WSL)

```bash
sudo apt install -y cmake ninja-build libsdl2-dev libvorbis-dev
cmake -S . -B build/linux -DHOH_DATA_DIR=/caminho/para/HeadOverHeels
cmake --build build/linux -j
ctest --test-dir build/linux        # valida o leitor contra os dados originais
./build/linux/hoh --data /caminho/para/HeadOverHeels
```

Só o núcleo e o teste (sem SDL): `-DHOH_BUILD_GAME=OFF`.

Atalhos: `scripts/build-linux.sh [pasta_do_jogo]` (configura, compila e roda o teste),
`scripts/game-smoke.sh [pasta_do_jogo]` (roda o jogo sem janela com teclas roteirizadas e salva
capturas em `build/game-smoke/`), `scripts/smoke-test.sh` (idem para o visualizador) e
`scripts/check-syntax.sh [arquivos]`, `scripts/play-test.sh` (NEW GAME → primeira sala → anda/pula) e
`scripts/monkey-test.sh <dados> <semente> <n> game` (ASan/UBSan, teclas aleatórias jogando).

Executáveis: `hoh` (o jogo) e `hoh-viewer` (visualizador de sprites/sons do marco 1).

Teste automático (variáveis de ambiente): `HOH_KEYS="3000:ENTER;6500:DOWN"` aperta teclas,
`HOH_SHOTS="5000:tela.bmp"` salva o quadro, `HOH_AUTOQUIT_MS=12000` encerra.

## Compilar para Windows x64 (no WSL, com MinGW-w64)

```bash
sudo apt install -y mingw-w64 ninja-build cmake
scripts/build-windows.sh            # monta SDL2/libogg/libvorbis e gera dist/windows/
```

O script espera em `/mnt/f/headover/tools/deps/dl/` (ou na pasta passada como argumento):
`SDL2-devel-2.32.10-mingw.tar.gz` (github.com/libsdl-org/SDL), `libogg-1.3.5.tar.gz` e
`libvorbis-1.3.7.tar.gz` (downloads.xiph.org, com os `SHA256SUMS` da xiph). O `hoh.exe` sai com
o runtime do GCC embutido e só precisa do `SDL2.dll` ao lado. `dist/windows/` também traz
`Jogar.bat` e `LEIA-ME.txt`.

## Compilar para o R36S (aarch64, ArkOS) — no WSL, com Zig

```bash
scripts/build-r36s.sh               # gera dist/r36s/ports/ e dist/hoh-r36s.zip
```

Usa o Zig 0.14.1 (`~/tools/zig-x86_64-linux-0.14.1/zig`, ziglang.org) mirando
`aarch64-linux-gnu.2.28`: o binário exige no máximo GLIBC_2.17, tem a libc++ embutida e usa o
`libSDL2-2.0.so.0` do próprio aparelho (KMSDRM). libogg/libvorbis vão estáticos. Para linkar, o
script gera um stub do SDL2 com o mesmo soname a partir dos headers do WSL.

Instalar no aparelho: extraia `hoh-r36s.zip` na pasta `roms/` do cartão (fica
`roms/ports/Head Over Heels (Nativo).sh` e `roms/ports/hohnative/`), copie `HoHOriginal.dat` e a
pasta `Sound/` para `roms/ports/hohnative/gamedata/` e abra em Ports. Controle via gptokeyb
(`hohnative/hoh.gptk`, mesmo mapeamento do port Box64); SELECT+START sai. Log em
`roms/ports/hohnative/log.txt`. No aparelho o FRAME RATE vem em AUTO, como no PC: 60 quadros por
segundo (testado: fica liso), caindo para 30 sozinho se não acompanhar; 30 FPS em GAME OPTIONS →
FRAME RATE deixa como o original.

## Qualidade de imagem (PC)

O jogo desenha em 640×480; o port amplia para a janela/tela inteira mantendo 4:3
(`src/compat/video_scaler.cpp`). **F10** alterna e o modo fica salvo em `port.cfg`
(ou `HOH_SCALER=...`):

| Modo | Resultado |
|---|---|
| `smooth3x` (padrão) | bordas suavizadas (Scale3x/AdvMAME3x) + filtro |
| `smooth2x` | idem com Scale2x/EPX |
| `sharp` | pixels nítidos e uniformes em qualquer tamanho (pré-escala inteira + filtro) |
| `nearest` | pixels crus esticados |
| `integer` | só múltiplos inteiros, com tarjas |

A janela abre com ~90% da altura da tela. No R36S (tela 640×480) a imagem é sempre 1:1.
Teste sem janela: `scripts/scaler-test.sh` (captura a saída ampliada de cada modo).

## Movimento suave

A lógica roda a 30 passos por segundo (60 nos menus), como no original. No jogo, com o movimento
suave, cada quadro mostra as posições interpoladas entre o passo anterior e o atual (a imagem anda
um passo atrás da lógica); depois do desenho tudo volta ao estado real, inclusive a semente do
`rand()`, então a jogabilidade não muda.

Os quadros saem na taxa do monitor dividida por um inteiro, para cada quadro ficar o mesmo tempo na
tela: num monitor de 120 Hz, 120 ou 60 por segundo; num de 144 Hz, 144 ou 72. Escolha em
GAME OPTIONS → FRAME RATE (item do port, antes do BACK; esquerda/direita) ou com **F9**; fica salvo
em `port.cfg` como `fps = ...` (ou `HOH_FPS=auto|120|60|off`):

| Modo | Resultado |
|---|---|
| `auto` (padrão) | taxa do monitor; se o PC perder mais de 10% dos retraços, cai para a metade e tenta voltar depois |
| `120`, `60`… | fixo (arredondado para um divisor da taxa do monitor) |
| `off` | só os 30 quadros do original |

Com vsync, o present espera o retraço e, no modo metade, o mesmo quadro é apresentado duas vezes.
Se o vsync não segurar (driver forçando desligado, renderer por software), o port percebe que os
quadros não caem na grade do retraço e passa a segurar o ritmo pelo relógio. `HOH_SHOW_FPS=1` mostra
o contador; `HOH_PROFILE=arquivo` grava por segundo o ritmo (taxa do monitor, alvo, vsync,
retraços perdidos, tempo de trabalho por quadro) e os tempos do laço, com o pior quadro do
segundo; `HOH_REFRESH=N` finge um monitor de N Hz (testes).

## Gráficos HD (PC)

Pacotes de sprites ampliados trocam os gráficos do jogo por versões HD, sem mudar a jogabilidade,
em todas as telas: abertura, tela-título, menus, mapa dos planetas, jogo, pausa e resultado.
O jogo continua desenhando o quadro em 640×480 exatamente como antes; cada desenho é gravado numa
lista (`src/compat/hd_core.cpp`) e, no present, a lista é refeita na GPU (`hd_render.cpp`) com as
imagens do pacote, na resolução nativa dele (2560×1920 com pacotes 4×), e só então reduzida para a
janela. **F8** alterna desligado → cada pacote; fica salvo em `port.cfg` (`hd = <pacote>|off`, ou
`HOH_HD=...`).

- Pacotes: `<pasta dos dados>/hd/<pacote>/<nome>.qoi` (ou `HOH_HD_DIR`), um arquivo por imagem do
  `HoHOriginal.dat`, com os nomes do export de `re/tools/hohfmt.py` (`block000_d0_f00_a`,
  `mobile000_f00`, `named_000_Dungeon_f00`, `world0_main`, `image00_48x29`...). Imagem que falta
  usa a original, sem filtro.
- Pisos e paredes são recortes das imagens do arquivo (ladrilhos de 16 px, células de 8×12): o port
  roda o mesmo recorte sobre imagens de coordenadas para saber de onde veio cada pixel e monta o
  ladrilho HD com os blocos correspondentes (`src/game/hd_assets.cpp`).
- Objetos: o jogo não ordena por profundidade, ele recorta cada objeto onde outro está na frente.
  O HD desenha os quadros inteiros na ordem dada pelos próprios recortes.
- Telas fora do jogo: os bitmaps de trabalho (painel do menu, linha de texto, logo, copyright,
  letreiro, fontes tingidas, `g_buffer2` com o quadro guardado para a pausa e as transições) têm
  lista própria, que começa num `clear_to_color` ou num desenho que cobre o bitmap inteiro. Quando
  um deles é desenhado em outro, os desenhos dele entram na lista de destino (se o resultado é o
  mesmo) ou ele vira uma textura própria na GPU (linha do menu esticada, painel em fade, quadro do
  jogo escurecido). O tingimento HSL das fontes (`tint_bitmap`), a silhueta das sombras e o cinza
  são transformações aplicadas aos texels do pacote. A água dos menus entra como cópia 1x do quadro
  (com filtro); a transição em colunas copia o quadro antigo uma vez por passo.
- No R36S (e no PC sem pacotes) nada é gravado.
- Gerar os pacotes (scripts em `HeadOverHeels/re/remaster`, fora deste repositório porque usam os
  gráficos do jogo): `hohfmt.py sprites` → `upscale.py` (Real-ESRGAN ncnn-vulkan, 4×, mantendo a
  transparência) → `make_qoi.py`. Os pacotes são derivados do jogo: não redistribuir.
- Testes: `scripts/hd-tour.sh` desenha as 276 salas e confere se a lista refaz o quadro 1x pixel a
  pixel (hoje: 0 diferenças); `scripts/hd-states.sh` percorre com teclas roteirizadas a abertura, o
  título, os menus, o mapa, o jogo, a pausa e o resultado e exige 0 pixels diferentes em todo
  estado (hoje: 0). `HOH_HD_VERIFY=arquivo` faz a mesma conta durante o jogo, por estado, a cada
  30 quadros (`HOH_HD_VERIFY_EVERY=n`); com `HOH_HD_VERIFY_DEEP=1` também confere cada lista de
  origem quando ela é usada. `HOH_TEST_GAMEOVER_MS=t` leva o jogo à tela de resultado no instante t
  (para os testes).

## Modo responsivo (PC)

A imagem acompanha a proporção da janela, em tempo real: o lado menor fica fixo (480 linhas de
altura em tela larga, 640 colunas de largura em pé) e o outro cresce com a janela — 16:9 dá
854×480, 21:9 dá 1120×480, em pé até 640×854; em 4:3 é o 640×480 de sempre. **F7** alterna
responsivo/4:3 (`port.cfg`: `responsive = 1|0`, ou `HOH_RESPONSIVE`). No R36S fica desligado.

O jogo continua desenhando o quadro de 640×480 (o "miolo") como no original, e a lógica não muda
(`src/game/responsive.cpp`):
- No jogo, a sala e o HUD são desenhados direto na tela larga: a sala no centro, e a câmera usa a
  largura extra (`room_init_scroll` soma a margem às extensões de vista), então salas que rolavam
  em 4:3 aparecem inteiras. A moldura do HUD contorna a esquina e desce até as bordas (faixas da
  ponta dos painéis). O miolo é copiado de volta para `g_backbuffer`, que transições e pausa usam;
  essas telas escurecem a tela larga do mesmo jeito. Em pé, sala e HUD ficam juntos na base.
- Abertura, menus, mapa e resultado ficam centrados, com um fundo desfocado e escurecido do próprio
  quadro em volta.
- A sala é desenhada uma vez por quadro, como antes (o desenho usa `rand()` nas salas escuras).
- Funciona com o renderizador HD (a tela larga também tem lista). Teste:
  `scripts/responsive-test.sh` roda 16:9, 21:9, em pé e 4:3 e confere o HD contra o 1x em cada
  quadro (0 diferenças).

## Idiomas

GAME OPTIONS → LANGUAGE (esquerda/direita) troca na hora entre inglês (padrão, o texto original),
português, espanhol e chinês; fica salvo em `port.cfg` (`lang = en|pt|es|zh`, ou `HOH_LANG`). Vale
no PC e no R36S.

- Os textos do jogo continuam em inglês nas tabelas do exe; a tradução é feita na hora de compor
  cada tela (`tr`/`tr_menu` em `src/game/lang.cpp`, uma tabela só com as quatro línguas). Menus,
  opções, controles, slots, abertura, letreiro de créditos, mapa dos planetas e tela de resultado
  são traduzidos; os nomes das teclas ficam como estão. As linhas em fonte pequena da tela de
  resultado (código de recorde) ficam em inglês no chinês (a fonte pequena não tem espaço para
  os ideogramas).
- As fontes em bitmap do jogo só têm maiúsculas sem acento. Cada fonte ganha uma faixa de glifos
  extras (`src/game/lang_glyphs.cpp`): as letras acentuadas são a própria letra da fonte
  (comprimida na vertical para caber o acento) com o acento desenhado no mesmo estilo; ¡ é o ! com
  o ponto em cima; os caracteres chineses vêm da GNU Unifont (16×16), com o degradê e o contorno da
  fonte, e na fonte de 26 linhas são espaçados de modo que a redução dos itens do menu para 18
  linhas não apague nenhum traço. As faixas são tingidas junto com a fonte, e no HD as letras
  usam a fonte ampliada do pacote.
- `tools/make_cjk_glyphs.py <unifont-18.0.01.hex.gz>` regera `src/game/lang_cjk.inc` (só os
  ideogramas usados nas traduções) sempre que o chinês mudar.
- Teste: `scripts/lang-test.sh` troca o idioma pelo menu e confere o `port.cfg`;
  `HOH_LANG=pt|es|zh scripts/hd-states.sh` passa por todas as telas no idioma e confere o HD contra
  o 1x (0 diferenças nos quatro idiomas). `HOH_LANG_DUMP=pasta` grava as faixas de glifos extras.

Os ideogramas são da [GNU Unifont](https://unifoundry.com/unifont/) 18.0.01, © Roman Czyborra,
Paul Hardy e colaboradores, sob a SIL Open Font License 1.1 (ou GNU GPL 2+ com exceção para
fontes).

## Rodar

`hoh [--data PASTA] [--fullscreen]`. Sem `--data`, procura os dados na pasta do executável,
na pasta atual e em `data/` ao lado do executável (ou na variável `HOH_DATA`).

- Alt+Enter ou F11: alterna tela cheia; F10: modo de escala da imagem; F9: quadros por segundo (auto, 120, 60, 30);
  F8: pacote de gráficos HD; F7: tela responsiva / 4:3. Idioma: GAME OPTIONS → LANGUAGE.
- Variáveis: `HOH_FULLSCREEN=0/1`, `HOH_HANDHELD=1` (força modo console portátil),
  `HOH_AUDIO_BUFFER=1024` (tamanho do buffer de áudio).

No R36S (SDL com KMSDRM) o modo portátil é detectado sozinho: tela cheia 640×480, sem cursor.

## Licença dos dados

O jogo original (Head Over Heels, Retrospec 2003) é freeware e só pode ser redistribuído sem
alterações. Este repositório contém o código do port, escrito a partir da análise do HoH.exe, e
em `src/game/globals.cpp` os textos e tabelas iniciais extraídos do HoH.exe. Não contém os
gráficos, os sons nem o `HoHOriginal.dat`: eles vêm da cópia do usuário (assim como os pacotes
HD, gerados localmente a partir dela). Os ideogramas de `src/game/lang_cjk.inc` são da GNU
Unifont (SIL Open Font License 1.1).
