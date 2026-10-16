#include "game/music.h"

#include "game/data_bridge.h"

#include <algorithm>

namespace hoh {

MusicStream *g_ogg_theme = nullptr;
MusicStream *g_ogg_game = nullptr;
MusicStream *g_music_stream = nullptr;

static int to_volume(float v) { return std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255); }

// 0x00418190 ogg_load_file: o segundo argumento (sempre 1) e o modo de repeticao.
MusicStream *ogg_load_file(const char *original_path, int loop) {
    (void)loop;  // o jogo sempre repete as duas musicas
    return music_open(data_path(original_path).c_str());
}

// 0x00427ec0 ogg_play(ogg, canais=2, ?, volume 0..1): recomeca a musica do inicio.
MusicStream *ogg_play(MusicStream *ogg, int channels, int unused, float volume) {
    (void)channels;
    (void)unused;
    if (!ogg) return nullptr;
    music_play(ogg, to_volume(volume), true);
    return ogg;
}

// 0x00427fe0 (era "ogg_poll"): so grava o volume no stream.
void ogg_set_volume(MusicStream *stream, float volume) {
    if (stream) music_set_volume(stream, to_volume(volume));
}

// 0x00427f70 ogg_stop: para e libera o stream (o arquivo continua aberto).
void ogg_stop(MusicStream *stream) {
    if (stream) music_stop(stream);
}

// 0x004280e0 ogg_destroy
void ogg_destroy(MusicStream *ogg) {
    if (ogg) music_close(ogg);
}

}  // namespace hoh
