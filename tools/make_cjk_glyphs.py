"""Gera src/game/lang_cjk.inc: os glifos 16x16 da GNU Unifont so para os caracteres chineses usados
nas traducoes (src/game/lang.cpp). Rodar de novo sempre que as traducoes em chines mudarem.

Uso: python tools/make_cjk_glyphs.py <unifont-18.0.01.hex.gz>
     (unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/; SIL OFL 1.1 / GPL2+ com excecao de fonte)
"""
import gzip
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src", "game", "lang.cpp")
OUT = os.path.join(ROOT, "src", "game", "lang_cjk.inc")


def is_cjk(cp):
    return 0x2E80 <= cp <= 0x9FFF or 0x3000 <= cp <= 0x303F or 0xFF00 <= cp <= 0xFFEF


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 1
    text = open(SRC, encoding="utf-8").read()
    wanted = sorted({ord(c) for c in text if is_cjk(ord(c))})
    glyphs = {}
    with gzip.open(argv[1], "rt", encoding="ascii") as f:
        for line in f:
            cp_hex, _, bits = line.strip().partition(":")
            cp = int(cp_hex, 16)
            if cp in wanted:
                glyphs[cp] = bits
    missing = [cp for cp in wanted if cp not in glyphs or len(glyphs[cp]) != 64]
    if missing:
        print("sem glifo 16x16 na Unifont:", " ".join(f"U+{cp:04X}" for cp in missing))
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("// Gerado por tools/make_cjk_glyphs.py a partir da GNU Unifont 18.0.01 (unifoundry.com).\n")
        o.write("// Unifont (Roman Czyborra, Paul Hardy e colaboradores), usada sob a SIL Open Font License 1.1:\n")
        o.write("// third_party/unifont/OFL-1.1.txt. Nao editar a mao.\n")
        o.write("// Cada glifo: 16 linhas de 16 bits (bit 15 = coluna 0).\n")
        for cp in wanted:
            rows = [int(glyphs[cp][i:i + 4], 16) for i in range(0, 64, 4)]
            o.write("{0x%04X, {%s}},  // %s\n" % (cp, ", ".join(f"0x{r:04X}" for r in rows), chr(cp)))
    print(f"{len(wanted)} glifos -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
