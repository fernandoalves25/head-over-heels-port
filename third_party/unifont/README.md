# GNU Unifont (Chinese glyphs)

`src/game/lang_cjk.inc` contains 200 glyphs (16×16 bitmaps) taken from
[GNU Unifont](https://unifoundry.com/unifont/) 18.0.01, created by Roman Czyborra, Paul Hardy
and the Unifont contributors. The port draws them in the style of the game's own fonts
(gradient and outline) — a Modified Version of the Font Software in the OFL's terms.

Unifont is dual-licensed under the SIL Open Font License 1.1 and the GNU GPL 2+ with the GNU
font embedding exception. This project uses the glyphs under the **SIL Open Font License 1.1**;
the license text is in [`OFL-1.1.txt`](OFL-1.1.txt), copied unchanged from
<https://unifoundry.com/OFL-1.1.txt>. The glyphs remain under that license, not under this
repository's MIT license.

`tools/make_cjk_glyphs.py` regenerates `lang_cjk.inc` from `unifont-18.0.01.hex.gz`
(<https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/>).

---

Os ideogramas de `src/game/lang_cjk.inc` (200 glifos 16×16) vêm da GNU Unifont 18.0.01, de
Roman Czyborra, Paul Hardy e colaboradores, e continuam sob a SIL Open Font License 1.1
(`OFL-1.1.txt`, cópia sem alterações de unifoundry.com), não sob a licença MIT deste repositório.
