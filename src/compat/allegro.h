// Subconjunto da API do Allegro 4.0 usado pelo Head Over Heels (Retrospec, 2003).
//
// O HoH.exe original importa 67 funcoes/variaveis da alleg40.dll (lista em
// HeadOverHeels/re/FORMATOS.md). Este header declara essas mesmas funcoes, com a
// semantica do Allegro 4.0, para que a logica decompilada do jogo possa ser portada
// quase linha a linha. A implementacao e nossa:
//   - gfx.cpp, font.cpp, packfile.cpp, datafile.cpp: nucleo sem dependencias
//   - platform_sdl.cpp, audio_sdl.cpp: video, entrada, timers e audio via SDL2
//
// Diferencas deliberadas em relacao ao Allegro:
//   - so existe profundidade de cor 16 bpp (RGB565), a unica que o jogo usa;
//   - "video bitmaps" sao bitmaps em memoria; show_video_bitmap() apresenta o quadro;
//   - nao ha drivers: set_gfx_mode escolhe entre janela e tela cheia.
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

// ------------------------------------------------------------------ sistema
int install_allegro(int system_id, int *errno_ptr, int (*atexit_ptr)(void (*)(void)));
#define allegro_init() install_allegro(0, nullptr, nullptr)
void allegro_exit(void);
void allegro_message(const char *fmt, ...);
extern char allegro_error[];
extern int cpu_capabilities;

#define CPU_MMX  0x0004
#define CPU_SSE  0x0080

// ------------------------------------------------------------------ bitmaps
#define MASK_COLOR_16 0xF81F

struct BITMAP {
    int w, h;
    int clip;            // recorte ligado
    int cl, cr, ct, cb;  // retangulo de recorte: cl/ct inclusivos, cr/cb exclusivos (como no Allegro 4)
    int is_video;        // criado por create_video_bitmap
    uint16_t *dat;       // w*h pixels RGB565, linhas contiguas (pitch == w)
    uint16_t **line;     // line[y] == dat + y*w; o jogo le dados direto em line[0]
};

BITMAP *create_bitmap(int width, int height);
BITMAP *create_video_bitmap(int width, int height);
void destroy_bitmap(BITMAP *bmp);
void set_clip(BITMAP *bmp, int x1, int y1, int x2, int y2);

int makecol(int r, int g, int b);
int getr(int c);
int getg(int c);
int getb(int c);
int getpixel(BITMAP *bmp, int x, int y);
void putpixel(BITMAP *bmp, int x, int y, int color);
void clear_to_color(BITMAP *bmp, int color);
void rectfill(BITMAP *bmp, int x1, int y1, int x2, int y2, int color);
void circlefill(BITMAP *bmp, int x, int y, int radius, int color);

void blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x, int dest_y,
          int width, int height);
void masked_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int dest_x,
                 int dest_y, int width, int height);
void draw_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y);
void stretch_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int w, int h);

// FBlend (lib de terceiros linkada estaticamente no HoH.exe; ver FORMATOS.md §6):
// mistura translucida/aditiva de sprites 16 bpp, respeitando a cor de mascara.
void fblend_trans(BITMAP *src, BITMAP *dst, int x, int y, int fact);
void fblend_add(BITMAP *src, BITMAP *dst, int x, int y, int fact);

// ------------------------------------------------------------------ texto
struct FONT;
extern FONT *font;
void textout(BITMAP *bmp, const FONT *f, const char *s, int x, int y, int color);
void textprintf(BITMAP *bmp, const FONT *f, int x, int y, int color, const char *fmt, ...);
int text_length(const FONT *f, const char *s);
int text_height(const FONT *f);

// ------------------------------------------------------------------ video
#define GFX_TEXT                  -1
#define GFX_AUTODETECT             0
#define GFX_AUTODETECT_FULLSCREEN  1
#define GFX_AUTODETECT_WINDOWED    2
#define AL_ID(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define GFX_DIRECTX        AL_ID('D', 'X', 'A', 'C')  // valores usados pelo HoH.exe (-r0..-r4)
#define GFX_DIRECTX_WIN    AL_ID('D', 'X', 'W', 'N')
#define GFX_GDI            AL_ID('G', 'D', 'I', 'B')

