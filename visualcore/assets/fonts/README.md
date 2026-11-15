# Bundled fonts

The exact bytes of these fonts are compiled into the native and WASM builds. All three are
unmodified and licensed under the SIL OFL 1.1.

| File | Face id | Role | Upstream | SHA-256 |
| --- | --- | --- | --- | --- |
| `Inter.ttf` | `embedded/inter` | Built-in Inter font and stack; `sans-serif` and `system-ui` fallback | Inter 3.019, https://github.com/rsms/inter (license `OFL.txt`) | `cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4` |
| `SourceSerif4.ttf` | `embedded/source-serif-4` | `serif` fallback | `SourceSerif4[opsz,wght].ttf` from google/fonts at `cc39c4c2c304bc368c9b7c311ea2bcc6013299fe` (license `SourceSerif4-OFL.txt`) | `97b2d4da6e3cb494b5a1e66ae176914d852ccabef49e0c02c0df25f3e39aca0b` |
| `JetBrainsMono.ttf` | `embedded/jetbrains-mono` | `monospace` fallback | `JetBrainsMono[wght].ttf` from google/fonts at `cc39c4c2c304bc368c9b7c311ea2bcc6013299fe` (license `JetBrainsMono-OFL.txt`) | `48715a42ec242c21e9f02692891e147d022299a52e48d5e413e1a942193ffeda` |

The retired document font ids map to Inter instances:

| Font id | wght | slnt |
| --- | ---: | ---: |
| `inter-regular-v1` | 400 | 0 |
| `inter-bold-v1` | 700 | 0 |
| `inter-oblique-v1` | 400 | -10 |
| `inter-bold-oblique-v1` | 700 | -10 |

Shaping, kerning and OpenType features come from HarfBuzz 15.0.0 at runtime. Glyphs are
rasterized on demand into a 4096 px dynamic atlas at 96 px/em.

Each character is drawn by the first font of its stack that covers it, then by the bundled face of
the stack's generic family. A fallback font gets the text's axis values clamped to what it allows.
A character nothing covers draws the missing-glyph box of the first available font. Text is laid
out left to right; there is no BiDi reordering yet.

Other faces can be registered from bytes whose SHA-256 matches the declared hash
(`registerFontFace`, `vc_register_font_face`, or `registerFontFace` in WASM).
