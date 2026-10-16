// Codificacao dos packfiles do Allegro 4.0 como usada pelo HoH.exe (ver FORMATOS.md §2).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace packfile_codec {

constexpr uint32_t F_PACK_MAGIC = 0x736C6821;    // "slh!"
constexpr uint32_t F_NOPACK_MAGIC = 0x736C682E;  // "slh."

// Mascara do magic na alleg40.dll do jogo: XOR(pw[i] << ((i&3)*8)) ^ 42, sem o 2o laco
// que versoes posteriores do Allegro adicionaram. Senha vazia = sem mascara.
uint32_t encrypt_id(uint32_t x, const std::string &password);

// LZSS do Allegro: N=4096, F=18, THRESHOLD=2, janela inicial zerada, r = N-F.
std::vector<uint8_t> lzss_unpack(const uint8_t *src, size_t n);
std::vector<uint8_t> lzss_pack(const uint8_t *src, size_t n);

// XOR ciclico com a senha; `phase` = posicao no arquivo do primeiro byte.
void xor_password(uint8_t *p, size_t n, const std::string &password, size_t phase);

// Arquivo inteiro em memoria <-> conteudo logico.
//   packed_mode = true:  le "rp"/"rbp" (magic + corpo cifrado, comprimido ou nao)
//   packed_mode = false: le "r" (so cifra, se houver senha)
bool decode(const std::vector<uint8_t> &file, const std::string &password, bool packed_mode,
            std::vector<uint8_t> &out, std::string *error = nullptr);
std::vector<uint8_t> encode(const std::vector<uint8_t> &content, const std::string &password,
                            bool packed_mode);

}  // namespace packfile_codec
