#include "game/data_bridge.h"

#include "game/globals.h"

#include <cstdio>
#include <cstring>

namespace hoh {

GameData *g_data = nullptr;
std::string g_data_dir;

std::string data_path(const char *original_relative_path) {
    std::string p = g_data_dir;
    for (const char *c = original_relative_path; *c; c++) p += (*c == '\\') ? '/' : *c;
    return p;
}

// 0x00402f30 load_game_data. O original le o HoHOriginal.dat campo a campo e constroi os objetos
// das classes Block/Mobile/Named/World; aqui o leitor do port (src/data/hohdata.cpp, mesma ordem
// de leitura) monta o GameData e esta funcao liga as globais que o resto do jogo usa.
bool load_game_data() {
    packfile_password(kDataPassword);
    g_sound_datafile = load_datafile(data_path("Sound\\Sound.dat").c_str());
    packfile_password(nullptr);

    delete g_data;
    g_data = new GameData();
    std::string err;
    if (!hoh::load_game_data(data_path(g_data_filename), *g_data, &err)) {
        std::snprintf(allegro_error, 256, "%s: %s", g_data_filename, err.c_str());
        return false;
    }
    if (!g_sound_datafile) {
        std::snprintf(allegro_error, 256, "Sound\\Sound.dat nao encontrado em %s", g_data_dir.c_str());
        return false;
    }
    // Globais que o loader original preenchia ao ler o arquivo (contagens e tabelas de nomes de
    // 0xff bytes por entrada); o resto do jogo as consulta direto.
    g_attr_count = static_cast<int32_t>(g_data->attr_names.size());
    for (size_t i = 0; i < g_data->attr_names.size() && (i + 1) * 0xff <= sizeof g_attr_names; i++)
        std::strncpy(&g_attr_names[i * 0xff], g_data->attr_names[i].c_str(), 0xfe);
    g_block_count = static_cast<int32_t>(g_data->blocks.size());
    g_mobile_count = static_cast<int32_t>(g_data->mobiles.size());
    g_named_count = static_cast<int32_t>(g_data->named.size());
    for (size_t i = 0; i < g_data->named.size() && (i + 1) * 0xff <= sizeof g_named_names; i++)
        std::strncpy(&g_named_names[i * 0xff], g_data->named[i].name.c_str(), 0xfe);
    g_world_count = static_cast<int32_t>(g_data->worlds.size());
    g_image_count = static_cast<int32_t>(g_data->images.size());
    for (size_t i = 0; i < 40 && i < g_data->images.size(); i++) g_images[i] = g_data->images[i];
    g_room_count = g_data->room_count;
    g_start_room = g_data->start_room;
    return true;
}

}  // namespace hoh
