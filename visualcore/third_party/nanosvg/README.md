# NanoSVG

Unmodified `nanosvg.h` and `nanosvgrast.h` from the official repository, at commit
`239e102ec2c691f2902e20ace2ed36ee4a35cfe6`:
https://github.com/memononen/nanosvg/tree/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src.

SHA-256 of `nanosvg.h`: `e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0`.
SHA-256 of `nanosvgrast.h`: `79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8`.

NanoSVG is distributed under the zlib license in `LICENSE.txt`, copied from the repository. It is compiled into
the native library and the WASM module (`visualcore/src/icon.cpp`).

It rasterizes icon SVGs into coverage masks that the glyph atlas draws tinted by the node fill. Only SVG that
passes `iconSvgProblem` (path, basic shapes, groups, transforms, fills and strokes, up to 64 KiB) reaches the
parser: gradients, images, references, scripts, styles sheets and entities are refused first.