#define GFX_CAN_TRIPLE_BUFFER 0x00000010

struct GFX_DRIVER {
    int id;
    const char *name;
    const char *desc;
    const char *ascii_name;
    int windowed;
};

extern BITMAP *screen;
extern GFX_DRIVER *gfx_driver;
extern int gfx_capabilities;
#define SCREEN_W (screen ? screen->w : 0)
#define SCREEN_H (screen ? screen->h : 0)

void set_color_depth(int depth);
int set_gfx_mode(int card, int w, int h, int v_w, int v_h);
int enable_triple_buffer(void);
int request_video_bitmap(BITMAP *bitmap);
int show_video_bitmap(BITMAP *bitmap);
int poll_scroll(void);
void vsync(void);

// ------------------------------------------------------------------ teclado
// Numeracao do Allegro 4.0 (confirmada pela tabela de nomes de teclas do HoH.exe em 0x4321a4).
enum {
    KEY_A = 1, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_0_PAD, KEY_1_PAD, KEY_2_PAD, KEY_3_PAD, KEY_4_PAD, KEY_5_PAD, KEY_6_PAD, KEY_7_PAD,
    KEY_8_PAD, KEY_9_PAD,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_ESC, KEY_TILDE, KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB, KEY_OPENBRACE,
    KEY_CLOSEBRACE, KEY_ENTER, KEY_COLON, KEY_QUOTE, KEY_BACKSLASH, KEY_BACKSLASH2, KEY_COMMA,
    KEY_STOP, KEY_SLASH, KEY_SPACE, KEY_INSERT, KEY_DEL, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_SLASH_PAD, KEY_ASTERISK, KEY_MINUS_PAD,
    KEY_PLUS_PAD, KEY_DEL_PAD, KEY_ENTER_PAD, KEY_PRTSCR, KEY_PAUSE, KEY_ABNT_C1, KEY_YEN,
    KEY_KANA, KEY_CONVERT, KEY_NOCONVERT, KEY_AT, KEY_CIRCUMFLEX, KEY_COLON2, KEY_KANJI,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL, KEY_ALT, KEY_ALTGR, KEY_LWIN, KEY_RWIN,
    KEY_MENU, KEY_SCRLOCK, KEY_NUMLOCK, KEY_CAPSLOCK,
    KEY_MAX
};
static_assert(KEY_CAPSLOCK == 114, "numeracao de teclas do Allegro 4.0");

int install_keyboard(void);
extern volatile char key[KEY_MAX];
int keypressed(void);
int readkey(void);  // (scancode << 8) | ascii
void clear_keybuf(void);

// ------------------------------------------------------------------ joystick
// Layout do Allegro 4.0 (o HoH.exe usa stride 0x1B8 e botoes em +0x138).
#define MAX_JOYSTICKS        8
#define MAX_JOYSTICK_AXIS    3
#define MAX_JOYSTICK_STICKS  5
#define MAX_JOYSTICK_BUTTONS 16
#define JOY_TYPE_AUTODETECT  -1

struct JOYSTICK_AXIS_INFO {
    int pos;  // -128..128
    int d1;   // direcao negativa (esquerda/cima) ativa
    int d2;   // direcao positiva (direita/baixo) ativa
    const char *name;
};
struct JOYSTICK_STICK_INFO {
    int flags;
    int num_axis;
    JOYSTICK_AXIS_INFO axis[MAX_JOYSTICK_AXIS];
    const char *name;
};
struct JOYSTICK_BUTTON_INFO {
    int b;
    const char *name;
};
struct JOYSTICK_INFO {
    int flags;
    int num_sticks;
    int num_buttons;
    JOYSTICK_STICK_INFO stick[MAX_JOYSTICK_STICKS];
    JOYSTICK_BUTTON_INFO button[MAX_JOYSTICK_BUTTONS];
};

