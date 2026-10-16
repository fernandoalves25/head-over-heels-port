// Video, teclado, controles e timers do Allegro 4.0 implementados sobre SDL2.
#include "compat/allegro.h"
#include "compat/platform.h"
#include "compat/video_scaler.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

// ------------------------------------------------------------------ estado global do Allegro
char allegro_error[256] = "";
int cpu_capabilities = 0;
BITMAP *screen = nullptr;
GFX_DRIVER *gfx_driver = nullptr;
int gfx_capabilities = 0;
volatile char key[KEY_MAX] = {};
JOYSTICK_INFO joy[MAX_JOYSTICKS] = {};
int num_joysticks = 0;

namespace {

SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
VideoScaler g_scaler;
ScaleMode g_scale_mode = ScaleMode::Smooth3x;  // padrao no PC (F10 alterna; port.cfg guarda)
bool g_scaler_ready = false;
// Quadros interpolados (port.cfg "fps"; F9 alterna): kFpsOff = so os 30 do original, kFpsAuto = taxa
// do monitor (cai para a metade se o PC nao acompanhar), N = N por segundo, arredondado para a taxa do
// monitor dividida por um inteiro (num monitor de 120 Hz: 120 ou 60).
constexpr int kFpsOff = 0, kFpsAuto = -1;
int g_fps_mode = kFpsAuto;
bool g_fps_cfg_set = false;
BITMAP *g_shown = nullptr;  // bitmap mostrado na tela (screen ou o ultimo show_video_bitmap)
GFX_DRIVER g_driver = {0, "SDL2", "SDL2", "SDL2", 1, 0, 0};
bool g_quit = false;
bool g_fullscreen = false;
std::deque<int> g_keybuf;

// ------------------------------------------------------------------ port.cfg (opcoes do port)
const char *kPortCfg = "port.cfg";

int fps_mode_from_name(const char *v) {
    if (std::strcmp(v, "auto") == 0) return kFpsAuto;
    int n = std::atoi(v);
    return n > 30 ? n : kFpsOff;  // "off", "0", "30": como o original
}

void load_port_cfg() {
    if (FILE *f = std::fopen(kPortCfg, "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            char key_[64] = "", val[128] = "";
            if (std::sscanf(line, " %63[^= ] = %127s", key_, val) == 2) {
                if (std::strcmp(key_, "scaler") == 0) scale_mode_from_name(val, &g_scale_mode);
                if (std::strcmp(key_, "fps") == 0) { g_fps_mode = fps_mode_from_name(val); g_fps_cfg_set = true; }
                if (std::strcmp(key_, "smooth") == 0 && !g_fps_cfg_set) {  // port.cfg antigo (liga/desliga)
                    g_fps_mode = std::atoi(val) != 0 ? kFpsAuto : kFpsOff;
                    g_fps_cfg_set = true;
                }
            }
        }
        std::fclose(f);
    }
    if (const char *env = std::getenv("HOH_SCALER")) scale_mode_from_name(env, &g_scale_mode);
    if (const char *env = std::getenv("HOH_SMOOTH")) { g_fps_mode = std::atoi(env) != 0 ? kFpsAuto : kFpsOff; g_fps_cfg_set = true; }
    if (const char *env = std::getenv("HOH_FPS")) { g_fps_mode = fps_mode_from_name(env); g_fps_cfg_set = true; }
}

void save_port_cfg() {
    if (FILE *f = std::fopen(kPortCfg, "w")) {
        std::fprintf(f, "# Opcoes do port (nao existem no jogo original)\n");
        std::fprintf(f, "# scaler: sharp | smooth2x | smooth3x | nearest | integer   (F10 alterna)\n");
        std::fprintf(f, "scaler = %s\n", scale_mode_name(g_scale_mode));
        std::fprintf(f, "# fps: auto | 120 | 60 | ... | off   (F9 alterna; auto = taxa do monitor, cai para a metade\n");
        std::fprintf(f, "#      se o PC nao acompanhar; off = so os 30 quadros do original)\n");
        if (g_fps_mode == kFpsAuto) std::fprintf(f, "fps = auto\n");
        else if (g_fps_mode == kFpsOff) std::fprintf(f, "fps = off\n");
        else std::fprintf(f, "fps = %d\n", g_fps_mode);
        std::fclose(f);
    }
}

// ------------------------------------------------------------------ ritmo dos quadros interpolados
// O jogo pensa a 30 ticks/s; os quadros interpolados saem a uma fracao inteira da taxa do monitor
// (refresh / divisor) para cada quadro ficar o mesmo numero de retracos na tela. Com vsync, o
// present ja espera o retraco e o quadro e repetido (divisor - 1) vezes; se o vsync nao segurar
// (driver forcando desligado, renderer por software), espera-se pelo relogio.
struct FramePacer {
    enum class Vsync { Unknown, On, Off };
    int refresh = 60;           // Hz do monitor da janela
    int auto_divisor = 1;       // divisor do modo auto (sobe quando o PC nao acompanha)
    Vsync vsync = Vsync::Unknown;
    uint64_t begin = 0, last_end = 0, deadline = 0;
    uint64_t hold_until = 0;    // auto: nao tenta voltar para a taxa maior antes disto
    double backoff_s = 5;
    // janela de avaliacao (~1 s)
    uint64_t win_t0 = 0;
    int win_frames = 0, win_misses = 0;
    double win_interval = 0, win_offgrid = 0, win_work = 0, win_work_max = 0;
} g_pacer;

int divisor_for(int mode, int refresh) {
    if (mode == kFpsAuto) return g_pacer.auto_divisor;
    if (mode <= 0) return 1;
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(refresh) / mode)));
}
int pacer_divisor() { return divisor_for(g_fps_mode, g_pacer.refresh); }

