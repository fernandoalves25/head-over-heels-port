// PACKFILE do Allegro 4.0: leitura/escrita inteira em memoria, com a cifra e o LZSS do jogo.
#include "compat/allegro.h"
#include "compat/packfile_codec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace packfile_codec {

uint32_t encrypt_id(uint32_t x, const std::string &pw) {
    if (pw.empty()) return x;
    uint32_t mask = 0;
    for (size_t i = 0; i < pw.size(); i++)
        mask ^= static_cast<uint32_t>(static_cast<uint8_t>(pw[i])) << ((i & 3) * 8);
    return x ^ mask ^ 42u;
}

void xor_password(uint8_t *p, size_t n, const std::string &pw, size_t phase) {
    if (pw.empty()) return;
    for (size_t i = 0; i < n; i++) p[i] ^= static_cast<uint8_t>(pw[(phase + i) % pw.size()]);
}

std::vector<uint8_t> lzss_unpack(const uint8_t *src, size_t n) {
    constexpr int N = 4096, F = 18, TH = 2;
    std::vector<uint8_t> out;
    out.reserve(n * 3);
    uint8_t ring[N] = {};
    int r = N - F;
    unsigned flags = 0;
    size_t i = 0;
    for (;;) {
        flags >>= 1;
        if (!(flags & 256)) {
            if (i >= n) break;
            flags = src[i++] | 0xFF00u;
        }
        if (flags & 1) {
            if (i >= n) break;
            uint8_t c = src[i++];
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & (N - 1);
        } else {
            if (i + 1 >= n) break;
            int pos = src[i] | ((src[i + 1] & 0xF0) << 4);
            int len = (src[i + 1] & 0x0F) + TH;
            i += 2;
            for (int k = 0; k <= len; k++) {
                uint8_t c = ring[(pos + k) & (N - 1)];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
    return out;
}

// Compressor guloso com cadeias de hash; gera fluxo que o lzss_unpack (e o Allegro) decodifica.
std::vector<uint8_t> lzss_pack(const uint8_t *in, size_t n) {
    constexpr int N = 4096, F = 18, TH = 2, MAX_CHAIN = 256;
    std::vector<uint8_t> out;
    out.reserve(n / 2 + 16);
    std::vector<int32_t> head(1 << 16, -1), prev(n, -1);
    auto hash3 = [in](size_t i) { return ((in[i] << 8) ^ (in[i + 1] << 4) ^ in[i + 2]) & 0xFFFF; };
    auto insert = [&](size_t i) {
        if (i + 2 < n) {
            int h = hash3(i);
            prev[i] = head[h];
            head[h] = static_cast<int32_t>(i);
        }
    };
    size_t i = 0, flag_pos = 0;
    int bit = 8;
    while (i < n) {
        if (bit == 8) {
            flag_pos = out.size();
            out.push_back(0);
            bit = 0;
        }
        int best_len = 0;
        size_t best_src = 0;
        if (i + 2 < n) {
            int max_len = static_cast<int>(std::min<size_t>(F, n - i));
            int chain = 0;
            for (int32_t c = head[hash3(i)]; c >= 0 && i - c <= static_cast<size_t>(N - F) && chain < MAX_CHAIN;
                 c = prev[c], chain++) {
                int l = 0;
                while (l < max_len && in[c + l] == in[i + l]) l++;
                if (l > best_len) {
                    best_len = l;
                    best_src = c;
                    if (l == max_len) break;
                }
            }
        }
        int advance;
        if (best_len > TH) {
            int pos = (N - F + static_cast<int>(best_src % N)) & (N - 1);
            out.push_back(static_cast<uint8_t>(pos & 0xFF));
            out.push_back(static_cast<uint8_t>(((pos >> 4) & 0xF0) | (best_len - TH - 1)));
            advance = best_len;
        } else {
            out[flag_pos] |= static_cast<uint8_t>(1 << bit);
            out.push_back(in[i]);
            advance = 1;
        }
        bit++;
        for (int k = 0; k < advance; k++) insert(i++);
    }
    return out;
}

static uint32_t read_be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool decode(const std::vector<uint8_t> &file, const std::string &pw, bool packed_mode,
            std::vector<uint8_t> &out, std::string *error) {
    if (!packed_mode) {
        out = file;
        xor_password(out.data(), out.size(), pw, 0);
        return true;
    }
    if (file.size() < 4) {
        if (error) *error = "arquivo curto demais";
        return false;
    }
    uint32_t magic = read_be32(file.data());
    std::vector<uint8_t> body(file.begin() + 4, file.end());
    // O XOR corre sobre o arquivo cru com a posicao contada desde o offset 0
    // (os 4 bytes do magic tambem avancam a senha).
    xor_password(body.data(), body.size(), pw, 4);
    if (magic == encrypt_id(F_PACK_MAGIC, pw)) {
        out = lzss_unpack(body.data(), body.size());
        return true;
    }
    if (magic == encrypt_id(F_NOPACK_MAGIC, pw)) {
        out = std::move(body);
        return true;
    }
    if (error) *error = "magic nao confere (senha errada ou arquivo nao e packfile)";
    return false;
}

std::vector<uint8_t> encode(const std::vector<uint8_t> &content, const std::string &pw, bool packed_mode) {
    if (!packed_mode) {
        std::vector<uint8_t> out = content;
        xor_password(out.data(), out.size(), pw, 0);
        return out;
    }
    std::vector<uint8_t> body = lzss_pack(content.data(), content.size());
    xor_password(body.data(), body.size(), pw, 4);
    uint32_t m = encrypt_id(F_PACK_MAGIC, pw);
    std::vector<uint8_t> out = {uint8_t(m >> 24), uint8_t(m >> 16), uint8_t(m >> 8), uint8_t(m)};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace packfile_codec

// ------------------------------------------------------------------ API Allegro

struct PACKFILE {
    bool writing = false;
    bool packed = false;
    std::string filename;
    std::string password;  // senha vigente no pack_fopen (o jogo zera a global logo depois)
    std::vector<uint8_t> data;
    size_t pos = 0;
};

static std::string g_password;

void packfile_password(const char *password) { g_password = password ? password : ""; }

static bool read_whole_file(const char *name, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(name, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

PACKFILE *pack_fopen(const char *filename, const char *mode) {
    if (!filename || !mode) return nullptr;
    bool writing = std::strpbrk(mode, "wW") != nullptr;
    bool packed = std::strpbrk(mode, "pP") != nullptr;
    PACKFILE *f = new PACKFILE();
    f->writing = writing;
    f->packed = packed;
    f->filename = filename;
    f->password = g_password;
    if (!writing) {
        std::vector<uint8_t> raw;
        if (!read_whole_file(filename, raw) ||
            !packfile_codec::decode(raw, f->password, packed, f->data)) {
            delete f;
            return nullptr;
        }
    } else {
        FILE *probe = std::fopen(filename, "ab");  // falha cedo se nao der para gravar
        if (!probe) {
            delete f;
            return nullptr;
        }
        std::fclose(probe);
    }
    return f;
}

long pack_fread(void *p, long n, PACKFILE *f) {
    if (!f || f->writing || n <= 0) return 0;
    size_t avail = f->data.size() - f->pos;
    size_t k = std::min(static_cast<size_t>(n), avail);
    std::memcpy(p, f->data.data() + f->pos, k);
    f->pos += k;
    return static_cast<long>(k);
}

long pack_fwrite(const void *p, long n, PACKFILE *f) {
    if (!f || !f->writing || n <= 0) return 0;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    f->data.insert(f->data.end(), b, b + n);
    return n;
}

int pack_feof(PACKFILE *f) { return !f || f->pos >= f->data.size(); }

int pack_fclose(PACKFILE *f) {
    if (!f) return -1;
    int rc = 0;
    if (f->writing) {
        std::vector<uint8_t> out = packfile_codec::encode(f->data, f->password, f->packed);
        FILE *fp = std::fopen(f->filename.c_str(), "wb");
        if (!fp || (!out.empty() && std::fwrite(out.data(), 1, out.size(), fp) != out.size())) rc = -1;
        if (fp) std::fclose(fp);
    }
    delete f;
    return rc;
}
