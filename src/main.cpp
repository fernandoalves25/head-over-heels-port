// Marco 1 do port: tela-titulo + musica + navegador de sprites/sons lidos dos dados originais.
// Ainda nao e o jogo: serve para validar a camada de plataforma (video, entrada, timer, audio)
// e o leitor dos .dat em Windows, Linux e no R36S.
#include "compat/allegro.h"
#include "compat/platform.h"
#include "data/hohdata.h"

#include <SDL.h>  // para SDL_main no Windows

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

volatile int g_ticks = 0;
void tick() { g_ticks = g_ticks + 1; }

bool file_exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

std::string find_data_dir(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--data") return std::string(argv[i + 1]) + "/";
    if (const char *env = std::getenv("HOH_DATA")) return std::string(env) + "/";
    std::vector<std::string> candidates = {platform_base_path(), "./", platform_base_path() + "data/"};
    for (const std::string &c : candidates)
        if (file_exists(c + "HoHOriginal.dat")) return c;
    return "";
}

// Entrada com deteccao de borda (teclado + controle 0).
struct Input {
    bool prev[16] = {}, cur[16] = {};
    enum { UP, DOWN, LEFT, RIGHT, NEXT, BACK, SOUND, MUSIC, COUNT };
    void update() {
        poll_joystick();
        for (int i = 0; i < COUNT; i++) prev[i] = cur[i];
        const JOYSTICK_INFO *j = num_joysticks > 0 ? &joy[0] : nullptr;
        auto btn = [j](int b) { return j && b < j->num_buttons && j->button[b].b; };
        cur[UP] = key[KEY_UP] || (j && j->stick[0].axis[1].d1);
        cur[DOWN] = key[KEY_DOWN] || (j && j->stick[0].axis[1].d2);
        cur[LEFT] = key[KEY_LEFT] || (j && j->stick[0].axis[0].d1);
        cur[RIGHT] = key[KEY_RIGHT] || (j && j->stick[0].axis[0].d2);
        cur[NEXT] = key[KEY_ENTER] || key[KEY_SPACE] || key[KEY_TAB] || btn(0);
        cur[BACK] = key[KEY_ESC] || btn(1) || btn(6);
        cur[SOUND] = key[KEY_S] || btn(2);
        cur[MUSIC] = key[KEY_M] || btn(3);
    }
    bool hit(int i) const { return cur[i] && !prev[i]; }
};

const char *kCategories[] = {"BLOCOS", "MOVEIS", "CENARIO", "MUNDOS", "IMAGENS"};

struct Viewer {
    hoh::GameData &gd;
    int cat = 0, item = 0, frame = 0;
    explicit Viewer(hoh::GameData &g) : gd(g) {}

    int item_count() const {
        switch (cat) {
            case 0: return static_cast<int>(gd.blocks.size());
            case 1: return static_cast<int>(gd.mobiles.size());
            case 2: return static_cast<int>(gd.named.size());
            case 3: return static_cast<int>(gd.worlds.size());
            default: return static_cast<int>(gd.images.size());
        }
    }
    // Lista de imagens do item atual (quadros; nos blocos, A e B lado a lado contam como um).
    std::vector<std::pair<BITMAP *, BITMAP *>> frames() const {
        std::vector<std::pair<BITMAP *, BITMAP *>> v;
        switch (cat) {
            case 0:
                for (auto &d : gd.blocks[item].dirs)
                    for (auto &f : d.frames) v.push_back({f.a, f.b});
                break;
            case 1: for (BITMAP *b : gd.mobiles[item].frames) v.push_back({b, nullptr}); break;
            case 2: for (BITMAP *b : gd.named[item].frames) v.push_back({b, nullptr}); break;
            case 3:
                v.push_back({gd.worlds[item].main, nullptr});
                for (auto &part : gd.worlds[item].parts)
                    for (auto &fp : part) v.push_back({fp.a, fp.b});
                break;
            default: v.push_back({gd.images[item], nullptr}); break;
        }
        return v;
    }
    std::string label() const {
        if (cat == 2) return gd.named[item].name;
        return "";
    }
};

// Captura de tela em BMP 24 bits (teste automatico: --frames N --shot arquivo.bmp).
bool save_bmp(BITMAP *b, const char *path) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    int row = (b->w * 3 + 3) & ~3;
    uint32_t size = 54 + static_cast<uint32_t>(row) * b->h;
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&h](int at, uint32_t v) { for (int i = 0; i < 4; i++) h[at + i] = uint8_t(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, b->w); put32(22, b->h);
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
    return true;
}

void draw_scaled(BITMAP *dst, BITMAP *b, int cx, int cy) {
    if (!b || b->w == 0 || b->h == 0) return;
    int scale = 1;
    while (b->w * (scale + 1) <= 300 && b->h * (scale + 1) <= 330 && scale < 4) scale++;
    int w = b->w * scale, h = b->h * scale;
    if (scale == 1) draw_sprite(dst, b, cx - w / 2, cy - h / 2);
    else stretch_sprite(dst, b, cx - w / 2, cy - h / 2, w, h);
}

}  // namespace