void pacer_reset_window() {
    g_pacer.win_t0 = 0;
    g_pacer.win_frames = g_pacer.win_misses = 0;
    g_pacer.win_interval = g_pacer.win_offgrid = g_pacer.win_work = g_pacer.win_work_max = 0;
}

// Volta a medir do zero (outro monitor, tela cheia, outro modo).
void pacer_reset(bool redetect_vsync) {
    if (redetect_vsync) g_pacer.vsync = FramePacer::Vsync::Unknown;
    g_pacer.auto_divisor = 1;
    g_pacer.hold_until = 0;
    g_pacer.backoff_s = 5;
    g_pacer.last_end = g_pacer.deadline = 0;
    pacer_reset_window();
}

void update_window_title() {
    if (!g_window) return;
    char fps[48];
    if (g_fps_mode == kFpsOff) std::snprintf(fps, sizeof fps, "30 (original)");
    else std::snprintf(fps, sizeof fps, "%s%d/s%s", g_fps_mode == kFpsAuto ? "auto (" : "",
                       static_cast<int>(std::lround(static_cast<double>(g_pacer.refresh) / pacer_divisor())),
                       g_fps_mode == kFpsAuto ? ")" : "");
    char t[160];
    std::snprintf(t, sizeof t, "Head Over Heels   [F9 quadros: %s | F10 escala: %s | F11 tela cheia]", fps,
                  scale_mode_name(g_scale_mode));
    SDL_SetWindowTitle(g_window, t);
}

void query_refresh_rate() {
    int hz = 60;
    SDL_DisplayMode m;
    int idx = g_window ? SDL_GetWindowDisplayIndex(g_window) : 0;
    if (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &m) == 0 && m.refresh_rate > 0) hz = m.refresh_rate;
    if (const char *e = std::getenv("HOH_REFRESH")) hz = std::max(30, std::atoi(e));  // testes
    if (hz != g_pacer.refresh) {
        g_pacer.refresh = hz;
        pacer_reset(true);
        update_window_title();
    }
}

// F9: auto -> taxa do monitor -> metade -> ... (enquanto >= 50/s) -> 30 do original -> auto.
void cycle_fps_mode(int dir) {
    std::vector<int> modes = {kFpsAuto};
    for (int d = 1; d <= 6 && g_pacer.refresh / d >= 50; d++)
        modes.push_back(static_cast<int>(std::lround(static_cast<double>(g_pacer.refresh) / d)));
    if (modes.size() == 1) modes.push_back(g_pacer.refresh);
    modes.push_back(kFpsOff);
    size_t next = 0;
    for (size_t i = 0; i < modes.size(); i++) {
        bool same = modes[i] == g_fps_mode ||
                    (modes[i] > 0 && g_fps_mode > 0 &&
                     divisor_for(modes[i], g_pacer.refresh) == divisor_for(g_fps_mode, g_pacer.refresh));
        if (same) {
            next = (i + modes.size() + (dir < 0 ? -1 : 1)) % modes.size();
            break;
        }
    }
    g_fps_mode = modes[next];
    pacer_reset(false);
}

void sleep_until(uint64_t t) {
    const double f = static_cast<double>(SDL_GetPerformanceFrequency());
    for (;;) {
        uint64_t now = SDL_GetPerformanceCounter();
        if (now >= t) return;
        double ms = (t - now) * 1000.0 / f;
        if (ms > 2.0) SDL_Delay(static_cast<Uint32>(ms - 1.5));
        else SDL_Delay(0);  // ultimo trecho: so cede a CPU
    }
}

