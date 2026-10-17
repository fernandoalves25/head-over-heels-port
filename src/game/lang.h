// Port: idiomas (nao existe no original; o jogo e so em ingles).
//
// Os textos do jogo continuam em ingles nas tabelas geradas do exe (globals.cpp); a traducao e
// feita na hora de compor/desenhar (tr, tr_menu). Os textos traduzidos sao UTF-8: as rotinas de
// texto em bitmap (gfx_util.cpp) decodificam UTF-8 e desenham os glifos que a fonte do jogo nao
// tem (letras acentuadas, chines) a partir de faixas extras (lang_glyphs.cpp).
#pragma once

#include <cstdint>
#include <vector>

namespace hoh {

enum Lang { LANG_EN, LANG_PT, LANG_ES, LANG_ZH, LANG_COUNT };

void lang_init();                 // le o idioma do port.cfg (platform_lang)
Lang lang_current();
void lang_set(Lang l);            // grava no port.cfg e refaz o que foi pre-desenhado (letreiro)
const char *lang_name(Lang l);    // o nome do idioma nele mesmo (ENGLISH, PORTUGUÊS, ESPAÑOL, 中文)

// Texto inteiro do jogo, igual ao ingles; sem traducao (ou em ingles), devolve o proprio texto.
const char *tr(const char *english);
// Linha de menu: texto inteiro, "ROTULO:  VALOR" (rotulo e valor traduzidos, valor alinhado a
// direita no mesmo numero de colunas) ou linha de controle (rotulo nas 12 primeiras colunas).
const char *tr_menu(int menu, const char *line);

// UTF-8
uint32_t utf8_next(const char *&s);
int utf8_cols(const char *s);     // caracteres (cada um ocupa uma celula da fonte)
// Todos os caracteres nao-ASCII usados pelas traducoes (para montar os glifos extras).
std::vector<uint32_t> lang_codepoints();

}  // namespace hoh
