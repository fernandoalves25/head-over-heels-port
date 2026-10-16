#include "data/hohdata.h"

#include <cstring>
#include <stdexcept>

namespace hoh {

namespace {

struct Reader {
    const uint8_t *p;
    size_t n, pos;
    Reader(const uint8_t *data, size_t size, size_t start = 0) : p(data), n(size), pos(start) {}
    void need(size_t k) const {
        if (pos + k > n) throw std::runtime_error("fim inesperado dos dados no offset " + std::to_string(pos));
    }
    int32_t i32() {
        need(4);
        uint32_t v = uint32_t(p[pos]) | (uint32_t(p[pos + 1]) << 8) | (uint32_t(p[pos + 2]) << 16) | (uint32_t(p[pos + 3]) << 24);
        pos += 4;
        return static_cast<int32_t>(v);
    }
    uint8_t u8() {
        need(1);
        return p[pos++];
    }
    int32_t count(int32_t limit) {
        int32_t c = i32();
        if (c < 0 || c > limit) throw std::runtime_error("contagem invalida " + std::to_string(c) + " no offset " + std::to_string(pos - 4));
        return c;
    }
    std::string str() {
        int32_t len = count(1 << 16);
        need(static_cast<size_t>(len));
        std::string s(reinterpret_cast<const char *>(p + pos), static_cast<size_t>(len));
        pos += static_cast<size_t>(len);
        return s;
    }
    BITMAP *image(int32_t w, int32_t h) {
        if (w < 0 || h < 0 || w > 4096 || h > 4096) throw std::runtime_error("dimensao de imagem invalida");
        size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2;
        need(bytes);
        BITMAP *bmp = create_bitmap(w, h);
        const uint8_t *src = p + pos;
        for (size_t i = 0; i < bytes / 2; i++) bmp->dat[i] = static_cast<uint16_t>(src[i * 2] | (src[i * 2 + 1] << 8));
        pos += bytes;
        return bmp;
    }
};

std::vector<BehaviourEntry> read_behaviour(Reader &r) {
    std::vector<BehaviourEntry> t(static_cast<size_t>(r.count(4096)));
    for (auto &e : t) {
        e.attr = r.i32();
        for (auto &v : e.param) v = r.i32();
    }
    return t;
}

void read_sprite_body(Reader &r, SpriteDef &s) {
    int32_t nframes = r.count(4096);
    s.v_c0 = r.i32();
    s.pingpong = r.u8();
    s.repeat = r.i32();
    s.w = r.i32();
    s.h = r.i32();
    for (int32_t i = 0; i < nframes; i++) s.frames.push_back(r.image(s.w, s.h));
}

FramePair read_pair(Reader &r) {
    int32_t w = r.i32(), h1 = r.i32(), h2 = r.i32();
    FramePair fp;
    fp.a = r.image(w, h1);
    fp.b = r.image(w, h2);
    return fp;
}

void parse_all(const std::vector<uint8_t> &d, GameData &out) {
    Reader r(d.data(), d.size());

    for (int32_t i = 0, n = r.count(1024); i < n; i++) out.attr_names.push_back(r.str());

    for (int32_t i = 0, n = r.count(4096); i < n; i++) {
        BlockDef b;
        b.v24 = r.i32();
        b.anim_ref = r.i32();
        b.multi = r.u8();
        b.ndirs_raw = r.i32();
        int32_t ndirs = b.multi ? b.ndirs_raw : 1;
        if (ndirs < 0 || ndirs > 64) throw std::runtime_error("numero de direcoes invalido");
        for (int32_t k = 0; k < ndirs; k++) {
            BlockDir dir;
            dir.nframes = r.count(4096);
            dir.vc8 = r.i32();
            dir.pingpong = r.u8();
            dir.repeat = r.i32();
            dir.w = r.i32();
            dir.h1 = r.i32();
            dir.h2 = r.i32();
            for (int32_t f = 0; f < dir.nframes; f++) {
                FramePair fp;
                fp.a = r.image(dir.w, dir.h1);
                fp.b = r.image(dir.w, dir.h2);
                dir.frames.push_back(fp);
            }
            b.dirs.push_back(std::move(dir));
        }
        b.beh_a = r.u8();
        b.beh_b = r.u8();
        b.has_behaviour = b.beh_b == 0 || b.beh_a != 0;  // read_behaviour_table @0x402d10
        if (b.has_behaviour) b.behaviour = read_behaviour(r);
        out.blocks.push_back(std::move(b));
    }

    for (int32_t i = 0, n = r.count(4096); i < n; i++) {
        SpriteDef m;
        m.f0 = r.u8();
        m.directional = r.u8();
        read_sprite_body(r, m);
        out.mobiles.push_back(std::move(m));
    }

    for (int32_t i = 0, n = r.count(4096); i < n; i++) {
        SpriteDef s;
        s.name = r.str();
        s.directional = r.u8();
        read_sprite_body(r, s);
        out.named.push_back(std::move(s));
    }

    for (int32_t i = 0, n = r.count(256); i < n; i++) {
        WorldDef w;
        w.v0 = r.i32();
        int32_t mw = r.i32(), mh = r.i32();
        w.main_image = r.image(mw, mh);
        for (auto &part : w.parts)
            for (auto &fp : part) fp = read_pair(r);
        out.worlds.push_back(w);
    }

    for (int32_t i = 0, n = r.count(4096); i < n; i++) {
        int32_t w = r.i32(), h = r.i32();
        out.images.push_back(r.image(w, h));
    }

    out.room_count = r.i32();
    size_t blob_len = d.size() - r.pos;
    if (blob_len >= kRoomBlobMax) throw std::runtime_error("blob de salas grande demais (o original sairia)");
    if (blob_len < kRoomGrid * 4 + 4) throw std::runtime_error("blob de salas curto demais");
    out.room_blob.assign(d.begin() + static_cast<long>(r.pos), d.end());
    Reader t(out.room_blob.data(), out.room_blob.size(), blob_len - kRoomGrid * 4 - 4);
    out.start_room = t.i32();  // 0x91e448
    for (auto &off : out.room_table) off = t.i32();  // 0x43bda4
}

}  // namespace

GameData::~GameData() {
    for (auto &b : blocks)
        for (auto &d : b.dirs)
            for (auto &f : d.frames) { destroy_bitmap(f.a); destroy_bitmap(f.b); }
    for (auto *list : {&mobiles, &named})
        for (auto &s : *list)
            for (BITMAP *f : s.frames) destroy_bitmap(f);
    for (auto &w : worlds) {
        destroy_bitmap(w.main_image);
        for (auto &part : w.parts)
            for (auto &fp : part) { destroy_bitmap(fp.a); destroy_bitmap(fp.b); }
    }
    for (BITMAP *b : images) destroy_bitmap(b);
}

bool load_game_data(const std::string &path, GameData &out, std::string *error) {
    packfile_password(kDataPassword);
    PACKFILE *f = pack_fopen(path.c_str(), "rbp");
    packfile_password(nullptr);
    if (!f) {
        if (error) *error = "nao consegui abrir/decodificar " + path;
        return false;
    }
    std::vector<uint8_t> d;
    uint8_t chunk[65536];
    long k;
    while ((k = pack_fread(chunk, sizeof chunk, f)) > 0) d.insert(d.end(), chunk, chunk + k);
    pack_fclose(f);
    try {
        parse_all(d, out);
    } catch (const std::exception &e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

bool parse_room(const GameData &data, int room_id, RoomRecord &R, std::string *error) {
    if (room_id < 0 || room_id >= kRoomGrid || data.room_table[room_id] < 0) {
        if (error) *error = "sala inexistente: " + std::to_string(room_id);
        return false;
    }
    try {
        Reader r(data.room_blob.data(), data.room_blob.size(), static_cast<size_t>(data.room_table[room_id]));
        R = RoomRecord{};
        R.begin_offset = r.pos;
        R.id = r.i32();
        R.a = r.i32();
        R.b = r.i32();
        for (auto &v : R.geo) v = r.i32();
        for (auto &v : R.flags7) v = r.i32();
        R.v415f50 = r.i32();
        R.floor_mobile = r.i32();
        R.floor_items.resize(static_cast<size_t>(r.count(4096)));
        for (auto &it : R.floor_items)
            for (auto &v : it.v) v = r.i32();
        for (auto &wall : R.walls) {
            wall.named = r.i32();
            wall.items.resize(static_cast<size_t>(r.count(4096)));
            for (auto &it : wall.items)
                for (auto &v : it.v) v = r.i32();
        }
        R.objects.resize(static_cast<size_t>(r.count(4096)));
        for (auto &o : R.objects) {
            o.block = r.i32();
            o.x = r.i32();
            o.y = r.i32();
            o.z = r.i32();
            o.alt = r.u8();
            o.image = r.i32();
            o.image_a = r.i32();
            o.image_b = r.i32();
            o.f1 = r.u8();
            o.v = r.i32();
            o.f2 = r.u8();
            o.beh_a = r.u8();  // lido e ignorado pelo original
            o.beh_b = r.u8();
            o.has_behaviour = o.beh_b == 0;
            if (o.has_behaviour) o.behaviour = read_behaviour(r);
        }
        for (auto &e : R.exits) {
            e.u = r.i32();
            for (auto &s : e.side)
                for (auto &v : s) v = r.i32();
        }
        R.v410 = r.i32();
        R.extras.resize(static_cast<size_t>(r.count(4096)));
        for (auto &x : R.extras) {
            for (auto &v : x.v) v = r.i32();
            x.flag = r.u8();
        }
        R.end_offset = r.pos;
    } catch (const std::exception &e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

}  // namespace hoh