// ------------------------------------------------------------------ teclado
int map_scancode(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return KEY_A + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return KEY_1 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return KEY_F1 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return KEY_1_PAD + (s - SDL_SCANCODE_KP_1);
    switch (s) {
        case SDL_SCANCODE_0: return KEY_0;
        case SDL_SCANCODE_KP_0: return KEY_0_PAD;
        case SDL_SCANCODE_ESCAPE: return KEY_ESC;
        case SDL_SCANCODE_GRAVE: return KEY_TILDE;
        case SDL_SCANCODE_MINUS: return KEY_MINUS;
        case SDL_SCANCODE_EQUALS: return KEY_EQUALS;
        case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
        case SDL_SCANCODE_TAB: return KEY_TAB;
        case SDL_SCANCODE_LEFTBRACKET: return KEY_OPENBRACE;
        case SDL_SCANCODE_RIGHTBRACKET: return KEY_CLOSEBRACE;
        case SDL_SCANCODE_RETURN: return KEY_ENTER;
        case SDL_SCANCODE_SEMICOLON: return KEY_COLON;
        case SDL_SCANCODE_APOSTROPHE: return KEY_QUOTE;
        case SDL_SCANCODE_BACKSLASH: return KEY_BACKSLASH;
        case SDL_SCANCODE_NONUSBACKSLASH: return KEY_BACKSLASH2;
        case SDL_SCANCODE_COMMA: return KEY_COMMA;
        case SDL_SCANCODE_PERIOD: return KEY_STOP;
        case SDL_SCANCODE_SLASH: return KEY_SLASH;
        case SDL_SCANCODE_SPACE: return KEY_SPACE;
        case SDL_SCANCODE_INSERT: return KEY_INSERT;
        case SDL_SCANCODE_DELETE: return KEY_DEL;
        case SDL_SCANCODE_HOME: return KEY_HOME;
        case SDL_SCANCODE_END: return KEY_END;
        case SDL_SCANCODE_PAGEUP: return KEY_PGUP;
        case SDL_SCANCODE_PAGEDOWN: return KEY_PGDN;
        case SDL_SCANCODE_LEFT: return KEY_LEFT;
        case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
        case SDL_SCANCODE_UP: return KEY_UP;
        case SDL_SCANCODE_DOWN: return KEY_DOWN;
        case SDL_SCANCODE_KP_DIVIDE: return KEY_SLASH_PAD;
        case SDL_SCANCODE_KP_MULTIPLY: return KEY_ASTERISK;
        case SDL_SCANCODE_KP_MINUS: return KEY_MINUS_PAD;
        case SDL_SCANCODE_KP_PLUS: return KEY_PLUS_PAD;
        case SDL_SCANCODE_KP_PERIOD: return KEY_DEL_PAD;
        case SDL_SCANCODE_KP_ENTER: return KEY_ENTER_PAD;
        case SDL_SCANCODE_PRINTSCREEN: return KEY_PRTSCR;
        case SDL_SCANCODE_PAUSE: return KEY_PAUSE;
        case SDL_SCANCODE_LSHIFT: return KEY_LSHIFT;
        case SDL_SCANCODE_RSHIFT: return KEY_RSHIFT;
        case SDL_SCANCODE_LCTRL: return KEY_LCONTROL;
        case SDL_SCANCODE_RCTRL: return KEY_RCONTROL;
        case SDL_SCANCODE_LALT: return KEY_ALT;
        case SDL_SCANCODE_RALT: return KEY_ALTGR;
        case SDL_SCANCODE_LGUI: return KEY_LWIN;
        case SDL_SCANCODE_RGUI: return KEY_RWIN;
        case SDL_SCANCODE_APPLICATION: return KEY_MENU;
        case SDL_SCANCODE_SCROLLLOCK: return KEY_SCRLOCK;
        case SDL_SCANCODE_NUMLOCKCLEAR: return KEY_NUMLOCK;
        case SDL_SCANCODE_CAPSLOCK: return KEY_CAPSLOCK;
        default: return 0;
    }
}

int ascii_for(const SDL_Keysym &ks) {
    SDL_Keycode k = ks.sym;
    bool shift = (ks.mod & KMOD_SHIFT) != 0, caps = (ks.mod & KMOD_CAPS) != 0;
    if (k >= SDLK_a && k <= SDLK_z) return (shift != caps) ? k - 32 : k;
    switch (k) {
        case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
        case SDLK_BACKSPACE: return 8;
        case SDLK_ESCAPE: return 27;
        case SDLK_TAB: return 9;
        default: break;
    }
    if (k >= 32 && k < 127) return k;
    return 0;
}

// ------------------------------------------------------------------ controles
struct Pad {
    SDL_GameController *gc = nullptr;
    SDL_Joystick *js = nullptr;  // joystick sem mapeamento de GameController
    SDL_JoystickID id = -1;
};
std::vector<Pad> g_pads;
bool g_joy_installed = false;

const SDL_GameControllerButton kPadButtons[] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_GUIDE};
const char *kPadButtonNames[] = {"A", "B", "X", "Y", "L1", "R1", "SELECT", "START", "L3", "R3", "GUIDE", "L2", "R2"};
constexpr int kPadButtonCount = 13;  // 11 botoes + 2 gatilhos

void refresh_joystick_info() {
    num_joysticks = std::min<int>(static_cast<int>(g_pads.size()), MAX_JOYSTICKS);
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        JOYSTICK_INFO &j = joy[i];
        std::memset(&j, 0, sizeof j);
        if (i >= num_joysticks) continue;
        j.num_sticks = 1;
        j.stick[0].num_axis = 2;
        j.stick[0].name = "STICK";
        j.stick[0].axis[0].name = "X";
        j.stick[0].axis[1].name = "Y";
        if (g_pads[i].gc) {
            j.num_buttons = kPadButtonCount;
            for (int b = 0; b < kPadButtonCount; b++) j.button[b].name = kPadButtonNames[b];
        } else {
            j.num_buttons = std::min(SDL_JoystickNumButtons(g_pads[i].js), MAX_JOYSTICK_BUTTONS);
            static const char *names[MAX_JOYSTICK_BUTTONS] = {"B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8",
                                                              "B9", "B10", "B11", "B12", "B13", "B14", "B15", "B16"};
            for (int b = 0; b < j.num_buttons; b++) j.button[b].name = names[b];
        }
    }
}

