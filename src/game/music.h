// Musica Ogg do jogo (no original: lib alogg sobre play_audio_stream; ver FORMATOS.md §6).
// Aqui o "arquivo" e o "stream" sao o mesmo MusicStream da camada de plataforma: o mixer puxa
// os dados sozinho, entao nao ha o que alimentar no timer.
#pragma once

#include "compat/platform.h"

namespace hoh {

extern MusicStream *g_ogg_theme;     // 0x0044b7b8 Sound\Theme.ogg
extern MusicStream *g_ogg_game;      // 0x00a5b684 Sound\Game.ogg
extern MusicStream *g_music_stream;  // 0x0091e01c stream tocando (retorno de ogg_play)

MusicStream *ogg_load_file(const char *original_path, int loop);                   // 0x00418190
MusicStream *ogg_play(MusicStream *ogg, int channels, int unused, float volume);   // 0x00427ec0
void ogg_set_volume(MusicStream *stream, float volume);                            // 0x00427fe0
void ogg_stop(MusicStream *stream);                                                // 0x00427f70
void ogg_destroy(MusicStream *ogg);                                                // 0x004280e0

}  // namespace hoh
