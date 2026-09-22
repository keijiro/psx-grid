#!/usr/bin/env python3
"""Optional Pillow study; shipping atlas generation needs no third-party modules."""
import sys
sys.dont_write_bytecode = True
import importlib.util
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('assets', root/'scripts/generate-assets.py')
a = importlib.util.module_from_spec(spec); spec.loader.exec_module(a)
atlas = Image.new('RGBA', (256,64))
for y,row in enumerate(a.pixels):
    for x,p in enumerate(row): atlas.putpixel((x,y),(a.PALETTE[p],)*3+(255 if p else 0,))
def bitmap(im, xy, s, bright=False):
    x,y = xy
    for c in s:
        i=ord(c)-32
        glyph=atlas.crop((i%32*8,24+i//32*8,i%32*8+7,31+i//32*8))
        if bright:
            data=[(22,22,22,p[3]) for p in glyph.getdata()]; glyph.putdata(data)
        im.paste(glyph,(x,y),glyph); x+=a.metrics[i]
(root/'build').mkdir(exist_ok=True)
for pitch in (16,18):
    im=Image.new('RGB',(320,240),(22,)*3); d=ImageDraw.Draw(im)
    bitmap(im,(8,8),f'PITCH {pitch} / BITMAP 5X7')
    for y in range(32,96,pitch):
        for x in range(8,312-pitch+1,pitch): d.point((x+pitch//2,y+pitch//2),fill=(78,)*3)
    for i in range(9):
        tile=atlas.crop((i*16,0,i*16+16,16))
        if pitch==18:
            # Compare a wider shell with the same ink, without rescaling strokes.
            tile=Image.new('RGBA',(18,18))
            td=ImageDraw.Draw(tile)
            if i<8:
                field=232 if i in (0,6,7) else 22 if i==1 else 52
                td.rounded_rectangle((2,1,16,16),radius=2,fill=(field,)*3+(255,),
                                     outline=(232,232,232,255) if i==1 else None)
                ink=atlas.crop((i*16+3,3,i*16+14,13))
                # Retain just the symbol/text pixels, not its old body.
                value=22 if i in (0,6,7) else 242
                ink.putdata([p if p[:3]==(value,)*3 else (0,0,0,0) for p in ink.getdata()])
                tile.paste(ink,(4,4),ink)
            else:
                marker=atlas.crop((128,0,144,16)); tile.paste(marker,(1,1),marker)
        im.paste(tile,(8+i*pitch,48),tile)
    d.line((7,47,11,47),fill=(242,)*3); d.line((7,47,7,51),fill=(242,)*3)
    d.rectangle((178,32,310,90),fill=(30,)*3,outline=(58,)*3)
    bitmap(im,(184,40),'> PLACE TILE'); bitmap(im,(184,56),'CHANGE LENGTH'); bitmap(im,(184,72),'CLOSE')
    samples=['CH1 C4 C#4 0/O 1/I 127,63','LANE COLLISION: MOVE OR ADJUST','CONNECT PAD 1 / RELEASE BUTTONS']
    for i,s in enumerate(samples): bitmap(im,(8,100+i*12),s)
    d.rectangle((8,138,311,151),fill=(232,)*3); bitmap(im,(12,141),samples[0],True)
    for size,y in ((9,158),(10,198)):
        font=ImageFont.truetype(str(root/'assets/ui/Jura-Regular.ttf'),size)
        bitmap(im,(8,y),f'JURA {size}PX')
        for i,s in enumerate(samples[:2]): d.text((8,y+10+i*10),s,font=font,fill=(242,)*3,stroke_width=0)
    im.save(root/f'docs/captures/study-{pitch}.png')
    im.resize((960,720),Image.Resampling.NEAREST).save(root/f'build/study-{pitch}-3x.png')
