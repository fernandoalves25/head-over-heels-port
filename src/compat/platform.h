// Extensoes do port que nao existem no Allegro (janela, tela cheia, musica Ogg, saida).
#pragma once

#include <string>

// Processa eventos do SO (teclado, controles, janela). As funcoes de entrada do Allegro ja
// chamam isto; o laco principal deve chamar ao menos uma vez por quadro.
void platform_pump(void);
// Envia o bitmap que esta sendo mostrado (screen ou o ultimo show_video_bitmap) para a tela.
void platform_present(void);
// Janela fechada / SIGTERM / combinacao de saida do console portatil.
bool platform_quit_requested(void);
void platform_toggle_fullscreen(void);
bool platform_is_fullscreen(void);
// true em consoles portateis (SDL com KMSDRM, ex.: R36S) -> sempre tela cheia 640x480.
bool platform_is_handheld(void);
// Pasta do executavel (com '/' no fim) ou "" se indisponivel.
std::string platform_base_path(void);

// ------------------------------------------------------------------ musica (substitui o alogg)
struct MusicStream;
MusicStream *music_open(const char *path);  // Ogg Vorbis
void music_play(MusicStream *m, int volume, bool loop);  // volume 0..255
void music_stop(MusicStream *m);
void music_set_volume(MusicStream *m, int volume);
bool music_is_playing(MusicStream *m);
void music_close(MusicStream *m);