int main(int argc, char **argv) {
    if (allegro_init() != 0) return 1;
    install_keyboard();
    install_joystick(JOY_TYPE_AUTODETECT);
    set_color_depth(16);
    bool windowed = true;
    int max_frames = -1, start_cat = -1;
    const char *shot = nullptr;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--fullscreen") windowed = false;
        else if (a == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--browse" && i + 1 < argc) start_cat = std::atoi(argv[++i]);
    }
    if (set_gfx_mode(windowed ? GFX_AUTODETECT_WINDOWED : GFX_AUTODETECT_FULLSCREEN, 640, 480, 0, 0) != 0) {
        allegro_message("Erro ao iniciar o video:\n%s", allegro_error);
        return 1;
    }
    reserve_voices(16, -1);
    if (install_sound(DIGI_AUTODETECT, MIDI_NONE, nullptr) != 0) std::fprintf(stderr, "sem som: %s\n", allegro_error);
    set_volume_per_voice(0);

    std::string dir = find_data_dir(argc, argv);
    hoh::GameData gd;
    std::string err;
    if (dir.empty() || !hoh::load_game_data(dir + "HoHOriginal.dat", gd, &err)) {
        allegro_message("Nao encontrei os dados do jogo original.\n\n"
                        "Copie HoHOriginal.dat e a pasta Sound/ para a pasta do executavel,\n"
                        "ou use --data <pasta> ou a variavel HOH_DATA.\n\n%s",
                        err.c_str());
        allegro_exit();
        return 1;
    }
    packfile_password(hoh::kDataPassword);
    DATAFILE *sounds = load_datafile((dir + "Sound/Sound.dat").c_str());
    packfile_password(nullptr);
    std::vector<const SAMPLE *> samples;
    std::vector<std::string> sample_names;
    if (sounds)
        for (DATAFILE *d = sounds; d->type != DAT_END; d++)
            if (d->type == DAT_SAMPLE) {
                samples.push_back(static_cast<SAMPLE *>(d->dat));
                sample_names.push_back(get_datafile_property(d, DAT_NAME));
            }
    MusicStream *themes[2] = {music_open((dir + "Sound/Theme.ogg").c_str()), music_open((dir + "Sound/Game.ogg").c_str())};
    int theme = 0;
    music_play(themes[theme], 200, true);

    BITMAP *back = create_bitmap(640, 480);
    install_int_ex(tick, BPS_TO_TIMER(60));

    Viewer view(gd);
    Input in;
    bool title = start_cat < 0;
    if (start_cat >= 0) view.cat = start_cat % 5;
    int sample_idx = -1, anim = 0, frames_drawn = 0, fps = 0, fps_timer = 0;
    std::string last_sound;
    BITMAP *title_img = gd.images.size() > 17 ? gd.images[17] : nullptr;

    while (!platform_quit_requested()) {
        while (g_ticks > 0) {  // logica a 60 Hz, como o timer do jogo original
            g_ticks = g_ticks - 1;
            in.update();
            anim++;
            if (++fps_timer >= 60) { fps = frames_drawn; frames_drawn = 0; fps_timer = 0; }
            if (title) {
                if (in.hit(Input::NEXT)) title = false;
                if (in.hit(Input::BACK)) goto done;
            } else {
                int n = view.item_count();
                if (in.hit(Input::BACK)) title = true;
                if (in.hit(Input::NEXT)) { view.cat = (view.cat + 1) % 5; view.item = view.frame = 0; }
                if (in.hit(Input::DOWN) && n) { view.item = (view.item + 1) % n; view.frame = 0; }
                if (in.hit(Input::UP) && n) { view.item = (view.item + n - 1) % n; view.frame = 0; }
                int nf = static_cast<int>(view.frames().size());
                if (in.hit(Input::RIGHT) && nf) view.frame = (view.frame + 1) % nf;
                if (in.hit(Input::LEFT) && nf) view.frame = (view.frame + nf - 1) % nf;
            }
            if (in.hit(Input::SOUND) && !samples.empty()) {
                sample_idx = (sample_idx + 1) % static_cast<int>(samples.size());
                play_sample(samples[sample_idx], 255, 128, 1000, 0);
                last_sound = sample_names[sample_idx];
            }
            if (in.hit(Input::MUSIC)) {
                music_stop(themes[theme]);
                theme ^= 1;
                music_play(themes[theme], 200, true);
            }
        }

        if (title) {
            if (title_img) blit(title_img, back, 0, 0, 0, 0, 640, 480);
            textout(back, font, "ENTER/A: SPRITES   S/X: SOM   M/Y: MUSICA   ESC/B: SAIR", 8, 4, makecol(255, 255, 255));
        } else {
            clear_to_color(back, makecol(40, 44, 64));
            auto frames = view.frames();
            int nf = static_cast<int>(frames.size());
            if (nf) {
                auto [a, b] = frames[view.frame % nf];
                if (b) {
                    draw_scaled(back, a, 170, 250);
                    draw_scaled(back, b, 470, 250);
                } else {
                    draw_scaled(back, a, 320, 250);
                }
                textprintf(back, font, 8, 4, makecol(255, 255, 0), "%s  %d/%d  QUADRO %d/%d  %dX%d %s",
                           kCategories[view.cat], view.item + 1, view.item_count(), view.frame + 1, nf,
                           a ? a->w : 0, a ? a->h : 0, view.label().c_str());
            }
            textout(back, font, "TAB/A: CATEGORIA  CIMA/BAIXO: ITEM  ESQ/DIR: QUADRO  ESC/B: VOLTAR", 8, 466, makecol(200, 200, 200));
        }
        if (!last_sound.empty()) textprintf(back, font, 8, 16, makecol(120, 255, 120), "SOM: %s", last_sound.c_str());
        textprintf(back, font, 584, 472 - (title ? 0 : 12), makecol(255, 255, 255), "FPS %d", fps);
        blit(back, screen, 0, 0, 0, 0, 640, 480);
        vsync();
        frames_drawn++;
        if (max_frames >= 0 && --max_frames <= 0) {
            if (shot) save_bmp(screen, shot);
            break;
        }
    }
done:
    remove_int(tick);
    for (MusicStream *m : themes) music_close(m);
    destroy_bitmap(back);
    unload_datafile(sounds);
    allegro_exit();
    return 0;
}
