// Port (renderizador HD): liga cada imagem do HoHOriginal.dat ao nome do arquivo no pacote HD.
#pragma once

namespace hoh {

// Depois de carregar o arquivo e antes de engine_build_game_objects: nomes iguais aos do export
// de re/tools/hohfmt.py (block000_d0_f00_a, mobile000_f00, named_000_Dungeon_f00, ...).
void hd_register_assets();
// Depois de engine_build_game_objects: ladrilhos de piso e celulas de parede viram "derivadas"
// (de que pixel do asset veio cada pixel), rodando o mesmo recorte do jogo sobre imagens de coordenadas.
void hd_tag_sets();
// Teste (HOH_HD_ROOMTOUR=<pasta>): desenha cada sala como no jogo, confere a lista do HD contra o
// quadro 1x (salas.txt) e salva algumas capturas HD. Volta quando termina.
void hd_room_tour(const char *out_dir);

}  // namespace hoh
