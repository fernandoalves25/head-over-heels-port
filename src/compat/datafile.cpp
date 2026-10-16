// load_datafile do Allegro 4.0 (formato "ALL."), suficiente para o Sound/Sound.dat do jogo.
#include "compat/allegro.h"
#include "compat/packfile_codec.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Chamado por destroy_sample para que o mixer pare vozes que usem a amostra (definido em audio).
void (*g_compat_sample_destroy_hook)(const SAMPLE *) = nullptr;

namespace {

struct Cursor {
    const uint8_t *p;
    size_t n, pos = 0;
    bool ok = true;
    uint32_t be32() {
        if (pos + 4 > n) { ok = false; pos = n; return 0; }
        uint32_t v = (uint32_t(p[pos]) << 24) | (uint32_t(p[pos + 1]) << 16) | (uint32_t(p[pos + 2]) << 8) | p[pos + 3];
        pos += 4;
        return v;
    }
    const uint8_t *take(size_t k) {
        if (pos + k > n) { ok = false; pos = n; return nullptr; }
        const uint8_t *r = p + pos;
        pos += k;
        return r;
    }
};

constexpr uint32_t DAT_MAGIC = 0x414C4C2E;  // "ALL."
constexpr uint32_t DAT_PROPERTY = 0x70726F70;  // "prop"

SAMPLE *read_sample(const uint8_t *d, size_t n) {
    if (n < 8) return nullptr;
    int16_t bits = static_cast<int16_t>((d[0] << 8) | d[1]);
    uint16_t freq = static_cast<uint16_t>((d[2] << 8) | d[3]);
    uint32_t len = (uint32_t(d[4]) << 24) | (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 8) | d[7];
    SAMPLE *s = static_cast<SAMPLE *>(std::calloc(1, sizeof(SAMPLE)));
    s->stereo = bits < 0;
    s->bits = bits < 0 ? -bits : bits;
    s->freq = freq;
    s->priority = 128;
    s->len = len;
    s->loop_start = 0;
    s->loop_end = len;
    size_t count = static_cast<size_t>(len) * (s->stereo ? 2 : 1);
    if (s->bits == 8) {
        if (8 + count > n) count = n - 8;
        s->data = std::malloc(count ? count : 1);
        std::memcpy(s->data, d + 8, count);
    } else {
        if (8 + count * 2 > n) count = (n - 8) / 2;
        uint16_t *out = static_cast<uint16_t *>(std::malloc((count ? count : 1) * 2));
        for (size_t i = 0; i < count; i++) out[i] = static_cast<uint16_t>(d[8 + i * 2] | (d[9 + i * 2] << 8));  // pack_igetw
        s->data = out;
    }
    return s;
}

DATAFILE *parse_objects(Cursor &c);

void *load_object(int type, const uint8_t *d, size_t n, long *size_out) {
    *size_out = static_cast<long>(n);
    if (type == DAT_SAMPLE) return read_sample(d, n);
    if (type == DAT_FILE) {
        Cursor sub{d, n};
        if (sub.be32() != DAT_MAGIC) return nullptr;
        return parse_objects(sub);
    }
    void *raw = std::malloc(n ? n : 1);
    std::memcpy(raw, d, n);
    return raw;
}

DATAFILE *parse_objects(Cursor &c) {
    uint32_t count = c.be32();
    if (!c.ok || count > 1000000) return nullptr;
    std::vector<DATAFILE> objs;
    std::vector<DATAFILE_PROPERTY> props;
    while (objs.size() < count && c.ok) {
        uint32_t type = c.be32();
        if (type == DAT_PROPERTY) {
            uint32_t ptype = c.be32(), psize = c.be32();
            const uint8_t *pd = c.take(psize);
            if (!pd) break;
            char *s = static_cast<char *>(std::malloc(psize + 1));
            std::memcpy(s, pd, psize);
            s[psize] = 0;
            props.push_back({s, static_cast<int>(ptype)});
            continue;
        }
        int32_t file_size = static_cast<int32_t>(c.be32());
        int32_t data_size = static_cast<int32_t>(c.be32());
        const uint8_t *body = c.take(static_cast<size_t>(file_size));
        if (!body) break;
        DATAFILE obj{};
        obj.type = static_cast<int>(type);
        if (data_size < 0) {  // sub-bloco comprimido individualmente
            std::vector<uint8_t> plain = packfile_codec::lzss_unpack(body, static_cast<size_t>(file_size));
            obj.dat = load_object(obj.type, plain.data(), plain.size(), &obj.size);
        } else {
            obj.dat = load_object(obj.type, body, static_cast<size_t>(file_size), &obj.size);
        }
        props.push_back({nullptr, DAT_END});
        obj.prop = static_cast<DATAFILE_PROPERTY *>(std::malloc(props.size() * sizeof(DATAFILE_PROPERTY)));
        std::memcpy(obj.prop, props.data(), props.size() * sizeof(DATAFILE_PROPERTY));
        props.clear();
        objs.push_back(obj);
    }
    for (auto &p : props) std::free(p.dat);
    DATAFILE *out = static_cast<DATAFILE *>(std::calloc(objs.size() + 1, sizeof(DATAFILE)));
    std::memcpy(out, objs.data(), objs.size() * sizeof(DATAFILE));
    out[objs.size()].type = DAT_END;
    return out;
}

}  // namespace

DATAFILE *load_datafile(const char *filename) {
    PACKFILE *f = pack_fopen(filename, "rp");
    if (!f) return nullptr;
    std::vector<uint8_t> buf;
    uint8_t chunk[65536];
    long k;
    while ((k = pack_fread(chunk, sizeof chunk, f)) > 0) buf.insert(buf.end(), chunk, chunk + k);
    pack_fclose(f);
    Cursor c{buf.data(), buf.size()};
    if (c.be32() != DAT_MAGIC) return nullptr;
    return parse_objects(c);
}

void destroy_sample(SAMPLE *spl) {
    if (!spl) return;
    if (g_compat_sample_destroy_hook) g_compat_sample_destroy_hook(spl);
    std::free(spl->data);
    std::free(spl);
}

void unload_datafile(DATAFILE *dat) {
    if (!dat) return;
    for (DATAFILE *d = dat; d->type != DAT_END; d++) {
        if (d->type == DAT_SAMPLE) destroy_sample(static_cast<SAMPLE *>(d->dat));
        else if (d->type == DAT_FILE) unload_datafile(static_cast<DATAFILE *>(d->dat));
        else std::free(d->dat);
        if (d->prop) {
            for (DATAFILE_PROPERTY *p = d->prop; p->type != DAT_END; p++) std::free(p->dat);
            std::free(d->prop);
        }
    }
    std::free(dat);
}

const char *get_datafile_property(const DATAFILE *dat, int type) {
    if (dat && dat->prop)
        for (const DATAFILE_PROPERTY *p = dat->prop; p->type != DAT_END; p++)
            if (p->type == type) return p->dat ? p->dat : "";
    return "";
}