void open_pad(int device_index) {
    Pad p;
    if (SDL_IsGameController(device_index)) {
        p.gc = SDL_GameControllerOpen(device_index);
        if (!p.gc) return;
        p.id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p.gc));
    } else {
        p.js = SDL_JoystickOpen(device_index);
        if (!p.js) return;
        p.id = SDL_JoystickInstanceID(p.js);
    }
    for (const Pad &q : g_pads)
        if (q.id == p.id) {
            if (p.gc) SDL_GameControllerClose(p.gc);
            if (p.js) SDL_JoystickClose(p.js);
            return;
        }
    g_pads.push_back(p);
    refresh_joystick_info();
}

void close_pad(SDL_JoystickID id) {
    for (size_t i = 0; i < g_pads.size(); i++)
        if (g_pads[i].id == id) {
            if (g_pads[i].gc) SDL_GameControllerClose(g_pads[i].gc);
            if (g_pads[i].js) SDL_JoystickClose(g_pads[i].js);
            g_pads.erase(g_pads.begin() + static_cast<long>(i));
            refresh_joystick_info();
            return;
        }
}

void set_axis(JOYSTICK_AXIS_INFO &a, int pos) {
    a.pos = std::clamp(pos, -128, 128);
    a.d1 = a.pos < -64;
    a.d2 = a.pos > 64;
}

// ------------------------------------------------------------------ timers
struct Timer {
    void (*proc)(void);
    uint64_t period;  // em contagens de SDL_GetPerformanceCounter
    uint64_t next;
};
std::mutex g_timer_mutex;
std::vector<Timer> g_timers;
std::thread g_timer_thread;
std::atomic<bool> g_timer_running{false};

void timer_loop() {
    while (g_timer_running) {
        uint64_t now = SDL_GetPerformanceCounter();
        std::vector<void (*)(void)> calls;
        {
            std::lock_guard<std::mutex> lock(g_timer_mutex);
            for (Timer &t : g_timers) {
                if (now - t.next < UINT64_C(1) << 63 && now - t.next > t.period * 10) t.next = now;  // evita rajadas apos travamentos
                while (static_cast<int64_t>(now - t.next) >= 0) {
                    calls.push_back(t.proc);
                    t.next += t.period;
                }
            }
        }
        for (auto proc : calls) proc();  // como no Allegro: os callbacks rodam na thread do timer
        SDL_Delay(1);
    }
}

// ------------------------------------------------------------------ roteiro de teste automatico
// Variaveis de ambiente (para testar sem ninguem no teclado, inclusive com SDL_VIDEODRIVER=dummy):
//   HOH_KEYS="3000:ENTER;3600:DOWN/800"   (ou por virgula; /ms = segura) aperta a tecla no instante (ms) e solta 120 ms depois
//   HOH_SHOTS="2500:a.bmp;5000:b.bmp" salva o quadro apresentado no instante (ms)
//   HOH_AUTOQUIT_MS=8000              pede para sair depois desse tempo
struct ScriptKey { uint32_t at; int key; uint32_t hold = 120; bool down_done = false, up_done = false; };
struct ScriptShot { uint32_t at; std::string path; bool done = false; };
std::vector<ScriptKey> g_script_keys;
std::vector<ScriptShot> g_script_shots;
std::vector<ScriptShot> g_script_scaled_shots;  // HOH_SCALED_SHOTS: quadro final ja ampliado
uint32_t g_script_quit_at = 0, g_script_t0 = 0;
bool g_script_loaded = false;

int script_key_code(const std::string &n) {
    static const std::pair<const char *, int> names[] = {
        {"ENTER", KEY_ENTER}, {"ESC", KEY_ESC}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT},
        {"RIGHT", KEY_RIGHT}, {"SPACE", KEY_SPACE}, {"LCTRL", KEY_LCONTROL}, {"RCTRL", KEY_RCONTROL}};
    for (auto &p : names)
        if (n == p.first) return p.second;
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return KEY_A + (n[0] - 'A');
    if (n == "F9") return KEY_F9;
    if (n == "F10") return KEY_F10;
    return std::atoi(n.c_str());
}

