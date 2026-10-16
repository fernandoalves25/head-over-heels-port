// Classes do motor isometrico (sala, objetos, controladores, conjuntos graficos) do HoH.exe.
//
// Layouts reconstruidos do pseudo-C (re/decomp/game.c) e da desmontagem; analises em
// re/analysis/[A-E]_structs.md (onde divergiam, valeu o codigo). Regras:
//
// * Cada campo traz o OFFSET ORIGINAL (32 bits) no comentario: `// +0x24`. O pseudo-C acessa
//   `*(int *)((int)this + 0x24)` / `param_1[9]` / `&DAT_00448d8c + room`; procure o offset aqui.
//   Em 64 bits os offsets reais mudam (ponteiros de 8 bytes); so o comentario e o original. Em
//   32 bits o layout e identico ao do exe (verificado com static_assert de offsetof/sizeof).
// * Structs simples, sem funcoes virtuais. O 1o campo `vtbl` so preserva o layout de 32 bits (o
//   port nao o usa). Chamadas pela vtable do original (`(***(code ***)obj)(1)` = destrutor
//   "scalar deleting") viram `<Classe>_destroy(obj)`, declarada no header do modulo da classe.
// * Heranca: `Object : Sprite` (vtables 0x4302e4/0x4302e8) e `Room : RoomBase` (0x4302f4/0x4302f8).
// * Alocacao: `new Classe()` (zerado) + `<Classe>_ctor(obj, ...)`; liberacao: `<Classe>_destroy`.
//   A Room tem 0x448dd4 bytes (grades 255x255 inline): sempre no heap, nunca na pilha.
// * Estas sao as classes do JOGO. As do leitor de dados (src/data/hohdata.h: BlockDef, SpriteDef,
//   WorldDef, RoomRecord, RoomObject...) sao outra coisa: o modulo engine_data constroi estas a
//   partir daquelas.
//
// Coordenadas de mundo: x, y = ALTURA (0 = chao), z. Direcoes: 0 = -x, 1 = -z, 2 = +x, 3 = +z.
// x == 0x1000 = objeto "fora da sala" (personagem ausente; rosquinha inativa em -0x1000).
// Projecao: sx = origin_x + x - z, sy = origin_y - (x + z)/2 - y.
#pragma once

#include "compat/allegro.h"

#include <cstdint>

