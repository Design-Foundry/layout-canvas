# HarfBuzz

HarfBuzz 15.0.0, fetched at configure time from
https://github.com/harfbuzz/harfbuzz/releases/download/15.0.0/harfbuzz-15.0.0.tar.xz.

SHA-256 of the archive: `968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df`.
Only the amalgamated `src/harfbuzz.cc` is compiled, with the `HB_NO_*` options listed in
`visualcore/CMakeLists.txt`. The sources are unmodified.

HarfBuzz is distributed under the "Old MIT" license in `COPYING`, copied from the archive. It is
compiled into the native library and the WASM module.

HarfBuzz parses every font file, the bundled Inter and any face registered from bytes whose
SHA-256 matches the expected hash. stb_truetype only rasterizes the outlines HarfBuzz produces.
