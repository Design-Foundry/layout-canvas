"""Create reproducible OFL Inter slnt=-10 static derivatives (fontTools 4.51.0)."""
from pathlib import Path
import hashlib
import fontTools
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

root = Path(__file__).resolve().parents[1]
source = root / 'assets/fonts/Inter.ttf'
assert fontTools.__version__ == '4.51.0'
assert hashlib.sha256(source.read_bytes()).hexdigest() == 'cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4'
for style, weight in [('Oblique', 400), ('BoldOblique', 700)]:
    font = TTFont(source, recalcTimestamp=False)
    face = instantiateVariableFont(font, {'wght': weight, 'slnt': -10}, inplace=False)
    face.recalcTimestamp = False
    assert 'fvar' not in face
    label = 'Bold Oblique' if weight == 700 else 'Oblique'
    names = {1: 'Canvas Inter', 2: label, 3: f'CanvasInter-{style}-3.019-static-v1',
             4: f'Canvas Inter {label}', 6: f'CanvasInter-{style}', 16: 'Canvas Inter', 17: label}
    for record in face['name'].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    face['OS/2'].usWeightClass = weight
    # OpenType OBLIQUE bit requires OS/2 v4; retain the source table version and
    # its italic compatibility bit, with names explicitly identifying Oblique.
    face['OS/2'].fsSelection = (face['OS/2'].fsSelection | 1) & ~96
    face['head'].macStyle = (face['head'].macStyle | 2) & ~1
    if weight == 700:
        face['OS/2'].fsSelection |= 32
        face['head'].macStyle |= 1
    target = root / f'assets/fonts/CanvasInter-{style}.ttf'
    face.save(target)
    print(style, hashlib.sha256(target.read_bytes()).hexdigest(), len(target.read_bytes()), 'bytes')