void script_load() {
    g_script_loaded = true;
    g_script_t0 = SDL_GetTicks();
    auto parse = [](const char *env, auto &&fn) {
        if (!env) return;
        std::string s = env;
        size_t p = 0;
        while (p < s.size()) {
            size_t e = s.find_first_of(";,", p);  // ';' ou ',' (o wsl.exe quebra comandos no ';')
            std::string item = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
            size_t c = item.find(':');
            if (c != std::string::npos) fn(static_cast<uint32_t>(std::atol(item.substr(0, c).c_str())), item.substr(c + 1));
            if (e == std::string::npos) break;
            p = e + 1;
        }
    };
    parse(std::getenv("HOH_KEYS"), [](uint32_t at, const std::string &k) {
        size_t slash = k.find('/');  // "RIGHT/2500" = segura 2,5 s
        ScriptKey sk{at, script_key_code(k.substr(0, slash))};
        if (slash != std::string::npos) sk.hold = static_cast<uint32_t>(std::atol(k.c_str() + slash + 1));
        g_script_keys.push_back(sk);
    });
    parse(std::getenv("HOH_SHOTS"), [](uint32_t at, const std::string &f) { g_script_shots.push_back({at, f}); });
    parse(std::getenv("HOH_SCALED_SHOTS"), [](uint32_t at, const std::string &f) { g_script_scaled_shots.push_back({at, f}); });
    if (const char *q = std::getenv("HOH_AUTOQUIT_MS")) g_script_quit_at = static_cast<uint32_t>(std::atol(q));
}

void script_tick() {
    if (!g_script_loaded) script_load();
    uint32_t t = SDL_GetTicks() - g_script_t0;
    for (ScriptKey &k : g_script_keys) {
        if (!k.down_done && t >= k.at && (k.key == KEY_F9 || k.key == KEY_F10)) {
            k.down_done = k.up_done = true;  // teclas do port: passam pelo tratamento de eventos
            SDL_Event e{};
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = k.key == KEY_F9 ? SDLK_F9 : SDLK_F10;
            e.key.keysym.scancode = k.key == KEY_F9 ? SDL_SCANCODE_F9 : SDL_SCANCODE_F10;
            SDL_PushEvent(&e);
            continue;
        }
        if (!k.down_done && t >= k.at && k.key > 0 && k.key < KEY_MAX) {
            k.down_done = true;
            key[k.key] = 1;
            int ascii = k.key == KEY_ENTER ? 13 : k.key == KEY_ESC ? 27 : k.key == KEY_SPACE ? ' ' : 0;
            g_keybuf.push_back((k.key << 8) | ascii);
        }
        if (k.down_done && !k.up_done && t >= k.at + k.hold) {
            k.up_done = true;
            key[k.key] = 0;
        }
    }
    if (g_script_quit_at && t >= g_script_quit_at) g_quit = true;
}

void save_bmp(const BITMAP *b, const char *path) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return;
    int row = (b->w * 3 + 3) & ~3;
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&h](int at, uint32_t v) { for (int i = 0; i < 4; i++) h[at + i] = uint8_t(v >> (8 * i)); };
    put32(2, 54 + static_cast<uint32_t>(row) * b->h); put32(10, 54); put32(14, 40); put32(18, b->w); put32(22, b->h);
    h[26] = 1; h[28] = 24;
    std::fwrite(h, 1, 54, f);
    std::vector<uint8_t> line(static_cast<size_t>(row), 0);
    for (int y = b->h - 1; y >= 0; y--) {
        for (int x = 0; x < b->w; x++) {
            int c = b->line[y][x];
            line[x * 3] = uint8_t(getb(c)); line[x * 3 + 1] = uint8_t(getg(c)); line[x * 3 + 2] = uint8_t(getr(c));
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
}

void present_bitmap(BITMAP *bmp) {
    if (bmp && !g_script_shots.empty()) {
        uint32_t t = SDL_GetTicks() - g_script_t0;
        for (ScriptShot &s : g_script_shots)
            if (!s.done && t >= s.at) { s.done = true; save_bmp(bmp, s.path.c_str()); }
    }
    if (!g_renderer || !g_scaler_ready || !bmp) return;
    if (!g_script_scaled_shots.empty()) {
        uint32_t t = SDL_GetTicks() - g_script_t0;
        for (ScriptShot &s : g_script_scaled_shots)
            if (!s.done && t >= s.at) { s.done = true; g_scaler.capture_next(s.path.c_str()); break; }
    }
    g_scaler.present(bmp, g_scale_mode);
}

void destroy_display() {
    g_scaler.shutdown();
    g_scaler_ready = false;
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    g_renderer = nullptr;
    g_window = nullptr;
    if (screen) destroy_bitmap(screen);
    screen = nullptr;
    g_shown = nullptr;
    gfx_driver = nullptr;
}

}  // namespace

// ------------------------------------------------------------------ sistema
int install_allegro(int, int *, int (*)(void (*)(void))) {
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        std::snprintf(allegro_error, sizeof allegro_error, "%s", SDL_GetError());
        return -1;
    }
    return 0;
}

void allegro_exit(void) {
    g_timer_running = false;
    if (g_timer_thread.joinable()) g_timer_thread.join();
    {
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        g_timers.clear();
    }
    remove_sound();
    for (Pad &p : g_pads) {
        if (p.gc) SDL_GameControllerClose(p.gc);
        if (p.js) SDL_JoystickClose(p.js);
    }
    g_pads.clear();
    destroy_display();
    SDL_Quit();
}

