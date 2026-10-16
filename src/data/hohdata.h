// Leitor do HoHOriginal.dat (blob do editor "HoHEdit") e dos registros de sala.
// Ordem de leitura reconstruida do HoH.exe: load_game_data @0x402f30, load_room @0x403c50.
// Nomes de campo provisorios (a, b, v..., f...) ainda nao tem semantica confirmada.
#pragma once

#include "compat/allegro.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hoh {

constexpr const char *kDataPassword = "19h28o37h";  // HoH.exe 0x433060
constexpr int kRoomGrid = 1600;                      // tabela de offsets no fim do blob
constexpr size_t kRoomBlobMax = 900000;              // o original da exit(0) se o blob encher isso

// Tabela de atributos: id (indice em attr_names, mapeado para o switch de 35 casos) + 10 parametros.
struct BehaviourEntry {
    int32_t attr = 0;
    std::array<int32_t, 10> param{};
};

struct FramePair {
    BITMAP *a = nullptr;  // w x h1: o sprite
    BITMAP *b = nullptr;  // w x h2: silhueta da sombra (observado no bloco 0 = Head; a confirmar nos demais)
};

struct BlockDir {
    int32_t nframes = 0, vc8 = 0;
    uint8_t pingpong = 0;  // se != 0 e nframes > 2, o original acrescenta os quadros n-2..1 de volta
    int32_t repeat = 0;
    int32_t w = 0, h1 = 0, h2 = 0;
    std::vector<FramePair> frames;
};

struct BlockDef {  // objetos 0x21d0 em 0xa5b1c8
    int32_t v24 = 0, anim_ref = 0;
    uint8_t multi = 0;
    int32_t ndirs_raw = 0;  // usado so se multi != 0; senao ha 1 direcao
    std::vector<BlockDir> dirs;
    uint8_t beh_a = 0, beh_b = 0;
    bool has_behaviour = false;  // tabela presente se beh_b == 0 || beh_a != 0
    std::vector<BehaviourEntry> behaviour;
};

struct SpriteDef {  // moveis (0x127c8 @0x91e020) e nomeados (0x24f18 @0xa5adcc)
    std::string name;  // so nos nomeados
    uint8_t f0 = 0;    // so nos moveis
    uint8_t directional = 0;
    int32_t v_c0 = 0;
    uint8_t pingpong = 0;
    int32_t repeat = 0;
    int32_t w = 0, h = 0;
    std::vector<BITMAP *> frames;
};

struct WorldDef {  // objetos 0x70 em 0xa5b5c4
    int32_t v0 = 0;
    BITMAP *main = nullptr;
    std::array<std::array<FramePair, 2>, 6> parts{};
};

struct GameData {
    std::vector<std::string> attr_names;
    std::vector<BlockDef> blocks;
    std::vector<SpriteDef> mobiles;
    std::vector<SpriteDef> named;
    std::vector<WorldDef> worlds;
    std::vector<BITMAP *> images;
    int32_t room_count = 0;
    std::vector<uint8_t> room_blob;
    std::array<int32_t, kRoomGrid> room_table{};  // offset no blob, -1 = sala inexistente
    int32_t start_room = -1;

    GameData() = default;
    GameData(const GameData &) = delete;
    GameData &operator=(const GameData &) = delete;
    ~GameData();
};

// Le e decodifica o arquivo (cifra + LZSS). Retorna false com mensagem em `error`.
bool load_game_data(const std::string &path, GameData &out, std::string *error = nullptr);

// ------------------------------------------------------------------ salas
struct RoomItem6 { std::array<int32_t, 6> v{}; };

struct RoomWall {
    int32_t named = -1;  // so o lado 0 e usado pelo original
    std::vector<RoomItem6> items;
};

struct RoomObject {
    int32_t block = 0, x = 0, y = 0, z = 0;
    uint8_t alt = 0;  // usa a copia em g_blocks_alt em vez do bloco base
    int32_t image = -1, image_a = 0, image_b = 0;
    uint8_t f1 = 0;
    int32_t v = 0;
    uint8_t f2 = 0;
    uint8_t beh_a = 0, beh_b = 0;
    bool has_behaviour = false;  // presente se beh_b == 0; senao usa a do bloco
    std::vector<BehaviourEntry> behaviour;
};

struct RoomExit {
    int32_t u = 0;
    std::array<std::array<int32_t, 7>, 2> side{};
};

struct RoomExtra {
    std::array<int32_t, 4> v{};
    uint8_t flag = 0;
};

struct RoomRecord {
    int32_t id = 0, a = 0, b = 0;
    std::array<int32_t, 4> geo{};
    std::array<int32_t, 7> flags7{};
    int32_t v415f50 = 0;
    int32_t floor_mobile = 0;
    std::vector<RoomItem6> floor_items;
    std::array<RoomWall, 2> walls;
    std::vector<RoomObject> objects;
    std::array<RoomExit, 6> exits;
    int32_t v410 = 0;
    std::vector<RoomExtra> extras;
    size_t begin_offset = 0, end_offset = 0;
};

bool parse_room(const GameData &data, int room_id, RoomRecord &out, std::string *error = nullptr);

}  // namespace hoh
