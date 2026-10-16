// Liga o leitor de dados do port (src/data/hohdata.h) as globais do jogo original.
#pragma once

#include "data/hohdata.h"

namespace hoh {

extern GameData *g_data;        // HoHOriginal.dat decodificado
extern std::string g_data_dir;  // pasta do jogo original, com '/' no fim

// 0x00402f30 load_game_data: carrega Sound/Sound.dat e HoHOriginal.dat e preenche g_images[],
// g_sound_datafile etc. Retorna false (com mensagem em allegro_error) se faltar algum arquivo.
bool load_game_data();

// Caminho de um arquivo do jogo original ("Sound\\Theme.ogg" -> "<pasta>/Sound/Theme.ogg").
std::string data_path(const char *original_relative_path);

}  // namespace hoh
