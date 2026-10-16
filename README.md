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

Marco 2 — front-end original portado (`src/game/`, 70 funções do `HoH.exe`, cada uma com o
endereço original no comentário): `game_main` e o laço de passo fixo (30 ticks/s), abertura,
tela-título, menus (opções, controles, som), fundo de água, transições, fontes em bitmap e
recoloração HSL, efeitos sonoros por categoria, música, `Config.dat` (mesmo formato do original).
As globais do jogo são geradas com os valores iniciais do `.data` do exe
(`../HeadOverHeels/re/port/gen_globals.py`). O motor isométrico (salas e jogabilidade) ainda é
stub (`src/game/pending_stubs.cpp`): a cena da sala na abertura fica vazia e "PLAY THE GAME"
ainda não entra no jogo.

Próximo: portar o motor isométrico e a jogabilidade; builds Windows x64, aarch64 (R36S) e Web.

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
`scripts/check-syntax.sh [arquivos]`.

Executáveis: `hoh` (o jogo) e `hoh-viewer` (visualizador de sprites/sons do marco 1).

Teste automático (variáveis de ambiente): `HOH_KEYS="3000:ENTER;6500:DOWN"` aperta teclas,
`HOH_SHOTS="5000:tela.bmp"` salva o quadro, `HOH_AUTOQUIT_MS=12000` encerra.

## Rodar

`hoh [--data PASTA] [--fullscreen]`. Sem `--data`, procura os dados na pasta do executável,
na pasta atual e em `data/` ao lado do executável (ou na variável `HOH_DATA`).

- Alt+Enter ou F11: alterna tela cheia.
- Variáveis: `HOH_FULLSCREEN=0/1`, `HOH_HANDHELD=1` (força modo console portátil),
  `HOH_AUDIO_BUFFER=1024` (tamanho do buffer de áudio).

No R36S (SDL com KMSDRM) o modo portátil é detectado sozinho: tela cheia 640×480, sem cursor.

## Licença dos dados

O jogo original é freeware e só pode ser redistribuído sem alterações. Este repositório
contém apenas código próprio; os dados vêm da cópia do usuário.
