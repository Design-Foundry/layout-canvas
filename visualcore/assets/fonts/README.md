# Bundled font

`Inter.ttf` is the unmodified Inter 3.019 variable font (default weight 400,
upright), Copyright 2020 The Inter Project Authors. Its SIL OFL 1.1 license is
in `OFL.txt`. Upstream: https://github.com/rsms/inter.

SHA-256: `cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4`.
The exact bytes are compiled into native and WASM builds. The first editable
text subset uses the default regular outlines. Trusted static derivatives provide
Bold, Oblique and Bold Oblique; arbitrary variable axes, font uploads and remote
font loading are not exposed.

`CanvasInter-Bold.ttf` is a renamed static 700/upright derivative of those exact
source bytes. `tools/generate-bold-font.py`, using fontTools 4.51.0, fixes both
axes (`wght=700`, `slnt=0`), preserves the source timestamps/copyright/license,
and names the derivative **Canvas Inter Bold**. Its hash is
`e08273de48feb18e9211f04eddd8e2c76ae9406f2befd71abfd7b01fc7a8d92c`
(316,172 bytes); two independent generation runs produced the same bytes. Its
font identifier is `inter-bold-v1`. This is a real outline/advance instance, not
stroke expansion or a synthetic browser weight applied to the native glyphs.

`tools/generate-oblique-fonts.py` fixes `slnt=-10` and weight 400 or 700 using the
same source, fontTools version and timestamp/license preservation. The renamed
derivatives are **Canvas Inter Oblique** and **Canvas Inter Bold Oblique**; these
are actual slant-axis instances, not a separately designed italic or a CSS shear.
Two independent runs reproduced both SHA-256 hashes:

| File | Bytes | SHA-256 | Native identifier |
| --- | ---: | --- | --- |
| `CanvasInter-Oblique.ttf` | 320,136 | `7a05f8ae4e714907434f5c70aced7aeaebfedb1f4f2b5eef42587b40db602f3d` | `inter-oblique-v1` |
| `CanvasInter-BoldOblique.ttf` | 323,144 | `5361476462b7af7bb5bdf351143efe00192c7a7556a0e17d2ab34231a9d437f5` | `inter-bold-oblique-v1` |

All four faces use the same immutable 4096×4096 R8 atlas (16 MiB, previously
4 MiB for two faces). Raster resolution remains 96 px/em. Native and WASM use committed
font bytes and separate generated pair-advance tables. Regenerate the Bold table
with `tools/generate-font-kerning.py bold` after verifying the derivative hash.
There are 4,169 nonzero Bold pairs and 2,816 units/em. Development generators need
fontTools; normal builds do not. The browser content input already loads the
original trusted variable font and selects only 400/700 with slant 0/-10 to match
the chosen native face. The local OFL applies to the original and derivatives.
The Oblique and Bold Oblique pair tables are regenerated with arguments `oblique`
and `bold_oblique`, containing 3,832 and 4,169 pairs respectively.

Text uses native em metrics, pair kerning, nonnegative letter spacing, explicit
newlines and left/center/right alignment. The initial repertoire is printable
ASCII, Latin-1 printable characters, en/em dashes, curly quotes, bullet,
ellipsis, single angle quotes, euro and trademark. Combining marks, complex
scripts and other characters are refused rather than replaced by missing glyphs.
Normalize authored Spanish text to precomposed characters before entering it.

Rendering uses a deterministic 96px/em grayscale atlas with linear sampling.
This is an initial raster text implementation, not outline rendering at arbitrary
zoom or full shaping/Unicode-line-breaking/rich-text parity.
