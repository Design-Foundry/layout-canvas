"""Create the trusted static 700/upright face from the bundled OFL Inter source.

Development-only: fontTools 4.51.0. Native/WASM builds consume committed bytes.
The renamed derivative keeps the original copyright and license records.
"""
from pathlib import Path
import hashlib
import fontTools
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

root = Path(__file__).resolve().parents[1]
source = root / 'assets/fonts/Inter.ttf'
assert fontTools.__version__ == '4.51.0'
assert hashlib.sha256(source.read_bytes()).hexdigest() == 'cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4'
font = TTFont(source, recalcTimestamp=False)
assert {axis.axisTag for axis in font['fvar'].axes} == {'wght', 'slnt'}
bold = instantiateVariableFont(font, {'wght': 700, 'slnt': 0}, inplace=False)
bold.recalcTimestamp = False
assert 'fvar' not in bold
names = {1: 'Canvas Inter', 2: 'Bold', 3: 'CanvasInter-Bold-3.019-static-v1',
         4: 'Canvas Inter Bold', 6: 'CanvasInter-Bold', 16: 'Canvas Inter', 17: 'Bold'}
for record in bold['name'].names:
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
bold['OS/2'].usWeightClass = 700
bold['OS/2'].fsSelection = (bold['OS/2'].fsSelection | 32) & ~65
bold['head'].macStyle = (bold['head'].macStyle | 1) & ~2
target = root / 'assets/fonts/CanvasInter-Bold.ttf'
bold.save(target)
print(hashlib.sha256(target.read_bytes()).hexdigest(), len(target.read_bytes()), 'bytes')
