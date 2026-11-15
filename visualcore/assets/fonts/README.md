# Bundled font

`Inter.ttf` is the unmodified Inter 3.019 variable font (default weight 400,
upright), Copyright 2020 The Inter Project Authors. Its SIL OFL 1.1 license is
in `OFL.txt`. Upstream: https://github.com/rsms/inter.

SHA-256: `cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4`.

The exact bytes are compiled into native and WASM builds and registered as the
face `embedded/inter`. The four document font ids map to instances of it:

| Font id | wght | slnt |
| --- | ---: | ---: |
| `inter-regular-v1` | 400 | 0 |
| `inter-bold-v1` | 700 | 0 |
| `inter-oblique-v1` | 400 | -10 |
| `inter-bold-oblique-v1` | 700 | -10 |

Shaping, kerning and OpenType features come from HarfBuzz 15.0.0 at runtime.
Glyphs are rasterized on demand into a 4096 px dynamic atlas at 96 px/em.

Other faces can be registered from bytes whose SHA-256 matches the declared
hash (`registerFontFace`, `vc_register_font_face`, or `registerFontFace` in
WASM).

The character repertoire is unchanged for now: printable ASCII, Latin-1
printable characters, en/em dashes, curly quotes, bullet, ellipsis, single angle
quotes, euro and trademark. Other characters are refused rather than replaced by
missing glyphs. Normalize authored Spanish text to precomposed characters before
entering it.