void allegro_message(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", buf);
    if (!platform_is_handheld()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Head Over Heels", buf, g_window);
}

bool platform_set_clipboard_text(const char *text) {
    return text && SDL_SetClipboardText(text) == 0;
}

std::string platform_base_path(void) {
    char *p = SDL_GetBasePath();
    if (!p) return "";
    std::string s = p;
    SDL_free(p);
    return s;
}

bool platform_is_handheld(void) {
    const char *forced = std::getenv("HOH_HANDHELD");
    if (forced) return std::atoi(forced) != 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) SDL_InitSubSystem(SDL_INIT_VIDEO);
    const char *drv = SDL_GetCurrentVideoDriver();
    return drv && std::strcmp(drv, "KMSDRM") == 0;
}

// ------------------------------------------------------------------ video
void set_color_depth(int) {}  // so 16 bpp

int set_gfx_mode(int card, int w, int h, int, int) {
    destroy_display();
    if (card == GFX_TEXT) return 0;
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        std::snprintf(allegro_error, sizeof allegro_error, "%s", SDL_GetError());
        return -1;
    }
    bool handheld = platform_is_handheld();
    const char *env = std::getenv("HOH_FULLSCREEN");
    g_fullscreen = handheld || (env ? std::atoi(env) != 0 : card == GFX_AUTODETECT_FULLSCREEN || card == GFX_DIRECTX);

    // Janela 4:3 com ~90% da altura util da tela (o scaler cuida de qualquer tamanho).
    int win_w = w, win_h = h;
    SDL_Rect usable;
    if (!handheld && SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        double sc = std::max(1.0, usable.h * 0.90 / h);
        if (w * sc > usable.w * 0.95) sc = std::max(1.0, usable.w * 0.95 / w);
        win_w = static_cast<int>(w * sc);
        win_h = static_cast<int>(h * sc);
    }
    if (const char *ws = std::getenv("HOH_WINDOW_SIZE")) std::sscanf(ws, "%dx%d", &win_w, &win_h);  // testes
    load_port_cfg();
    if (handheld) g_scale_mode = ScaleMode::Nearest;  // tela 640x480 nativa: 1:1
    // fps: auto tambem no console (no R36S fica liso a 60; o auto cai para 30 se nao acompanhar)
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (g_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_window = SDL_CreateWindow("Head Over Heels", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, flags);
    if (!g_window) {
        std::snprintf(allegro_error, sizeof allegro_error, "%s", SDL_GetError());
        return -1;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        std::snprintf(allegro_error, sizeof allegro_error, "%s", SDL_GetError());
        destroy_display();
        return -1;
    }
    g_scaler_ready = g_scaler.init(g_renderer, w, h);
    if (!g_scaler_ready) {
        std::snprintf(allegro_error, sizeof allegro_error, "%s", SDL_GetError());
        destroy_display();
        return -1;
    }
    if (handheld) SDL_ShowCursor(SDL_DISABLE);
    query_refresh_rate();
    update_window_title();
    screen = create_bitmap(w, h);
    g_shown = screen;
    g_driver.id = card;
    g_driver.windowed = !g_fullscreen;
    g_driver.w = w;
    g_driver.h = h;
    gfx_driver = &g_driver;
    gfx_capabilities = GFX_CAN_TRIPLE_BUFFER;
    present_bitmap(screen);
    return 0;
}

int enable_triple_buffer(void) { return 0; }

int request_video_bitmap(BITMAP *bitmap) {
    g_shown = bitmap;
    platform_pump();
    present_bitmap(g_shown);
    return 0;
}

int show_video_bitmap(BITMAP *bitmap) { return request_video_bitmap(bitmap); }
int poll_scroll(void) { return 0; }

// No Allegro, vsync() so espera o retraco vertical; quem mostra o quadro e o laco do jogo
// (platform_present / show_video_bitmap). O ritmo vem do present com PRESENTVSYNC.
void vsync(void) { platform_pump(); }

void platform_present(void) { present_bitmap(g_shown); }