extern JOYSTICK_INFO joy[MAX_JOYSTICKS];
extern int num_joysticks;
int install_joystick(int type);
int poll_joystick(void);

// ------------------------------------------------------------------ timers
#define TIMERS_PER_SECOND 1193181L
#define SECS_TO_TIMER(x)  ((long)(x) * TIMERS_PER_SECOND)
#define MSEC_TO_TIMER(x)  ((long)(x) * (TIMERS_PER_SECOND / 1000))
#define BPS_TO_TIMER(x)   (TIMERS_PER_SECOND / (long)(x))

int install_int_ex(void (*proc)(void), long speed);
void remove_int(void (*proc)(void));
void rest(unsigned int ms);

// ------------------------------------------------------------------ som
#define DIGI_AUTODETECT -1
#define DIGI_NONE        0
#define MIDI_NONE        0
#define PLAYMODE_PLAY     0
#define PLAYMODE_LOOP     1
#define PLAYMODE_FORWARD  0
#define PLAYMODE_BACKWARD 2
#define PLAYMODE_BIDIR    4

struct SAMPLE {
    int bits;              // 8 ou 16 (PCM sem sinal, como no Allegro)
    int stereo;
    int freq;
    int priority;
    unsigned long len;     // em quadros (amostras por canal)
    unsigned long loop_start;
    unsigned long loop_end;
    unsigned long param;
    void *data;
};

int install_sound(int digi, int midi, const char *cfg_path);
void remove_sound(void);
void reserve_voices(int digi_voices, int midi_voices);
void set_volume_per_voice(int scale);
int play_sample(const SAMPLE *spl, int vol, int pan, int freq, int loop);
void stop_sample(const SAMPLE *spl);
void destroy_sample(SAMPLE *spl);
int allocate_voice(const SAMPLE *spl);
void deallocate_voice(int voice);
void release_voice(int voice);
void voice_start(int voice);
void voice_stop(int voice);
const SAMPLE *voice_check(int voice);
int voice_get_position(int voice);
void voice_set_volume(int voice, int volume);
void voice_set_pan(int voice, int pan);
void voice_set_frequency(int voice, int frequency);
void voice_set_playmode(int voice, int playmode);

// ------------------------------------------------------------------ packfiles
struct PACKFILE;
void packfile_password(const char *password);
PACKFILE *pack_fopen(const char *filename, const char *mode);  // "r", "w", "rp", "wp", "rbp", "wbp"...
int pack_fclose(PACKFILE *f);
long pack_fread(void *p, long n, PACKFILE *f);
long pack_fwrite(const void *p, long n, PACKFILE *f);
int pack_feof(PACKFILE *f);

// ------------------------------------------------------------------ datafiles
#define DAT_ID(a, b, c, d) AL_ID(a, b, c, d)
#define DAT_END    -1
#define DAT_FILE   DAT_ID('F', 'I', 'L', 'E')
#define DAT_DATA   DAT_ID('D', 'A', 'T', 'A')
#define DAT_SAMPLE DAT_ID('S', 'A', 'M', 'P')
#define DAT_NAME   DAT_ID('N', 'A', 'M', 'E')

struct DATAFILE_PROPERTY {
    char *dat;
    int type;
};
struct DATAFILE {
    void *dat;
    int type;
    long size;
    DATAFILE_PROPERTY *prop;
};

DATAFILE *load_datafile(const char *filename);
void unload_datafile(DATAFILE *dat);
const char *get_datafile_property(const DATAFILE *dat, int type);

// ------------------------------------------------------------------ matematica
typedef int32_t fixed;
fixed fixsqrt(fixed x);
inline fixed itofix(int x) { return x << 16; }
inline int fixtoi(fixed x) { return (x >> 16) + ((x & 0x8000) >> 15); }
