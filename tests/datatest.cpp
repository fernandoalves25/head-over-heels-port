// Valida o leitor dos dados contra os arquivos originais do jogo.
// Uso: hoh-datatest <pasta_do_jogo>   (a pasta que contem HoHOriginal.dat e Sound/)
#include "compat/allegro.h"
#include "compat/packfile_codec.h"
#include "data/hohdata.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

static void test_lzss_roundtrip() {
    std::mt19937 rng(1234);
    for (int round = 0; round < 50; round++) {
        size_t n = rng() % 20000;
        std::vector<uint8_t> v(n);
        int mode = round % 3;
        for (auto &b : v) b = static_cast<uint8_t>(mode == 0 ? rng() : mode == 1 ? rng() % 4 : (rng() % 50 ? 0 : rng()));
        auto packed = packfile_codec::lzss_pack(v.data(), v.size());
        auto back = packfile_codec::lzss_unpack(packed.data(), packed.size());
        CHECK(back == v);
    }
    // Arquivo inteiro: encode -> decode com a senha do jogo
    std::vector<uint8_t> content = {'H', 'o', 'H', 0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3};
    auto file = packfile_codec::encode(content, hoh::kDataPassword, true);
    std::vector<uint8_t> out;
    CHECK(packfile_codec::decode(file, hoh::kDataPassword, true, out));
    CHECK(out == content);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("uso: %s <pasta_do_jogo>\n", argv[0]);
        return 2;
    }
    std::string dir = argv[1];

    test_lzss_roundtrip();

    // Magic dos arquivos originais = F_PACK_MAGIC mascarado (0x76373E6A)
    CHECK(packfile_codec::encrypt_id(packfile_codec::F_PACK_MAGIC, hoh::kDataPassword) == 0x76373E6Au);

    hoh::GameData gd;
    std::string err;
    bool ok = hoh::load_game_data(dir + "/HoHOriginal.dat", gd, &err);
    if (!ok) std::printf("erro: %s\n", err.c_str());
    CHECK(ok);
    if (ok) {
        size_t block_frames = 0;
        for (auto &b : gd.blocks)
            for (auto &d : b.dirs) block_frames += d.frames.size();
        std::printf("atributos %zu, blocos %zu (%zu quadros), moveis %zu, nomeados %zu, mundos %zu, imagens %zu\n",
                    gd.attr_names.size(), gd.blocks.size(), block_frames, gd.mobiles.size(), gd.named.size(),
                    gd.worlds.size(), gd.images.size());
        CHECK(gd.attr_names.size() == 35);
        CHECK(gd.attr_names.front() == "OBJ-Gravity");
        CHECK(gd.attr_names.back() == "ROOM-Vertical Scroll");
        CHECK(gd.blocks.size() == 79);
        CHECK(block_frames == 360);
        CHECK(gd.mobiles.size() == 105);
        CHECK(gd.named.size() == 161);
        CHECK(gd.named.front().name == "Dungeon");
        CHECK(gd.worlds.size() == 8);
        CHECK(gd.images.size() == 40);
        CHECK(gd.images[17]->w == 640 && gd.images[17]->h == 480);  // tela-titulo
        CHECK(gd.room_count == 276);
        CHECK(gd.start_room == 1407);

        // Todas as salas: decodificam, sao contiguas e terminam onde a tabela comeca.
        std::vector<std::pair<size_t, size_t>> spans;
        int rooms = 0;
        for (int id = 0; id < hoh::kRoomGrid; id++) {
            if (gd.room_table[id] < 0) continue;
            hoh::RoomRecord rr;
            std::string rerr;
            bool rok = hoh::parse_room(gd, id, rr, &rerr);
            if (!rok) std::printf("sala %d: %s\n", id, rerr.c_str());
            CHECK(rok);
            CHECK(rr.id == id);
            spans.push_back({rr.begin_offset, rr.end_offset});
            rooms++;
        }
        std::sort(spans.begin(), spans.end());
        bool contiguous = !spans.empty() && spans.front().first == 0;
        for (size_t i = 1; i < spans.size(); i++) contiguous = contiguous && spans[i - 1].second == spans[i].first;
        CHECK(rooms == 276);
        CHECK(contiguous);
        CHECK(!spans.empty() && spans.back().second == gd.room_blob.size() - hoh::kRoomGrid * 4 - 4);
        std::printf("salas %d (contiguas: %s), sala inicial %d\n", rooms, contiguous ? "sim" : "nao", gd.start_room);
    }

    packfile_password(hoh::kDataPassword);
    DATAFILE *snd = load_datafile((dir + "/Sound/Sound.dat").c_str());
    packfile_password(nullptr);
    CHECK(snd != nullptr);
    if (snd) {
        int samples = 0;
        for (DATAFILE *d = snd; d->type != DAT_END; d++) samples += d->type == DAT_SAMPLE;
        std::printf("Sound.dat: %d samples, primeiro = %s\n", samples, get_datafile_property(snd, DAT_NAME));
        CHECK(samples == 46);
        CHECK(std::string(get_datafile_property(snd, DAT_NAME)) == "Sample_Bat");
        const SAMPLE *fanfare = nullptr;
        for (DATAFILE *d = snd; d->type != DAT_END; d++)
            if (std::string(get_datafile_property(d, DAT_NAME)) == "Sample_Fanfare") fanfare = static_cast<SAMPLE *>(d->dat);
        CHECK(fanfare && fanfare->bits == 16 && fanfare->stereo && fanfare->freq == 22050 && fanfare->len == 66152);
        unload_datafile(snd);
    }

    std::printf(g_failures ? "%d falha(s)\n" : "OK\n", g_failures);
    return g_failures ? 1 : 0;
}