void platform_toggle_fullscreen(void) {
    if (!g_window || platform_is_handheld()) return;
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(g_window, g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (gfx_driver) gfx_driver->windowed = !g_fullscreen;
    query_refresh_rate();
    pacer_reset(true);  // em tela cheia o vsync pode se comportar diferente
}

bool platform_is_fullscreen(void) { return g_fullscreen; }
bool platform_smooth_enabled(void) { return g_fps_mode != kFpsOff; }
int platform_fps_mode(void) { return g_fps_mode; }
int platform_fps_target(void) {
    if (g_fps_mode == kFpsOff) return 30;
    return static_cast<int>(std::lround(static_cast<double>(g_pacer.refresh) / pacer_divisor()));
}
void platform_cycle_fps_mode(int dir) {
    cycle_fps_mode(dir);
    save_port_cfg();
    update_window_title();
}

void platform_smooth_frame_begin(void) { g_pacer.begin = SDL_GetPerformanceCounter(); }

void platform_smooth_frame_end(void) {
    FramePacer &p = g_pacer;
    const double f = static_cast<double>(SDL_GetPerformanceFrequency());
    const int d = pacer_divisor();
    const double frame_s = static_cast<double>(d) / p.refresh;  // duracao de um quadro novo
    const uint64_t submit = g_scaler.submit_time();
    const double work_ms = submit > p.begin ? (submit - p.begin) * 1000.0 / f : 0.0;

    if (p.vsync != FramePacer::Vsync::Off) {
        for (int i = 1; i < d; i++) g_scaler.present_again();  // cada present espera um retraco
    } else {
        const uint64_t step = static_cast<uint64_t>(frame_s * f);
        const uint64_t now = SDL_GetPerformanceCounter();
        uint64_t target = p.deadline + step;
        if (p.deadline == 0 || now - p.deadline > static_cast<uint64_t>(0.25 * f)) target = p.begin + step;
        else if (now > target + step) target = now;  // atrasou mais de um quadro: recomeca daqui
        sleep_until(target);
        p.deadline = target;
    }

    const uint64_t end = SDL_GetPerformanceCounter();
    const double interval = p.last_end ? (end - p.last_end) / f : 1.0;
    p.last_end = end;
    if (interval >= 0.25) return;  // primeiro quadro ou volta de menu/pausa: nada a medir
    if (p.win_t0 == 0) p.win_t0 = end;
    p.win_frames++;
    p.win_interval += interval;
    const double vblanks = interval * p.refresh;  // com vsync, sempre perto de um inteiro >= 1
    p.win_offgrid += vblanks < 0.5 ? 0.5 : std::fabs(vblanks - std::round(vblanks));
    if (interval > (d + 0.5) / p.refresh) p.win_misses++;  // ficou um retraco a mais na tela
    p.win_work += work_ms;
    p.win_work_max = std::max(p.win_work_max, work_ms);

    const bool measuring = p.vsync == FramePacer::Vsync::Unknown;
    if (!(measuring ? p.win_frames >= 30 : (end - p.win_t0) / f >= 1.0)) return;
    const double avg_interval = p.win_interval / p.win_frames;
    const double avg_work = p.win_work / p.win_frames;
    const double avg_offgrid = p.win_offgrid / p.win_frames;
    // Com vsync os quadros saem em multiplos inteiros do retraco e nunca mais rapido que o alvo;
    // fora da grade (ou bem mais rapido) = o vsync nao segura e o relogio assume.
    if (p.vsync != FramePacer::Vsync::Off) {
        const bool on_grid = avg_offgrid < 0.12 && avg_interval >= 0.75 * frame_s;
        p.vsync = on_grid ? FramePacer::Vsync::On : FramePacer::Vsync::Off;
    }
    bool changed = false;
    if (g_fps_mode == kFpsAuto && !measuring) {
        const uint64_t now = SDL_GetPerformanceCounter();
        if (p.win_misses * 10 > p.win_frames && p.refresh / (p.auto_divisor + 1) >= 30) {
            p.auto_divisor++;  // perdeu mais de 10% dos retracos: metade da taxa (120 -> 60)
            p.hold_until = now + static_cast<uint64_t>(p.backoff_s * f);
            p.backoff_s = std::min(p.backoff_s * 2, 60.0);
            changed = true;
        } else if (p.auto_divisor > 1 && now >= p.hold_until) {
            const double faster_ms = 1000.0 * (p.auto_divisor - 1) / p.refresh;
            if (avg_work < 0.6 * faster_ms && p.win_work_max < 0.9 * faster_ms) {
                p.auto_divisor--;  // folga de sobra: tenta a taxa maior de novo
                p.hold_until = now + static_cast<uint64_t>(3 * f);
                changed = true;
            }
        }
    }
    if (const char *prof = std::getenv("HOH_PROFILE")) {
        if (FILE *pf = std::fopen(prof, "a")) {
            std::fprintf(pf, "[ritmo] monitor %d Hz  alvo %.1f/s (divisor %d)  vsync %s  quadros %d  perdidos %d  "
                             "intervalo %.2f ms (fora da grade %.2f)  trabalho %.2f ms (max %.2f)\n",
                         p.refresh, static_cast<double>(p.refresh) / d, d,
                         p.vsync == FramePacer::Vsync::On ? "segura" : "nao segura (relogio)", p.win_frames,
                         p.win_misses, avg_interval * 1000.0, avg_offgrid, avg_work, p.win_work_max);
            std::fclose(pf);
        }
    }
    pacer_reset_window();
    if (changed) {
        p.deadline = 0;
        update_window_title();
    }
}
bool platform_quit_requested(void) { return g_quit; }

// ------------------------------------------------------------------ eventos
void platform_pump(void) {
    script_tick();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: g_quit = true; break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    if (!e.key.repeat) platform_toggle_fullscreen();
                    break;
                }
                if (e.key.keysym.sym == SDLK_F11) {
                    if (!e.key.repeat) platform_toggle_fullscreen();
                    break;
                }
                if (e.key.keysym.sym == SDLK_F9) {  // port: quadros interpolados
                    if (!e.key.repeat) {
                        cycle_fps_mode(1);
                        save_port_cfg();
                        update_window_title();
                    }
                    break;
                }
                if (e.key.keysym.sym == SDLK_F10 && !platform_is_handheld()) {  // port: modo de escala
                    if (!e.key.repeat) {
                        g_scale_mode = static_cast<ScaleMode>((static_cast<int>(g_scale_mode) + 1) %
                                                              static_cast<int>(ScaleMode::Count));
                        save_port_cfg();
                        update_window_title();
                        present_bitmap(g_shown);
                    }
                    break;
                }
                if (int k = map_scancode(e.key.keysym.scancode)) {
                    key[k] = 1;
                    if (g_keybuf.size() < 64) g_keybuf.push_back((k << 8) | ascii_for(e.key.keysym));
                }
                break;
            case SDL_KEYUP:
                if (int k = map_scancode(e.key.keysym.scancode)) key[k] = 0;
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_JOYDEVICEADDED:
                if (g_joy_installed) open_pad(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEREMOVED:
                if (g_joy_installed) close_pad(e.cdevice.which);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_EXPOSED) present_bitmap(g_shown);
                if (e.window.event == SDL_WINDOWEVENT_MOVED || e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
                    query_refresh_rate();  // a janela pode ter ido para outro monitor
                break;
            default: break;
        }
    }
}

