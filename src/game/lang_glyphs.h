// Port: glifos que as fontes em bitmap do jogo nao tem (ver lang.h).
//
// Cada fonte do jogo (faixa de glifos de largura fixa) ganha uma faixa "extra" paralela com as
// celulas dos caracteres nao-ASCII das traducoes: letras acentuadas (a letra do proprio jogo,
// comprimida na vertical quando nao sobra espaco, com o acento por cima no mesmo estilo), ¿ e ¡
// (? e ! virados) e, nas fontes grandes, os caracteres chineses (GNU Unifont, com o degrade e o
// contorno da fonte). As letras guardam de que pixel da fonte original veio cada pixel, entao no
// renderizador HD saem com a fonte do pacote. Quando o jogo tinge uma fonte inteira
// (tint_bitmap), a faixa extra recebe o mesmo tingimento (com cache por parametros).
#pragma once

#include "compat/allegro.h"

#include <cstdint>

namespace hoh {

// Faixa extra de uma fonte base (antes de qualquer tingimento dela). cell: largura da celula;
// mixed: indices de glyph_index_mixed (fonte com minusculas); cjk: inclui os caracteres chineses.
void lang_glyphs_build(BITMAP *font, int cell, bool mixed, bool cjk);
// Fonte que e a outra esticada na vertical (g_font_medium = g_font_w18 em 18 linhas).
void lang_glyphs_build_stretched(BITMAP *font, BITMAP *from);
// tint_bitmap de uma fonte inteira: a faixa extra de dst passa a ser a de src tingida igual.
void lang_glyphs_on_tint(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct);
// Desenha o glifo cp da fonte em (x, y); false se a fonte nao tem esse caractere.
bool lang_draw_glyph(BITMAP *dst, BITMAP *font, uint32_t cp, int x, int y);
// Libera as faixas (free_game_resources).
void lang_glyphs_free();

}  // namespace hoh
