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
// Copia texto para a area de transferencia do sistema (o original usava a API do Windows).
bool platform_set_clipboard_text(const char *text);
// Quadros interpolados entre os ticks de logica (F9: auto -> taxa do monitor -> metade -> 30).
bool platform_smooth_enabled(void);
// Em volta de cada quadro interpolado: o fim segura o ritmo (a taxa do monitor dividida por um
// inteiro: 120 Hz -> 120 ou 60 quadros novos por segundo), repetindo o quadro no vsync ou, se o
// vsync nao segurar, esperando pelo relogio; no modo auto, cai para a metade se o PC nao acompanhar.
void platform_smooth_frame_begin(void);
void platform_smooth_frame_end(void);
// Para o menu de opcoes: modo atual (-1 = auto, 0 = so os 30 do original, N = N por segundo), a
// taxa que ele da agora e o mesmo ciclo do F9 (dir < 0 volta); a troca fica salva no port.cfg.
int platform_fps_mode(void);
int platform_fps_target(void);
void platform_cycle_fps_mode(int dir);

// ------------------------------------------------------------------ musica (substitui o alogg)
struct MusicStream;
MusicStream *music_open(const char *path);  // Ogg Vorbis
void music_play(MusicStream *m, int volume, bool loop);  // volume 0..255
void music_stop(MusicStream *m);
void music_set_volume(MusicStream *m, int volume);
bool music_is_playing(MusicStream *m);
void music_close(MusicStream *m);