int install_keyboard(void) { return 0; }

int keypressed(void) {
    platform_pump();
    return !g_keybuf.empty();
}

int readkey(void) {
    while (g_keybuf.empty()) {
        platform_pump();
        if (g_quit) return (KEY_ESC << 8) | 27;
        SDL_Delay(1);
    }
    int k = g_keybuf.front();
    g_keybuf.pop_front();
    return k;
}

void clear_keybuf(void) { g_keybuf.clear(); }

// ------------------------------------------------------------------ joystick
int install_joystick(int) {
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    g_joy_installed = true;
    for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
    refresh_joystick_info();
    return 0;
}

int poll_joystick(void) {
    platform_pump();
    for (int i = 0; i < num_joysticks; i++) {
        Pad &p = g_pads[i];
        JOYSTICK_INFO &j = joy[i];
        int x = 0, y = 0;
        if (p.gc) {
            x = SDL_GameControllerGetAxis(p.gc, SDL_CONTROLLER_AXIS_LEFTX) / 256;
            y = SDL_GameControllerGetAxis(p.gc, SDL_CONTROLLER_AXIS_LEFTY) / 256;
            if (SDL_GameControllerGetButton(p.gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) x = -128;
            if (SDL_GameControllerGetButton(p.gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) x = 128;
            if (SDL_GameControllerGetButton(p.gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) y = -128;
            if (SDL_GameControllerGetButton(p.gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) y = 128;
            int b = 0;
            for (SDL_GameControllerButton btn : kPadButtons) j.button[b++].b = SDL_GameControllerGetButton(p.gc, btn);
            j.button[b++].b = SDL_GameControllerGetAxis(p.gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384;
            j.button[b++].b = SDL_GameControllerGetAxis(p.gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384;
        } else {
            if (SDL_JoystickNumAxes(p.js) >= 2) {
                x = SDL_JoystickGetAxis(p.js, 0) / 256;
                y = SDL_JoystickGetAxis(p.js, 1) / 256;
            }
            if (SDL_JoystickNumHats(p.js) > 0) {
                Uint8 hat = SDL_JoystickGetHat(p.js, 0);
                if (hat & SDL_HAT_LEFT) x = -128;
                if (hat & SDL_HAT_RIGHT) x = 128;
                if (hat & SDL_HAT_UP) y = -128;
                if (hat & SDL_HAT_DOWN) y = 128;
            }
            for (int b = 0; b < j.num_buttons; b++) j.button[b].b = SDL_JoystickGetButton(p.js, b);
        }
        set_axis(j.stick[0].axis[0], x);
        set_axis(j.stick[0].axis[1], y);
    }
    return 0;
}

// ------------------------------------------------------------------ timers
int install_int_ex(void (*proc)(void), long speed) {
    if (!proc || speed <= 0) return -1;
    uint64_t period = static_cast<uint64_t>(speed) * SDL_GetPerformanceFrequency() / TIMERS_PER_SECOND;
    {
        std::lock_guard<std::mutex> lock(g_timer_mutex);
        uint64_t now = SDL_GetPerformanceCounter();
        auto it = std::find_if(g_timers.begin(), g_timers.end(), [proc](const Timer &t) { return t.proc == proc; });
        if (it != g_timers.end()) it->period = std::max<uint64_t>(1, period);  // Allegro: reinstalar so muda a velocidade
        else g_timers.push_back({proc, std::max<uint64_t>(1, period), now + period});
    }
    if (!g_timer_running.exchange(true)) g_timer_thread = std::thread(timer_loop);
    return 0;
}

void remove_int(void (*proc)(void)) {
    std::lock_guard<std::mutex> lock(g_timer_mutex);
    g_timers.erase(std::remove_if(g_timers.begin(), g_timers.end(), [proc](const Timer &t) { return t.proc == proc; }),
                   g_timers.end());
}

void rest(unsigned int ms) {
    uint32_t end = SDL_GetTicks() + ms;
    do {
        platform_pump();
        SDL_Delay(1);
    } while (static_cast<int32_t>(end - SDL_GetTicks()) > 0);
}
