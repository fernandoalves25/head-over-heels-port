// Ponto de entrada do port. Trata as opcoes proprias do port e chama game_main (0x40f050) com o
// resto da linha de comando, como o WinMain do Allegro fazia no original.
//
//   hoh [--data PASTA] [--fullscreen | --window] [opcoes do original: -r0..-r4, -b, arquivo.dat]
#include "compat/platform.h"
#include "game/data_bridge.h"
#include "game/main_loop.h"

#include <SDL.h>  // SDL_main no Windows

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

static bool file_exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

int main(int argc, char **argv) {
    std::vector<char *> args = {argv[0]};
    std::string data_dir;
    static char r_full[] = "-r1", r_window[] = "-r2";  // modos do original (ver game_main)
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--fullscreen") args.push_back(r_full);
        else if (a == "--window") args.push_back(r_window);
        else args.push_back(argv[i]);
    }
    if (data_dir.empty())
        if (const char *env = std::getenv("HOH_DATA")) data_dir = env;
    if (data_dir.empty()) {
        // Procura os dados do jogo original: pasta do executavel, pasta atual, data/ ao lado.
        for (const std::string &c : {platform_base_path(), std::string("./"), platform_base_path() + "data/"})
            if (file_exists(c + "HoHOriginal.dat")) { data_dir = c; break; }
    }
    // O original assume que a pasta atual e a pasta do jogo (Config.dat, Save%i.dat e os .dat sao
    // abertos por caminho relativo). O port entra nela e usa caminhos relativos a partir dai.
    if (!data_dir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(data_dir, ec);
        if (ec) std::fprintf(stderr, "nao consegui entrar em %s: %s\n", data_dir.c_str(), ec.message().c_str());
    }
    hoh::g_data_dir = "";
    return hoh::game_main(static_cast<int>(args.size()), args.data());
}