namespace hoh {

struct Block;
struct Behaviour;
struct FloorSet;
struct Named;
struct World;
struct Sprite;
struct Object;
struct Controller;
struct Door;
struct Flare;
struct RoomBase;
struct Room;

// ------------------------------------------------------------------ limites (tamanhos dos arrays)
constexpr int kRoomCells = 255;        // grades da sala: [255][255] (indice = borda + celula)
constexpr int kRoomMaxObjects = 255;   // RoomBase::objects
constexpr int kRoomMaxLights = 16;     // RoomBase::light_*
constexpr int kRoomMaxGlows = 16;      // RoomBase::glows
constexpr int kRoomMaxControllers = 256;  // Room::controllers
constexpr int kRoomMaxDoors = 10;      // Room::doors
constexpr int kBlockMaxAnims = 50;     // Block: animacoes (direcoes)
constexpr int kBlockMaxFrames = 20;    // Block: quadros por animacao
constexpr int kSetMaxTiles = 30;       // FloorSet/Named: celulas por lado
constexpr int kSetMaxFrames = 20;      // FloorSet/Named: quadros
constexpr int kBehaviourMaxAttrs = 255;
constexpr int kBehaviourParams = 10;
constexpr int kOutOfRoom = 0x1000;     // coordenada de "fora da sala"

// Atributos na ORDEM DO EXE (tabela 0x431038, g_attr_name_table). O arquivo usa outra ordem:
// g_attr_map[id_do_arquivo] da o indice abaixo (map_attribute_names 0x402e40).
enum Attr {
    ATTR_OBJ_GRAVITY = 0,
    ATTR_OBJ_PUSHABLE = 1,
    ATTR_OBJ_STICKY = 2,
    ATTR_OBJ_ANIMATED = 3,
    ATTR_OBJ_KILLER = 4,
    ATTR_MOBILE_RANDOM = 5,
    ATTR_ROOM_CEILING = 6,
    ATTR_MOBILE_MOVER = 7,
    ATTR_OBJ_SPRING = 8,
    ATTR_OBJ_CONVEYOR = 9,
    ATTR_ROOM_KILLER_FLOOR = 10,
    ATTR_OBJ_TRAP = 11,
    ATTR_OBJ_TELEPORTER = 12,
    ATTR_MOBILE_PUSHER = 13,
    ATTR_OBJ_PICKABLE = 14,
    ATTR_OBJ_EXTRA = 15,
    ATTR_OBJ_FREEZABLE = 16,
    ATTR_OBJ_JOYSTICK = 17,
    ATTR_MOBILE_CONTROLLED = 18,
    ATTR_OBJ_SWITCH = 19,
    ATTR_MOBILE_FOLLOWER = 20,
    ATTR_MOBILE_HIT_AND_TURN = 21,
    ATTR_OBJ_DISAPPEAR = 22,   // "OBJ-Dissapear" no exe
    ATTR_MOBILE_HOOVER = 23,
    ATTR_OBJ_FLICKER = 24,
    ATTR_ROOM_SNOWING = 25,
    ATTR_ROOM_RAIN = 26,
    ATTR_ROOM_DARK = 27,
    ATTR_MOBILE_SLIDER = 28,
    ATTR_MOBILE_GLIDER = 29,
    ATTR_ROOM_UNDER_WATER = 30,
    ATTR_ROOM_VERTICAL_SCROLL = 31,
    ATTR_MOBILE_SOUND = 32,
    ATTR_OBJ_TRANSPARENT = 33,
    ATTR_MOBILE_FOLLOWER_EXTRA = 34,
    ATTR_COUNT = 35
};

// Controller::type (switch de Controller_update 0x410d00).
enum CtrlType {
    CTRL_MOVER = 0,           // MOBILE-Mover
    CTRL_TRAP = 1,            // OBJ-Trap
    CTRL_RANDOM = 3,          // MOBILE-Random
    CTRL_SPRING = 4,          // OBJ-Spring
    CTRL_TELEPORTER = 5,      // OBJ-Teleporter
    CTRL_PUSHER = 6,          // MOBILE-Pusher
    CTRL_EXTRA = 7,           // OBJ-Extra (bonus)
    CTRL_PROJECTILE = 8,      // rosquinha disparada (Room::projectile)
    CTRL_JOYSTICK = 9,        // OBJ-Joystick
    CTRL_SWITCH = 10,         // OBJ-Switch
    CTRL_FOLLOWER = 0xb,      // MOBILE-Follower
    CTRL_HIT_AND_TURN = 0xc,  // MOBILE-Hit And Turn
    CTRL_DISAPPEAR = 0xd,     // OBJ-Dissapear
    CTRL_HOOVER = 0xe,        // MOBILE-Hoover
    CTRL_FLICKER = 0xf,       // OBJ-Flicker
    CTRL_SNOW = 0x10,         // particula de neve (ROOM-Snowing)
    CTRL_RAIN = 0x11,         // gota de chuva (ROOM-Rain)
    CTRL_SLIDER = 0x12,       // MOBILE-Slider
    CTRL_GLIDER = 0x13,       // MOBILE-Glider
    CTRL_BUBBLE = 0x14,       // bolha (ROOM-Under Water)
    CTRL_FOLLOWER_EXTRA = 0x15  // MOBILE-Follower-Extra
};

// Door::type / lados de saida do registro da sala.
enum ExitType {
    EXIT_WALL_ZMAX = 0,  // parede 0 (fundo-esquerda, z = depth_px)
    EXIT_WALL_XMAX = 1,  // parede 1 (fundo-direita, x = width_px)
    EXIT_SIDE_XMIN = 2,  // lado x = 0
    EXIT_SIDE_ZMIN = 3,  // lado z = 0
    EXIT_CEILING = 4,    // sobe (Room::has_up_exit)
    EXIT_FLOOR = 5       // desce (Room::has_down_exit)
};

enum Direction { DIR_NEG_X = 0, DIR_NEG_Z = 1, DIR_POS_X = 2, DIR_POS_Z = 3 };

// ------------------------------------------------------------------ Block (0x21d0, vtable 0x4302e0)
// Definicao grafica + caixa de colisao de um bloco/objeto. g_blocks[i] (ctor 0x40f700, lido por
// load_game_data), g_blocks_alt[i] = copia espelhada (Block_ctor_mirrored: troca size_x/size_z e
// espelha os bitmaps). Tambem criado para cada celula de FloorSet/Named e pecas de World.
// Indices fixos de g_blocks: 0 Head, 1 Heels, 2 Head+Heels, 3 bolsa, 4 bandeja, 5 corneta,
// 6 rosquinha, 7 neve, 8 chuva, 9 coroa, 10 objeto da festa final, 11 bolha.
struct Block {
    const void *vtbl;                                   // +0x0000 vtable 0x4302e0 (so layout)
    BITMAP *image;                                      // +0x0004 1o quadro (= frames[0][0])
    BITMAP *shadow;                                     // +0x0008 1a silhueta (= shadows[0][0]); pode ser NULL
    int size_x;                                         // +0x000c caixa em x (unid. de mundo)
    int size_y;                                         // +0x0010 altura = sprite_h - sprite_w/2
    int size_z;                                         // +0x0014 caixa em z = sprite_w - size_x
    int sprite_w;                                       // +0x0018 largura do bitmap
    int sprite_h;                                       // +0x001c altura do bitmap
    char *name;                                         // +0x0020 strdup (free no dtor); "" nos blocos de bitmap
    int shadow_mode;                                    // +0x0024 1o int do arquivo: 0 nao recebe sombra, 1 so no topo (object_shadow_extend_sides), outro = recebe; nas pecas de World = (j == 0)
    bool animated;                                      // +0x0028 tem animacoes (senao anim_count = 1)
    int anim_count;                                     // +0x002c n de animacoes (direcoes), <= 50
    int frame_count[kBlockMaxAnims];                    // +0x0030 quadros da anim a (com hold e ping-pong)
    int anim_speed[kBlockMaxAnims];                     // +0x00f8 ticks por quadro (default de Sprite_play_anim)
    BITMAP *frames[kBlockMaxAnims][kBlockMaxFrames];    // +0x01c0 imagem A [anim][quadro] (original: +0x1c0 + (a*20+f)*4)
    BITMAP *shadows[kBlockMaxAnims][kBlockMaxFrames];   // +0x1160 imagem B (silhueta) [anim][quadro]
    bool hidden;                                        // +0x2100 copiado para Sprite::hidden (sempre 0 no jogo)
    int owned_count[kBlockMaxAnims];                    // +0x2104 bitmaps realmente carregados por anim (os que o dtor destroi)
    int id;                                             // +0x21cc indice em g_blocks (-1 no ctor)
};

// ------------------------------------------------------------------ Behaviour (0x2be0, vtable 0x4302f0)
// Tabela de atributos (id + 10 parametros). g_block_behaviours[i] = tabela padrao do bloco i;
// load_room cria uma propria quando o objeto do registro nao usa a padrao.
struct Behaviour {
    const void *vtbl;                                   // +0x000 vtable 0x4302f0 (so layout)
    bool inherit;                                       // +0x004 2o byte do registro (usa a tabela do bloco); so lido por read_behaviour_table
    int count;                                          // +0x008 n de atributos (<= 255)
    int attr_id[kBehaviourMaxAttrs];                    // +0x00c id do ARQUIVO (switch usa g_attr_map[attr_id])
    int params[kBehaviourMaxAttrs][kBehaviourParams];   // +0x408 p0..p9 de cada atributo
};

// ------------------------------------------------------------------ FloorSet (0x127c8, vtable 0x430258)
// O "Mobile" do original (metodos Mobile_*, ctor 0x401910; g_mobiles): padrao de PISO. A imagem
// (losango isometrico) e cortada em ladrilhos de 16 px, cada um vira um Block (permite buracos).
struct FloorSet {
    const void *vtbl;                                            // +0x00000 vtable 0x430258 (so layout)
    BITMAP *src_frames[kSetMaxFrames];                           // +0x00004 quadros-fonte lidos do arquivo
    BITMAP *tile_mask;                                           // +0x00054 mascara losango 16 x (depth+8)
    int n;                                                       // +0x00058 ladrilhos por lado = w/16
    BITMAP *tile_bmp[kSetMaxTiles][kSetMaxTiles][kSetMaxFrames]; // +0x0005c [lin][col][quadro]
    Block *tile_block[kSetMaxTiles][kSetMaxTiles];               // +0x1199c Block de cada ladrilho [lin][col]
    int w;                                                       // +0x127ac largura do quadro 0
    int h;                                                       // +0x127b0 altura do quadro 0
    int depth;                                                   // +0x127b4 h - w/2 (espessura; minimo 1)
    bool flip;                                                   // +0x127b8 1o byte do registro: inverte o indice das faces laterais
    bool animated;                                               // +0x127b9 2o byte: piso animado
    int frames_built;                                            // +0x127bc quadros ja anexados aos Blocks
    int speed;                                                   // +0x127c0 velocidade da animacao
    int nframes;                                                 // +0x127c4 n de quadros-fonte
};

// ------------------------------------------------------------------ Named (0x24f18, vtable 0x4302fc)
// Padrao de PAREDE (g_named; ctor 0x417a80): imagem fatiada em celulas 8x12 (paralelogramos).
// Celulas A = parede 0; B = espelhadas, parede 1. Indices [col][lin] invertidos ([0][0] = canto
// inferior direito da imagem).
struct Named {
    const void *vtbl;                                             // +0x00000 vtable 0x4302fc (so layout)
    BITMAP *src_frames[kSetMaxFrames];                            // +0x00004 imagens fonte (uma por quadro)
    BITMAP *cell_mask;                                            // +0x00054 mascara 8x12 da celula
    int cols;                                                     // +0x00058 colunas = w/8
    int rows;                                                     // +0x0005c linhas = (h - w/2)/8
    BITMAP *cells_a[kSetMaxTiles][kSetMaxTiles][kSetMaxFrames];   // +0x00060 [col][lin][quadro] (parede 0)
    BITMAP *cells_b[kSetMaxTiles][kSetMaxTiles][kSetMaxFrames];   // +0x119a0 espelhadas (parede 1)
    Block *blocks_a[kSetMaxTiles][kSetMaxTiles];                  // +0x232e0 Blocks das celulas A
    Block *blocks_b[kSetMaxTiles][kSetMaxTiles];                  // +0x240f0 Blocks das celulas B
    int w;                                                        // +0x24f00 largura da imagem
    int h;                                                        // +0x24f04 altura da imagem
    bool animated;                                                // +0x24f08
    int frames_built;                                             // +0x24f0c quadros ja anexados aos Blocks
    int speed;                                                    // +0x24f10 atraso da animacao
    int nframes;                                                  // +0x24f14 n de imagens fonte
};

// ------------------------------------------------------------------ World (0x70, vtable 0x430238)
// Pecas graficas das portas de um mundo (g_worlds; ctor 0x401000, World_init_door_blocks).
// O original indexa como array plano de Block* a partir de +0x04: door_part[0][0] + k, com
// k = espelhado + 6*lado + 2*peca (Door_build_objects). [j][0] = normal, [j][1] = espelhada.
struct World {
    const void *vtbl;           // +0x00 vtable 0x430238 (so layout)
    Block *door_part[6][2];     // +0x04 pecas P (batentes/verga): j 0..2 lado 0, 3..5 lado 1
    Block *pillar_part[6][2];   // +0x34 pecas Q (pedestal: base, segmento, topo); [j][0].shadow_mode = (j%3 == 0)
    BITMAP *image;              // +0x64 imagem do mundo (desenhada sobre as portas 0/1; registrada em g_bitmap_registry)
    int depth;                  // +0x68 profundidade da porta (1o int do registro; size_x das pecas)
    int unused_6c;              // +0x6c nunca acessado
};

// ------------------------------------------------------------------ Sprite (0x464, vtable 0x4302e4)
// Base de Object: posicao, grafico, animacao, oclusao. So e construida via Object_ctor.
struct Sprite {
    const void *vtbl;          // +0x000 vtable 0x4302e4 (Sprite) / 0x4302e8 (Object) (so layout)
    bool hidden;               // +0x004 = block->hidden: nao desenha nem faz sombra
    bool dirty;                // +0x005 refazer work_bmp/oclusao (Room_update_occlusion)
    int index;                 // +0x008 posicao em room->objects
    Block *block;              // +0x00c grafico + caixa
    int x;                     // +0x010
    int y;                     // +0x014 altura (0 = chao)
    int z;                     // +0x018
    int scr_x;                 // +0x01c x - z - block->size_z (soma-se a room->origin_x)
    int scr_y;                 // +0x020 sprite_h + y + (x+z)/2 (subtrai-se de room->origin_y)
    int shadow_scr_x;          // +0x024 igual a scr_x (sombra no chao, sem y)
    int shadow_scr_y;          // +0x028 shadow->h + (x+z)/2
    BITMAP *work_bmp;          // +0x02c copia do quadro atual com as partes ocluidas apagadas
    BITMAP *cur_img;           // +0x030 quadro atual = block->frames[anim][frame]
    BITMAP *cur_shadow;        // +0x034 silhueta atual = block->shadows[anim][frame]
    BITMAP *recv_shadow;       // +0x038 sombras de outros objetos projetadas sobre este
    Sprite *overlaps[256];     // +0x03c sprites cujo retangulo de tela cruza este
    int overlap_count;         // +0x43c
    bool has_recv_shadow;      // +0x440 recv_shadow tem conteudo neste quadro
    int light;                 // +0x444 luz 0..255 (sala escura); 0xff no init
    bool animating;            // +0x448 Sprite_tick_anim roda (Room_animate)
    int anim;                  // +0x44c animacao/direcao (4 morte/teleporte, 5 ocioso nos personagens)
    int frame;                 // +0x450 quadro atual
    int anim_speed;            // +0x454 ticks por quadro
    int anim_timer;            // +0x458 contador de ticks
    bool transparent;          // +0x45c OBJ-Transparent: desenhado depois, com fblend
    int alpha;                 // +0x460 desenha com 255 - alpha
};

// ------------------------------------------------------------------ Object (0x504, vtable 0x4302e8)
// Objeto instanciado na sala (Room_create_object 0x416ac0 -> Object_ctor 0x410140). Os
// personagens, a rosquinha e as particulas tambem sao Objects.
struct Object : Sprite {
    bool pushable;             // +0x464 pode ser empurrado (cadeia de empurroes)
    bool gravity;              // +0x465 cai (Room_update_physics); desligado durante o pulo
    bool sticky;               // +0x466 anda junto com o objeto embaixo (Room_carry_riders)
    Object *standing_on;       // +0x468 objeto sob este (gravidade)
    bool on_conveyor;          // +0x46c sendo levado por uma esteira
    Object *conveyor;          // +0x470 esteira que o leva
    bool is_conveyor;          // +0x474 OBJ-Conveyor Belt
    int conv_dx;               // +0x478 deslocamento x aplicado a quem esta em cima
    int conv_dz;               // +0x47c deslocamento z
    int door_side;             // +0x480 1..4 nos batentes de porta (desliza o jogador para a abertura)
    Object *contact;           // +0x484 ultimo objeto que colidiu neste tick (Controller_update zera)
    int contact_dx;            // +0x488 movimento de quem bateu
    int contact_dy;            // +0x48c < 0 = algo pressionando de cima
    int contact_dz;            // +0x490
    bool pickable;             // +0x494 OBJ-Pickable
    int record_index;          // +0x498 indice no registro da sala (-1 = dinamico); chave de g_room_taken_ids
    int bonus_pending;         // +0x49c (jogador) tipo de extra recem pego, -1 nenhum
    int bonus_record_index;    // +0x4a0 (jogador) record_index do extra pego
    bool freezable;            // +0x4a4 OBJ-Freezable
    bool freeze_stops_anim;    // +0x4a5
    int freeze_timer;          // +0x4a8 ticks congelado (rosquinha: 9000)
    bool touched_killer;       // +0x4ac tocou objeto killer neste tick
    bool killer;               // +0x4ad OBJ-Killer
    int channel;               // +0x4b0 MOBILE-Controlled: canal de switch/joystick (-1)
    int ctl_anim_mode;         // +0x4b4 MOBILE-Controlled p1 (1 quadro por direcao, 2 anim)
    bool ctl_toggles_anim;     // +0x4b8 MOBILE-Controlled p2: switch liga/desliga a animacao
    Object *joystick_target;   // +0x4bc objeto movido por este joystick
    bool active;               // +0x4c0 ligado (1); 0 = Controller_update nao age
    bool particle;             // +0x4c1 neve/chuva/bolha: sem contato
    bool teleporter;           // +0x4c2 e teletransportador (JUMP em cima teleporta)
    int saved_x;               // +0x4c4 room_save_object_positions / restore
    int saved_y;               // +0x4c8
    int saved_z;               // +0x4cc
    SAMPLE *snd_ambient;       // +0x4d0 som continuo (MOBILE-Sound p0)
    int snd_ambient_cat;       // +0x4d4 categoria (get_sample_category); toca se >= room->sound_mode; -1 no ctor
    SAMPLE *snd_move;          // +0x4d8 som de movimento / zumbido do teleporte
    int snd_move_cat;          // +0x4dc
    SAMPLE *snd_bump;          // +0x4e0 som de batida/virada/interruptor
    int snd_bump_cat;          // +0x4e4
    SAMPLE *snd_event;         // +0x4e8 som de evento (pegar, sumir, morrer)
    int snd_event_cat;         // +0x4ec
    int voice_ambient;         // +0x4f0 voz Allegro de snd_ambient (-1)
    int voice_move;            // +0x4f4 voz de snd_move (-1)
    int voice_bump;            // +0x4f8 voz de snd_bump (-1)
    int bump_last_tick;        // +0x4fc ultimo tick de snd_bump (Room_play_sample_throttled)
    bool trap;                 // +0x500 OBJ-Trap: jogador em cima gera contact_dy = -1
};

// ------------------------------------------------------------------ Controller (0x54, vtable 0x4302ec)
// Comportamento ativo de um objeto (Room_add_ctrl_* -> Controller_ctor_*; Room::controllers).
// Varios campos mudam de papel conforme o tipo (ver D_structs.md §4); o nome e o do uso principal.
struct Controller {
    const void *vtbl;      // +0x00 vtable 0x4302ec (so layout)
    int type;              // +0x04 CtrlType
    Room *room;            // +0x08 sala dona
    Object *obj;           // +0x0c objeto controlado
    int vx;                // +0x10 velocidade x | Teleporter: x destino | Extra: who | Flicker: liga luzes | Follower-Extra: offset de bloco
    int vy;                // +0x14 velocidade escalar (speed) na maioria; vy do Mover; Teleporter: y destino; Flicker: liga glows
    int vz;                // +0x18 velocidade z | Teleporter: z destino
    int ramp_max;          // +0x1c Mover: passos de aceleracao
    bool state;            // +0x20 flag principal: sentido / disparado / ligado / ativo
    bool flag2;            // +0x21 Mover: acelerando; Trap: cai; Pusher: some ao bater; Follower: foge da luz
    int p24;               // +0x24 Mover: pausa nas pontas; Follower: +1 aproxima / -1 foge
    int p28;               // +0x28 espera/intervalo/canal/direcao/anim; Teleporter: coluna da sala destino
    int p2c;               // +0x2c contador/alcance/speed; Teleporter: linha da sala destino (id = col + lin*40)
    int p30;               // +0x30 pausa/contador; Extra: tipo de bonus; Joystick/Follower-Extra: direcao
    int max_x;             // +0x34 Mover: limite max x | Follower: dx ate o alvo | Pusher: anim de movimento | Flicker: ticks aceso
    int max_y;             // +0x38 Mover: limite max y | Follower: face atual | Hoover: y inicial
    int max_z;             // +0x3c Mover: limite max z | Follower: dz ate o alvo
    int min_x;             // +0x40 Mover: limite min x | Flicker: ticks apagado
    int min_y;             // +0x44 Mover: limite min y | Follower: nova direcao | Joystick: 1
    int min_z;             // +0x48 Mover: limite min z | Follower: alternador
    int anim;              // +0x4c modo de anim (1 quadro por direcao, 2 anim por direcao) ou indice de anim
    bool disabled;         // +0x50 terminado/desligado: Controller_update retorna
    bool push;             // +0x51 ao mover empurra objetos pushable
    bool flag3;            // +0x52 inverter ao bater / animar ao mover / modo eixo
};

// ------------------------------------------------------------------ Door (0x54, vtable 0x43023c)
// Saida de sala: porta com graficos de World (tipos 0..3, Door_ctor 0x4014d0) ou saida vertical
// (4 teto, 5 chao, Door_ctor_vertical 0x4014a0). Room::doors.
struct Door {
    const void *vtbl;      // +0x00 vtable 0x43023c (so layout)
    World *world;          // +0x04 pecas graficas (NULL nas saidas 4/5)
    int type;              // +0x08 ExitType
    int x;                 // +0x0c x da porta
    int elevation;         // +0x10 tipos 0..3: n de blocos do pedestal; 4/5: altura do teto (ROOM-Ceiling, 500)
    int z;                 // +0x14 z da porta
    int post_h;            // +0x18 altura do batente = door_part[1][0]->size_y
    int post2_off;         // +0x1c deslocamento do 2o batente
    Room *room;            // +0x20 sala dona
    Object *bottom_obj;    // +0x24 objeto mais baixo da porta
    Object *post_obj;      // +0x28 1o batente (referencia para img_sx/img_sy)
    int n_objects;         // +0x2c objetos de sala criados pela porta
    int img_sx;            // +0x30 posicao de tela da imagem do mundo (tipos 0/1; 0 nos 2/3)
    int img_sy;            // +0x34
    int dest_room;         // +0x38 sala destino (-1 = sem destino)
    int dest_door;         // +0x3c porta de chegada (indice entre as do tipo oposto na sala destino)
    int ent_x;             // +0x40 retangulo da abertura: x
    int base_y;            // +0x44 altura do piso da porta (soma do pedestal)
    int ent_z;             // +0x48 z
    int top_y;             // +0x4c altura da abertura
    int width;             // +0x50 largura da abertura (x nos lados 0/3, z nos 1/2)
};

// ------------------------------------------------------------------ Flare (0x1c, vtable 0x430240)
// Brilho preso a um objeto (o "Glow" de E_structs.md: mesma classe; ctor 0x4016f0, que o Ghidra
// rotulou AFX_DDPDATA). Room_add_glow / Room_draw_glows. O nome `Glow` declarado em globals.h nao
// e definido: use Flare.
struct Flare {
    const void *vtbl;      // +0x00 vtable 0x430240 (so layout)
    Object *obj;           // +0x04 objeto ao qual o brilho esta preso (usa scr_x/scr_y e work_bmp)
    int x;                 // +0x08 ancora x dentro do sprite
    int y;                 // +0x0c ancora y dentro do sprite
    int mode;              // +0x10 0 = cruz de 4 linhas brancas; 1 = imagem aditiva (fblend)
    int alpha;             // +0x14 0xff (nao lido)
    BITMAP *image;         // +0x18 imagem do brilho (modo 1, g_images[i])
};

// ------------------------------------------------------------------ RoomBase (0x448854, vtable 0x4302f4)
// Parte base da sala: objetos, luzes, grades de piso/paredes (ctor 0x412e30, init 0x412e70).
// Grades [255][255] com indice [borda + celula]: piso [B+fx][B+fz] (fx, fz contados a partir do
// canto do FUNDO: x_mundo = (W-1-fx)*tile); parede 0 [B+pos][k] (pos ao longo de x, k = celula
// de 8 px de altura); parede 1 [B+pos][k] (pos ao longo de z). B = border.
struct RoomBase {
    const void *vtbl;                                   // +0x000000 vtable 0x4302f4 / Room 0x4302f8 (so layout)
    Object *objects[kRoomMaxObjects];                   // +0x000004 [0] Head, [1] Heels, [2] rosquinha, depois os do registro
    int n_objects;                                      // +0x000400
    int origin_x;                                       // +0x000404 origem de tela atual (Room_update_camera)
    int origin_y;                                       // +0x000408
    bool dark;                                          // +0x00040c iluminacao por luzes ligada (ROOM-Dark / Room_add_light)
    int ambient;                                        // +0x000410 luz ambiente 0..255 (registro; 100 tipico)
    int n_lights;                                       // +0x000414 <= 16
    bool light_flicker[kRoomMaxLights];                 // +0x000418 luz cintila
    int light_flicker_delta[kRoomMaxLights];            // +0x000428 rand%4-1 sorteado por quadro (Room_light_floor)
    int light_x[kRoomMaxLights];                        // +0x000468
    int light_y[kRoomMaxLights];                        // +0x0004a8
    int light_z[kRoomMaxLights];                        // +0x0004e8
    int light_radius[kRoomMaxLights];                   // +0x000528 brilho = 255*(1-dist/raio); 1 no init
    bool light_on[kRoomMaxLights];                      // +0x000568 [0] Head, [1] Heels (luzes presas aos personagens)
    int unused_578;                                     // +0x000578 nunca acessado (0x578 so aparece como floor_cell[i][j-1])
    Block *floor_cell[kRoomCells][kRoomCells];          // +0x00057c celula de piso (Block de um FloorSet) ou NULL = buraco
    int floor_light[kRoomCells][kRoomCells];            // +0x03fd80 luz por celula de piso
    int floor_height[kRoomCells][kRoomCells];           // +0x07f584 desnivel da celula (sempre 0 nesta versao)
    int width;                                          // +0x0bed88 W em celulas (eixo x)
    int depth;                                          // +0x0bed8c D em celulas (eixo z)
    bool rectangular;                                   // +0x0bed90 1 = sem recortes de planta
    bool owns_extra;                                    // +0x0bed91 o dtor destroi extra[] (nunca ligado)
    int width_px;                                       // +0x0bed94 tile*W (limite de x)
    int depth_px;                                       // +0x0bed98 tile*D (limite de z)
    int border;                                         // +0x0bed9c margem B somada aos indices das grades (Room_set_border)
    int cut_a_x;                                        // +0x0beda0 recortes (Room_apply_shape_cuts): width_px - tile*f0
    int cut_b_x;                                        // +0x0beda4 tile*f1
    int notch_x0;                                       // +0x0beda8 tile*f2
    int notch_x1;                                       // +0x0bedac width_px - tile*f3
    int cut_a_z;                                        // +0x0bedb0 depth_px - tile*f4
    int cut_b_z;                                        // +0x0bedb4 depth_px - tile*f5
    int notch_z;                                        // +0x0bedb8 tile*f6
    int tile;                                           // +0x0bedbc tamanho da celula = sprite_w/2 do ladrilho de piso (8)
    int floor_cell_h;                                   // +0x0bedc0 size_y do ladrilho de piso
    int floor_phase_x;                                  // +0x0bedc4 fase do padrao de piso em x (geo[2])
    int floor_phase_z;                                  // +0x0bedc8 fase em z (geo[3])
    Block *wall0_cell[kRoomCells][kRoomCells];          // +0x0bedcc celulas da parede 0 [pos][k]
    Block *wall1_cell[kRoomCells][kRoomCells];          // +0x0fe5d0 celulas da parede 1
    int wall0_light[kRoomCells][kRoomCells];            // +0x13ddd4
    int wall1_light[kRoomCells][kRoomCells];            // +0x17d5d8
    int wall0_z[kRoomCells][kRoomCells];                // +0x1bcddc z (px) da celula (depth_px ou recuado pelos recortes)
    int wall1_x[kRoomCells][kRoomCells];                // +0x1fc5e0 x (px) da celula (width_px ou recuado)
    int wall0_height[kRoomCells];                       // +0x23bde4 n de celulas de altura por coluna
    int wall1_height[kRoomCells];                       // +0x23c1e0
    int floor_anim_frame[kRoomCells][kRoomCells];       // +0x23c5dc quadro atual da celula animada (inicial = fase)
    int floor_anim_tick[kRoomCells][kRoomCells];        // +0x27bde0 contador ate block->anim_speed
    bool floor_anim_on[kRoomCells][kRoomCells];         // +0x2bb5e4 celula animavel (1 no piso base)
    int wall0_anim_frame[kRoomCells][kRoomCells];       // +0x2cb3e8
    int wall1_anim_frame[kRoomCells][kRoomCells];       // +0x30abec
    int wall0_anim_tick[kRoomCells][kRoomCells];        // +0x34a3f0
    int wall1_anim_tick[kRoomCells][kRoomCells];        // +0x389bf4
    int wall0_anim_on[kRoomCells][kRoomCells];          // +0x3c93f8 (int, nao bool) so as da parede base
    int wall1_anim_on[kRoomCells][kRoomCells];          // +0x408bfc
    Flare *glows[kRoomMaxGlows];                        // +0x448400 brilhos presos a objetos
    int n_glows;                                        // +0x448440
    bool glow_on[kRoomMaxGlows];                        // +0x448444
    int n_extra;                                        // +0x448454 lista generica nunca usada
    void *extra[255];                                   // +0x448458 (classe desconhecida, destrutor virtual)
};

// ------------------------------------------------------------------ Room (0x448dd4, vtable 0x4302f8)
// Sala carregada (load_room 0x403c50 -> Room_ctor 0x415f80). Ha ate duas carregadas ao mesmo
// tempo: g_char_rooms[2] (uma por personagem; slot 1 NULL se estao na mesma sala).
struct Room : RoomBase {
    int moved_count;                                    // +0x448854 objetos movidos no ultimo Room_move_object (exceto particulas)
    Object *active_player;                              // +0x448858 personagem controlado (camera, deslize em quinas, Extra/Follower/Glider)
    Object *light_obj[kRoomMaxLights];                  // +0x44885c objeto ao qual a luz i esta presa (NULL = fixa)
    int light_dx[kRoomMaxLights];                       // +0x44889c deslocamento da luz presa
    int light_dy[kRoomMaxLights];                       // +0x4488dc (update_character_lights reescreve [1])
    int light_dz[kRoomMaxLights];                       // +0x44891c
    int n_controllers;                                  // +0x44895c
    Controller *controllers[kRoomMaxControllers];       // +0x448960
    int n_doors;                                        // +0x448d60
    Door *doors[kRoomMaxDoors];                         // +0x448d64 portas (0..3) e saidas verticais (4, 5)
    int id;                                             // +0x448d8c id na grade 40x40 (col + lin*40)
    bool has_up_exit;                                   // +0x448d90 ha saida tipo 4 (teto)
    int up_exit_y;                                      // +0x448d94 y que dispara a saida pelo teto (ROOM-Ceiling, 500)
    bool has_down_exit;                                 // +0x448d98 ha saida tipo 5; objetos com gravidade em y=0 somem
    Controller *projectile;                             // +0x448d9c controlador tipo 8 da rosquinha (objects[2])
    bool killer_floor;                                  // +0x448da0 ROOM-Killer Floor
    bool snowing;                                       // +0x448da1 ROOM-Snowing
    bool raining;                                       // +0x448da2 ROOM-Rain
    bool underwater;                                    // +0x448da3 ROOM-Under Water (queda 1/tick, bolhas)
    int weather_amount;                                 // +0x448da4 p0 de Snowing/Rain (0 -> 100)
    int base_origin_x;                                  // +0x448da8 origem de tela sem rolagem (registro `a`)
    int base_origin_y;                                  // +0x448dac (registro `b`)
    bool scroll_x;                                      // +0x448db0 sala excede a tela no lado x (room_init_scroll)
    bool scroll_z;                                      // +0x448db1 idem lado z
    bool vertical_scroll;                               // +0x448db2 ROOM-Vertical Scroll
    int player_dir[2];                                  // +0x448db4 ultima direcao pedida por personagem (-1); lida pelo Glider
    bool four_crowns;                                   // +0x448dbc copia de g_four_crowns (Follower foge na sala 444)
    int wall_named;                                     // +0x448dc0 indice do Named das paredes (identifica o mundo: HUD/musica)
    int sfx_volume;                                     // +0x448dc4 copia de g_sfx_volume (0..15, x17 no play_sample)
    int sound_mode;                                     // +0x448dc8 copia de g_sound_mode (categoria minima)
    int sound_tick;                                     // +0x448dcc tick atual (Room_update_object_sounds)
    Block **block_table;                                // +0x448dd0 = g_blocks (troca de bloco por indice)
};

// ------------------------------------------------------------------ estruturas pequenas
// Area de transferencia do codigo de recorde (g_clipboard 0xa5a980, g_clipboard_ptr 0xa5a978).
struct Clipboard {
    char *text;            // +0x0 texto (realloc)
    int unused_4;          // +0x4 nunca acessado
    int status;            // +0x8 1 ok, -1 erro
};

#if UINTPTR_MAX == 0xffffffffu
// Em 32 bits o layout tem de bater com o exe.
static_assert(sizeof(Block) == 0x21d0, "Block");
static_assert(sizeof(Behaviour) == 0x2be0, "Behaviour");
static_assert(sizeof(FloorSet) == 0x127c8, "FloorSet");
static_assert(sizeof(Named) == 0x24f18, "Named");
static_assert(sizeof(World) == 0x70, "World");
static_assert(sizeof(Sprite) == 0x464, "Sprite");
static_assert(sizeof(Object) == 0x504, "Object");
static_assert(sizeof(Controller) == 0x54, "Controller");
static_assert(sizeof(Door) == 0x54, "Door");
static_assert(sizeof(Flare) == 0x1c, "Flare");
static_assert(sizeof(RoomBase) == 0x448854, "RoomBase");
static_assert(sizeof(Room) == 0x448dd4, "Room");
static_assert(sizeof(Clipboard) == 0xc, "Clipboard");
#endif

}  // namespace hoh
