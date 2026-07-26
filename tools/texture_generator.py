#!/usr/bin/env python3
"""Deterministic 16x16 voxel texture generator, validator, and atlas builder.

Design audit (2026-07): the original generator interpolated one 5x5 control
field for nearly every material, added axis-aligned 3x3 grains and a fixed
upper-left pixel highlight, then sampled a 15x15 domain into a 16x16 image.
That combination produced large rectangles, horizontal/vertical crossings,
shared camouflage-like structure, and a visible 15-pixel repeat.  It also made
edge equality by aliasing the last row/column instead of constructing features
on a torus.  The generators below therefore combine irregular toroidal macro
fields, material-specific meso features, and sparse correlated micro accents.
"""

import argparse
import json
import math
import struct
import sys
import zlib
from collections import Counter, deque
from pathlib import Path

SIZE = 16
# Selected from the deterministic contact-sheet candidates. CMake's asset
# target relies on this default, so keep it aligned with committed atlas.json.
DEFAULT_SEED = 213785369
NAMES = [
    "dirt", "grass_top", "grass_side", "stone", "oak_log", "oak_log_top",
    "leaves", "sand", "bedrock", "water", "snow", "oak_planks",
    "deepslate", "cactus_side", "cactus_top", "coal_ore", "iron_ore",
    "gold_ore", "diamond_ore", "lava", "ice", "gravel", "clay",
    "red_sand", "terracotta", "podzol_top", "moss", "tall_grass",
    "flower", "reeds", "birch_log", "birch_leaves", "spruce_log",
    "spruce_leaves", "jungle_log", "jungle_leaves", "acacia_log",
    "acacia_leaves", "cobblestone", "crafting_table", "furnace", "chest",
    "torch", "white_wool", "white_bed", "farmland", "wet_farmland",
    "wheat_young", "wheat_middle", "wheat_mature", "oak_sapling",
    "birch_sapling", "spruce_sapling", "jungle_sapling", "acacia_sapling",
    "snow_layer", "fire", "copper_ore",
]

PALETTES = {
    "dirt": [(76,51,34,255),(89,59,38,255),(104,69,43,255),(119,80,49,255),(132,91,57,255),(145,102,66,255)],
    "stone": [(82,86,87,255),(94,98,99,255),(107,111,111,255),(120,124,123,255),(134,137,135,255),(148,150,147,255)],
    "sand": [(174,163,122,255),(188,176,132,255),(201,189,143,255),(214,202,155,255),(226,215,168,255),(236,226,181,255)],
    "grass_top": [(43,91,42,255),(50,104,45,255),(59,117,49,255),(69,130,54,255),(81,143,61,255),(95,154,69,255)],
    "grass_side": [(76,53,36,255),(91,62,40,255),(106,73,46,255),(121,84,52,255),(55,113,49,255),(72,135,57,255)],
    "oak_log": [(62,45,30,255),(74,52,33,255),(87,60,36,255),(101,70,41,255),(116,81,48,255),(131,93,56,255)],
    "oak_log_top": [(82,57,34,255),(98,68,38,255),(115,80,44,255),(133,94,52,255),(151,109,61,255),(169,125,72,255)],
    "oak_planks": [(94,66,39,255),(111,78,43,255),(129,91,49,255),(148,106,57,255),(167,123,68,255),(186,141,80,255)],
    "leaves": [(0,0,0,0),(32,78,37,255),(39,91,40,255),(48,105,44,255),(59,120,49,255),(73,133,56,255),(88,145,65,255)],
    "coal_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(34,35,36,255),(47,48,49,255),(62,63,63,255)],
    "copper_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(121,68,49,255),(174,101,67,255),(211,146,99,255)],
    "iron_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(151,119,98,255),(177,143,116,255),(202,169,140,255)],
}
EXTRA_BASES = {
    "bedrock":(58,59,62), "water":(47,92,156), "snow":(218,226,231),
    "deepslate":(54,58,64), "cactus_side":(57,111,57), "cactus_top":(76,128,64),
    "gold_ore":(190,145,50), "diamond_ore":(54,174,169), "lava":(211,80,25),
    "ice":(142,187,205), "gravel":(103,100,98), "clay":(133,146,156),
    "red_sand":(172,87,48), "terracotta":(142,76,55), "podzol_top":(91,65,42),
    "moss":(67,108,52), "tall_grass":(59,124,52), "flower":(175,68,83),
    "reeds":(112,145,70), "birch_log":(177,169,141), "birch_leaves":(74,127,58),
    "spruce_log":(72,55,39), "spruce_leaves":(43,82,61), "jungle_log":(96,70,44),
    "jungle_leaves":(48,118,48), "acacia_log":(119,76,53), "acacia_leaves":(78,115,55),
    "cobblestone":(105,107,105), "crafting_table":(132,88,49), "furnace":(93,95,94),
    "chest":(145,98,47), "torch":(195,133,48), "white_wool":(205,205,199),
    "white_bed":(205,197,191), "farmland":(91,62,40), "wet_farmland":(61,44,35),
    "wheat_young":(70,116,47), "wheat_middle":(128,128,48), "wheat_mature":(190,151,57),
    "oak_sapling":(58,120,50), "birch_sapling":(79,133,61), "spruce_sapling":(45,88,63),
    "jungle_sapling":(48,127,47), "acacia_sapling":(82,119,54),
    "snow_layer":(218,226,231), "fire":(221,91,24),
}
TRANSPARENT = {"tall_grass","flower","reeds","torch","wheat_young","wheat_middle",
               "wheat_mature","oak_sapling","birch_sapling","spruce_sapling",
               "jungle_sapling","acacia_sapling","fire"}

def muted_palette(base, transparent=False):
    colors = [tuple(max(1, min(245, c+d)) for c in base)+(255,)
              for d in (-43,-28,-14,0,13,27)]
    return ([(0,0,0,0)]+colors) if transparent else colors

for _name, _base in EXTRA_BASES.items():
    PALETTES[_name] = muted_palette(_base, _name in TRANSPARENT or _name.endswith("_leaves"))
PALETTES["fire"] = [(0,0,0,0),(116,34,17,255),(164,48,17,255),(207,67,17,255),
                    (235,101,20,255),(245,151,35,255),(250,205,76,255)]
for _name, _ore in {"gold_ore":((125,91,31),(191,143,43),(235,190,67)),
                    "diamond_ore":((27,111,112),(50,177,173),(112,226,211))}.items():
    PALETTES[_name] = PALETTES["stone"][:3] + [c+(255,) for c in _ore]

HIGH_CONTRAST_NAMES = {"coal_ore","copper_ore","iron_ore","gold_ore","diamond_ore","fire"}
NATURAL = {"dirt","grass_top","stone","sand","bedrock","deepslate","gravel","clay",
           "red_sand","terracotta","podzol_top","moss","snow","snow_layer","cobblestone"}
DIRECTIONAL = {"oak_planks","oak_log","birch_log","spruce_log","jungle_log","acacia_log",
               "farmland","wet_farmland","cactus_side","reeds"}
LEAF_NAMES = {"leaves","birch_leaves","spruce_leaves","jungle_leaves","acacia_leaves"}

def mix64(v):
    v=(v+0x9E3779B97F4A7C15)&0xffffffffffffffff
    v=((v^(v>>30))*0xBF58476D1CE4E5B9)&0xffffffffffffffff
    v=((v^(v>>27))*0x94D049BB133111EB)&0xffffffffffffffff
    return v^(v>>31)

def sample(seed,name,x,y,channel=0):
    salt=int.from_bytes(name.encode(),"little")&0xffffffffffffffff
    return mix64(seed^salt^(x*0x8DA6B343)^(y*0xD8163841)^(channel*0xA24BAED4963EE407))

def rand01(seed,name,x,y,channel=0): return sample(seed,name,x,y,channel)/0xffffffffffffffff
def wrap(v): return v%SIZE
def torus_delta(a,b):
    d=(a-b)%SIZE
    return d-SIZE if d>SIZE/2 else d
def neighbors4(x,y): return ((wrap(x-1),y),(wrap(x+1),y),(x,wrap(y-1)),(x,wrap(y+1)))
def neighbors8(x,y):
    return tuple((wrap(x+dx),wrap(y+dy)) for dy in (-1,0,1) for dx in (-1,0,1) if dx or dy)

def macro_field(seed,name,count=5):
    """Irregular radial influence on a torus; no enlarged low-res rectangles."""
    anchors=[]
    for i in range(count):
        h=sample(seed,name,i,31,1)
        anchors.append((h%SIZE,(h>>8)%SIZE,1.8+((h>>16)%25)/10.0,((h>>24)%201-100)/100.0))
    field=[]
    for y in range(SIZE):
        for x in range(SIZE):
            value=0.0; weight=0.0
            for ax,ay,r,tone in anchors:
                dx,dy=torus_delta(x,ax),torus_delta(y,ay)
                # Skewed radial kernels break axis symmetry without block scaling.
                dist=math.sqrt((dx+0.23*dy)**2+(dy-0.17*dx)**2)
                w=max(0.0,1.0-dist/r)**2
                value+=tone*w; weight+=w
            field.append(value/max(0.45,weight))
    return field

def grow_blob(seed,name,anchor,size,channel=0,elongation=None):
    cells={anchor}; frontier=[anchor]
    while len(cells)<size:
        choices=[]
        for x,y in frontier:
            # Four-connected growth guarantees that a visual cluster remains a
            # single cluster under the validator; diagonal steps may still be
            # introduced by a material's later detail pass.
            for nx,ny in neighbors4(x,y):
                if (nx,ny) not in cells:
                    dx,dy=torus_delta(nx,anchor[0]),torus_delta(ny,anchor[1])
                    penalty=abs(dx)+abs(dy)
                    if elongation:
                        penalty=abs(dx)*elongation[0]+abs(dy)*elongation[1]
                    score=rand01(seed,name,nx,ny,channel)-penalty*0.035
                    choices.append((score,nx,ny))
        if not choices: break
        _,nx,ny=max(choices)
        cells.add((nx,ny)); frontier.append((nx,ny))
        if len(frontier)>max(4,size//2): frontier.pop(0)
    return cells

def quantize(values,levels=6):
    # Rank quantization uses every palette entry even when an intentionally
    # flat plane contains tied values.  The tiny quasiperiodic tie breaker is
    # correlated across neighbors and is not a per-pixel random-noise layer.
    ranked=sorted(range(len(values)),key=lambda i:(
        values[i]+.032*math.sin((i%SIZE)*1.71+(i//SIZE)*.93)
        +.021*math.sin((i%SIZE)*.47-(i//SIZE)*1.33), i))
    result=[0]*len(values)
    for rank,index in enumerate(ranked): result[index]=min(levels-1,rank*levels//len(values))
    return result

def add_sparse_micro(indices,seed,name,amount=18,low=0,high=5):
    # Accents occur as adjacent pairs/turns, never independent white noise.
    for i in range(amount//2):
        h=sample(seed,name,i,77,3); x=h%SIZE; y=(h>>8)%SIZE
        dx,dy=((1,0),(1,1),(0,1),(-1,1))[((h>>16)&3)]
        delta=1 if (h>>20)&1 else -1
        for step in range(2+(1 if i%7==0 else 0)):
            p=wrap(y+dy*step)*SIZE+wrap(x+dx*step)
            indices[p]=max(low,min(high,indices[p]+delta))

def generate_dirt(name,seed):
    values=macro_field(seed,name,6)
    # Pebbly soil clods: compact irregular components at several scales.
    for i in range(7):
        h=sample(seed,name,i,41); blob=grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),3+(h>>16)%7,i)
        tone=(-0.42,0.30,0.55)[i%3]
        for x,y in blob: values[y*SIZE+x]+=tone
    out=quantize(values); add_sparse_micro(out,seed,name,14)
    return out

def generate_grass(seed):
    values=[v*0.35 for v in macro_field(seed,"grass_top",5)]
    # 3-6 toroidal tufts, with bent 2-4 pixel blades in varied directions.
    tuft_count=3+sample(seed,"grass_top",4,8)%4
    for i in range(tuft_count):
        h=sample(seed,"grass_top",i,53); anchor=(h%SIZE,(h>>8)%SIZE)
        blob=grow_blob(seed,"grass_top",anchor,7+(h>>16)%9,i)
        for x,y in blob:
            dx,dy=torus_delta(x,anchor[0]),torus_delta(y,anchor[1])
            values[y*SIZE+x]+=0.52-0.045*(abs(dx)+abs(dy))+(-0.18 if dx+dy>2 else 0.10)
        direction=((1,-1),(1,0),(-1,-1),(0,-1),(1,1),(-1,0))[i%6]
        length=2+(h>>24)%3; x,y=anchor
        for step in range(length):
            if step==2 and ((h>>29)&1): direction=(direction[0],-direction[1])
            x,y=wrap(x+direction[0]),wrap(y+direction[1])
            values[y*SIZE+x]+=0.72-0.13*step
    out=quantize(values); add_sparse_micro(out,seed,"grass_top",10)
    return out

def generate_stone(name,seed):
    values=[v*0.35 for v in macro_field(seed,name,4)]
    # Angular flakes are grown irregularly; short cracks stop after 2-5 steps.
    for i in range(8):
        h=sample(seed,name,i,61); anchor=(h%SIZE,(h>>8)%SIZE)
        blob=grow_blob(seed,name,anchor,5+(h>>16)%10,i)
        tone=(-0.38,0.24,0.42)[i%3]
        for x,y in blob: values[y*SIZE+x]+=tone
        if i%2==0:
            x,y=anchor; dx,dy=((1,1),(1,-1),(-1,1),(1,0))[i//2%4]
            for step in range(2+(h>>24)%4):
                x,y=wrap(x+dx),wrap(y+dy)
                values[y*SIZE+x]-=0.58
                if step==1: dy=0 if dy else (1 if (h>>30)&1 else -1)
    return quantize(values)

def generate_sand(name,seed):
    values=[v*0.18 for v in macro_field(seed,name,7)]
    for i in range(12):
        h=sample(seed,name,i,67); x=h%SIZE; y=(h>>8)%SIZE
        tone=(-0.25,0.22,0.34)[i%3]
        for step in range(2+(i%3==0)):
            xx=wrap(x+step); yy=wrap(y+(step//2 if i&1 else -(step//2)))
            values[yy*SIZE+xx]+=tone
    out=quantize(values); add_sparse_micro(out,seed,name,6)
    return out

def break_axis_runs(indices,limit=8):
    """Bend accidental flat runs without sprinkling independent random pixels."""
    out=list(indices)
    for horizontal in (True,False):
        for fixed in range(SIZE):
            run_start=0
            for moving in range(1,SIZE+1):
                def at(pos): return fixed*SIZE+pos if horizontal else pos*SIZE+fixed
                if moving<SIZE and out[at(moving)]==out[at(run_start)]: continue
                if moving-run_start>limit:
                    for pos in range(run_start+limit,moving,limit):
                        index=at(pos); out[index]=max(0,min(5,out[index]+(1 if out[index]<5 else -1)))
                run_start=moving
    return out

def break_flat_rectangles(indices):
    """Notch large near-solid boxes so clods retain an organic silhouette."""
    out=list(indices)
    for color in range(6):
        pending={(x,y) for y in range(SIZE) for x in range(SIZE)
                 if out[y*SIZE+x]==color}
        while pending:
            start=pending.pop(); component={start}; queue=[start]
            while queue:
                for neighbor in neighbors4(*queue.pop()):
                    if neighbor in pending:
                        pending.remove(neighbor); component.add(neighbor); queue.append(neighbor)
            if len(component)<12: continue
            xs=[p[0] for p in component]; ys=[p[1] for p in component]
            width=max(xs)-min(xs)+1; height=max(ys)-min(ys)+1
            if len(component)/(width*height)<=.96: continue
            # A single corner notch is a meso-shape correction, not a random
            # pixel layer. Prefer the lower-right corner to preserve upper-left light.
            corner=(max(xs),max(ys)); index=corner[1]*SIZE+corner[0]
            out[index]=color-1 if color>0 else 1
    return out

def generate_wood_side(name,seed):
    values=[]
    phase=(sample(seed,name,1,1)%628)/100.0
    for y in range(SIZE):
        for x in range(SIZE):
            bend=1.25*math.sin((y+phase)*math.tau/SIZE)+0.55*math.sin((2*y+x/5)*math.tau/SIZE)
            fiber=math.sin((x+bend)*math.tau/4.7)+0.35*math.sin((x*2-y*.35)*math.tau/5.3)
            values.append(fiber+macro_field(seed,name,4)[y*SIZE+x]*0.22)
    out=quantize(values)
    # Broken bark scars interrupt rather than reinforce the vertical fibers.
    for i in range(7):
        h=sample(seed,name,i,71); x=h%SIZE; y=(h>>8)%SIZE
        for step in range(2+(h>>16)%3): out[wrap(y+step)*SIZE+wrap(x+(step==2))]=i%2
    return out

def generate_log_top(name,seed):
    h=sample(seed,name,2,4); cx=5.2+(h%50)/10; cy=5.0+((h>>8)%55)/10
    values=[]
    for y in range(SIZE):
        for x in range(SIZE):
            dx,dy=torus_delta(x,cx),torus_delta(y,cy)
            radius=math.sqrt((dx*1.08+.12*dy)**2+(dy*.91)**2)
            wobble=.55*math.sin(math.atan2(dy,dx)*3+(h%31))+.25*math.sin((x+y)*1.7)
            ring=math.sin((radius+wobble)*2.45)
            # Ring breaks come from a separate angular signal.
            if rand01(seed,name,x//2,y//2,5)<.14: ring*=.15
            values.append(ring-radius*.025)
    return quantize(values)

def generate_planks(name,seed):
    out=[3]*256
    seams=(0,5,10)
    for y in range(SIZE):
        nearest=min(abs(torus_delta(y,s)) for s in seams)
        for x in range(SIZE):
            wobble=round(.65*math.sin((x+seed%7)*math.tau/11)+.35*math.sin(x*math.tau/5))
            on_seam=any(y==wrap(s+wobble) and (x+3*s)%13!=0 for s in seams)
            grain=math.sin((x+.35*y)*math.tau/7)+.4*math.sin((2*x-y)*math.tau/9)
            out[y*SIZE+x]=0 if on_seam else max(1,min(5,3+round(grain*.75)+(nearest==1)))
    return out

def generate_leaves(name,seed):
    values=macro_field(seed,name,7); out=quantize(values,6)
    holes=set()
    for i in range(5):
        h=sample(seed,name,i,83); anchor=(h%SIZE,(h>>8)%SIZE)
        holes |= grow_blob(seed,name,anchor,1+(h>>16)%4,i)
    return [0 if (x,y) in holes else 1+out[y*SIZE+x]
            for y in range(SIZE) for x in range(SIZE)]

def ore_clusters(name,seed):
    count=2+sample(seed,name,7,13)%3; clusters=[]; occupied=set()
    for i in range(count):
        h=sample(seed,name,i,89); anchor=(h%SIZE,(h>>8)%SIZE)
        for attempt in range(16):
            blob=grow_blob(seed,name,anchor,5+(h>>16)%8,i, (1.0,1.15))
            if not any(n in occupied for p in blob for n in neighbors4(*p)):
                break
            anchor=(wrap(anchor[0]+3),wrap(anchor[1]+5))
        occupied|=blob; clusters.append((anchor,blob))
    return clusters

def generate_ore(name,seed):
    base=generate_stone("stone",seed^sample(seed,name,0,0))
    out=[min(2,i//2) for i in base]
    for anchor,blob in ore_clusters(name,seed):
        for x,y in blob:
            dx,dy=torus_delta(x,anchor[0]),torus_delta(y,anchor[1])
            out[y*SIZE+x]=5 if dx+dy<0 else (4 if (x+y)%3 else 3)
    return out

def generate_generic(name,seed):
    if name in {"dirt","gravel","podzol_top"}: return generate_dirt(name,seed)
    if name=="grass_top" or name=="moss": return generate_grass(seed^sample(seed,name,0,0))
    if name in {"stone","bedrock","deepslate","clay","terracotta","cobblestone"}: return generate_stone(name,seed)
    if name in {"sand","red_sand","snow","snow_layer"}: return generate_sand(name,seed)
    if name.endswith("_ore"): return generate_ore(name,seed)
    if name in LEAF_NAMES: return generate_leaves(name,seed)
    if name.endswith("_log"): return generate_wood_side(name,seed)
    if name.endswith("_log_top"): return generate_log_top(name,seed)
    if name.endswith("_planks"): return generate_planks(name,seed)
    field=quantize(macro_field(seed,name,6))
    add_sparse_micro(field,seed,name,12)
    return field

PLANTS={"tall_grass","flower","reeds","torch","wheat_young","wheat_middle","wheat_mature",
        "oak_sapling","birch_sapling","spruce_sapling","jungle_sapling","acacia_sapling"}

def generate_special(name,seed,indices):
    if name=="grass_side":
        dirt=generate_dirt("dirt",seed); indices=dirt
        heights=[2+(sample(seed,name,x//3,1)%3) for x in range(SIZE)]
        for x in range(SIZE):
            for y in range(heights[x]+1): indices[y*SIZE+x]=4+((x+y+sample(seed,name,x,y))&1)
    elif name in PLANTS:
        indices=[0]*256; h=sample(seed,name,1,2); center=6+h%4
        limit=8 if name=="wheat_young" else 12 if name=="wheat_middle" else 15
        for y in range(limit):
            x=wrap(center+round(math.sin((y+h%5)*.65)))
            indices[y*SIZE+x]=2+y%5
            if y in (4,7,10,13):
                for dx in (-2,-1,1,2): indices[y*SIZE+wrap(x+dx)]=1+(y+dx)%6
        if name=="flower":
            for dx,dy in ((0,0),(1,0),(-1,0),(0,1),(0,-1)): indices[wrap(13+dy)*SIZE+wrap(center+dx)]=6
    elif name=="fire":
        indices=[0]*256
        for y in range(15):
            center=7+round(math.sin((y+seed%9)*.9)); half=max(1,5-y//3)
            for x in range(center-half,center+half+1): indices[y*SIZE+wrap(x)]=1+min(5,y//3)
    elif name in {"water","lava","ice"}:
        indices=[(y+round(1.4*math.sin((x+seed%7)*math.tau/9))+x//5)%6 for y in range(SIZE) for x in range(SIZE)]
    elif name in {"farmland","wet_farmland"}:
        base=generate_dirt(name,seed); indices=[0 if (x+round(math.sin(y*.7)))%5==0 else max(1,v) for y in range(SIZE) for x,v in enumerate(base[y*SIZE:(y+1)*SIZE])]
    elif name=="white_wool":
        indices=[1 if (x+y)%5==0 or (x-y)%7==0 else 2+(sample(seed,name,x//2,y//2)%4) for y in range(SIZE) for x in range(SIZE)]
    elif name in {"cactus_side","reeds"}:
        indices=[1+((x+round(math.sin(y*.7)))%5) for y in range(SIZE) for x in range(SIZE)]
    elif name in {"crafting_table","furnace","chest","white_bed"}:
        indices=[]
        for y in range(SIZE):
            for x in range(SIZE):
                border=(x in (1,14) and 1<=y<=14) or (y in (1,14) and 1<=x<=14)
                panel=x in (5,10) and 4<=y<=11
                indices.append(0 if border else 1 if panel else 2+sample(seed,name,x//2,y//2)%4)
    return indices

def resolve_seed(seed,name,local_seeds=None):
    return int((local_seeds or {}).get(name,seed))

def generate_texture(name,seed,local_seeds=None):
    local=resolve_seed(seed,name,local_seeds)
    indices=generate_generic(name,local)
    indices=generate_special(name,local,indices)
    if name in NATURAL:
        indices=break_flat_rectangles(break_axis_runs(indices))
    palette=PALETTES[name]
    return [palette[max(0,min(len(palette)-1,int(i)))] for i in indices]

def png_bytes(width,height,pixels):
    raw=bytearray()
    for y in range(height):
        raw.append(0)
        for pixel in pixels[y*width:(y+1)*width]: raw.extend(pixel)
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    header=struct.pack(">IIBBBBB",width,height,8,6,0,0,0)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",header)+chunk(b"IDAT",zlib.compress(bytes(raw),9))+chunk(b"IEND",b"")

def write_png(path,width,height,pixels):
    path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(png_bytes(width,height,pixels))

def read_generated_png(path):
    data=path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"): raise ValueError(f"not a PNG: {path}")
    offset=8; compressed=bytearray(); width=height=0
    while offset<len(data):
        length=struct.unpack(">I",data[offset:offset+4])[0]; kind=data[offset+4:offset+8]
        payload=data[offset+8:offset+8+length]; offset+=12+length
        if kind==b"IHDR":
            width,height,depth,color,_,_,_=struct.unpack(">IIBBBBB",payload)
            if depth!=8 or color!=6: raise ValueError(f"expected 8-bit RGBA PNG: {path}")
        elif kind==b"IDAT": compressed.extend(payload)
        elif kind==b"IEND": break
    raw=zlib.decompress(compressed); stride=width*4; pixels=[]
    for y in range(height):
        start=y*(stride+1)
        if raw[start]!=0: raise ValueError(f"unsupported PNG filter in {path}")
        row=raw[start+1:start+1+stride]; pixels.extend(tuple(row[x:x+4]) for x in range(0,stride,4))
    return width,height,pixels

def luminance(c): return c[0]*.2126+c[1]*.7152+c[2]*.0722
def palette_contrast(palette):
    values=[luminance(c) for c in palette if c[3]==255]; return max(values)-min(values)
def color_distance(a,b): return abs(luminance(a)-luminance(b))

def connected_components(pixels,color):
    pending={(x,y) for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x]==color}; result=[]
    while pending:
        start=pending.pop(); comp={start}; queue=[start]
        while queue:
            for n in neighbors4(*queue.pop()):
                if n in pending: pending.remove(n); comp.add(n); queue.append(n)
        result.append(comp)
    return result

def structure_metrics(pixels):
    lum=[luminance(c) if c[3] else 0 for c in pixels]
    runs=[]
    for y in range(SIZE):
        run=1
        for x in range(1,SIZE):
            if color_distance(pixels[y*SIZE+x],pixels[y*SIZE+x-1])<4: run+=1
            else: runs.append(run); run=1
        runs.append(run)
    for x in range(SIZE):
        run=1
        for y in range(1,SIZE):
            if color_distance(pixels[y*SIZE+x],pixels[(y-1)*SIZE+x])<4: run+=1
            else: runs.append(run); run=1
        runs.append(run)
    comps=[]
    for color in set(pixels):
        for comp in connected_components(pixels,color):
            xs=[p[0] for p in comp]; ys=[p[1] for p in comp]
            area=len(comp); box=(max(xs)-min(xs)+1)*(max(ys)-min(ys)+1)
            comps.append((area,area/box if box else 0,(max(xs)-min(xs)+1)/(max(ys)-min(ys)+1)))
    def correlation(dx,dy):
        mean=sum(lum)/len(lum); a=[]; b=[]
        for y in range(SIZE):
            for x in range(SIZE): a.append(lum[y*SIZE+x]-mean); b.append(lum[wrap(y+dy)*SIZE+wrap(x+dx)]-mean)
        den=math.sqrt(sum(v*v for v in a)*sum(v*v for v in b)); return sum(x*y for x,y in zip(a,b))/den if den else 0
    internal=[]; seam=[]
    for y in range(SIZE):
        for x in range(SIZE-1): internal.append(color_distance(pixels[y*SIZE+x],pixels[y*SIZE+x+1]))
        seam.append(color_distance(pixels[y*SIZE+15],pixels[y*SIZE]))
    for x in range(SIZE):
        for y in range(SIZE-1): internal.append(color_distance(pixels[y*SIZE+x],pixels[(y+1)*SIZE+x]))
        seam.append(color_distance(pixels[15*SIZE+x],pixels[x]))
    center_rows=[sum(lum[y*SIZE+x] for x in range(SIZE))/SIZE for y in range(6,10)]
    center_cols=[sum(lum[y*SIZE+x] for y in range(SIZE))/SIZE for x in range(6,10)]
    outer=sum(lum)/256
    return {"longest_run":max(runs),"largest_component":max(a for a,_,_ in comps)/256,
            "largest_rectangularity":max(r for a,r,_ in comps if a>=12) if any(a>=12 for a,_,_ in comps) else 0,
            "periodicity":max(abs(correlation(s,0)) for s in (2,4,8)),
            "seam_ratio":(sum(seam)/len(seam))/max(1,sum(internal)/len(internal)),
            "center_cross":max(abs(sum(center_rows)/4-outer),abs(sum(center_cols)/4-outer))/max(1,palette_contrast(list(set(pixels)))),
            "transitions":sum(pixels[y*SIZE+x]!=pixels[y*SIZE+wrap(x+1)] for y in range(SIZE) for x in range(SIZE))+sum(pixels[y*SIZE+x]!=pixels[wrap(y+1)*SIZE+x] for y in range(SIZE) for x in range(SIZE))}

def validate_texture(path):
    width,height,pixels=read_generated_png(path); errors=[]; name=path.stem
    def fail(rule,value,limit): errors.append(f"{name}: {rule}: detected {value}, allowed {limit}")
    if (width,height)!=(SIZE,SIZE): fail("dimensions",f"{width}x{height}","16x16"); return errors
    colors=set(pixels); palette_limit=len(PALETTES.get(name,[])) or 16
    if len(colors)>palette_limit: fail("palette size",len(colors),f"<= {palette_limit}")
    if name in PALETTES and not 5<=len(colors)<=8: fail("ordinary palette usage",len(colors),"5..8")
    if any(c[3] not in (0,255) for c in pixels): fail("alpha values","non-binary","0 or 255")
    if any(c[3] and c[:3]==(0,0,0) for c in pixels): fail("opaque black outline",1,0)
    if name in HIGH_CONTRAST_NAMES and palette_contrast(PALETTES[name])<palette_contrast(PALETTES["stone"])*1.15:
        fail("rare-material contrast",round(palette_contrast(PALETTES[name]),2),f">= {palette_contrast(PALETTES['stone'])*1.15:.2f}")
    metrics=structure_metrics(pixels)
    if metrics["seam_ratio"]>2.60: fail("toroidal seam discontinuity",f"{metrics['seam_ratio']:.2f}","<= 2.60x interior")
    if name in NATURAL:
        if metrics["longest_run"]>8: fail("near-color straight run",metrics["longest_run"],"<= 8 pixels")
        if metrics["largest_component"]>.34: fail("largest flat connected area",f"{metrics['largest_component']:.3f}","<= 0.340")
        if metrics["largest_rectangularity"]>.96: fail("flat-region rectangularity",f"{metrics['largest_rectangularity']:.3f}","<= 0.960")
        if metrics["periodicity"]>.76: fail("2/4/8-pixel autocorrelation",f"{metrics['periodicity']:.3f}","<= 0.760")
        if metrics["center_cross"]>.31: fail("center-axis luminance bias",f"{metrics['center_cross']:.3f}","<= 0.310")
        if not 125<=metrics["transitions"]<=410: fail("meso-frequency transition count",metrics["transitions"],"125..410")
    if name=="grass_side":
        grass=set(PALETTES[name][4:]); rows=[y for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x] in grass]
        if not rows or sum(rows)/len(rows)>=6: fail("grass-side top layer mean row",f"{sum(rows)/len(rows) if rows else 99:.2f}","< 6")
    if name.endswith("_ore"):
        ore=set(PALETTES[name][3:]); cells={(x,y) for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x] in ore}; count=0
        while cells:
            count+=1; queue=[cells.pop()]
            while queue:
                for n in neighbors4(*queue.pop()):
                    if n in cells: cells.remove(n); queue.append(n)
        if not 2<=count<=4: fail("connected ore clusters",count,"2..4")
    return errors

def generate(output,seed,local_seeds=None):
    output.mkdir(parents=True,exist_ok=True)
    for name in NAMES: write_png(output/f"{name}.png",SIZE,SIZE,generate_texture(name,seed,local_seeds))

def validate(output):
    errors=[]
    for name in NAMES:
        path=output/f"{name}.png"
        errors.extend([f"missing texture: {path}"] if not path.exists() else validate_texture(path))
    if errors: raise ValueError("\n".join(errors))

def build_atlas(output,seed,local_seeds=None):
    validate(output); columns=4; rows=math.ceil(len(NAMES)/columns); grid=max(columns,rows)
    atlas=[(0,0,0,0)]*(grid*SIZE*grid*SIZE)
    metadata={"version":1,"tile_size":SIZE,"grid_size":grid,"filter":"nearest","seed":seed,"textures":{}}
    for index,name in enumerate(NAMES):
        _,_,pixels=read_generated_png(output/f"{name}.png"); tx,ty=index%grid,index//grid
        for y in range(SIZE):
            begin=(ty*SIZE+y)*grid*SIZE+tx*SIZE; atlas[begin:begin+SIZE]=pixels[y*SIZE:(y+1)*SIZE]
        entry={"index":index,"x":tx,"y":ty,"source":f"{name}.png","generated":True}
        if local_seeds and name in local_seeds: entry["local_seed"]=int(local_seeds[name])
        metadata["textures"][name]=entry
    write_png(output/"atlas.png",grid*SIZE,grid*SIZE,atlas)
    (output/"atlas.json").write_text(json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

# Compact 3x5 bitmap glyphs keep contact sheets dependency-free.
FONT={c:bits for c,bits in {
"0":"111101101101111","1":"010110010010111","2":"111001111100111","3":"111001111001111",
"4":"101101111001001","5":"111100111001111","6":"111100111101111","7":"111001010010010",
"8":"111101111101111","9":"111101111001111","a":"010101111101101","b":"100100110101110",
"c":"011100100100011","d":"001001011101011","e":"010101111100011","f":"011100110100100",
"g":"011100101101010","h":"100100110101101","i":"010000110010111","j":"001001001101010",
"k":"100101110101101","l":"100100100100111","m":"101111111101101","n":"110101101101101",
"o":"010101101101010","p":"110101110100100","q":"010101101011001","r":"110101110101101",
"s":"011100010001110","t":"111010010010010","u":"101101101101111","v":"101101101101010",
"w":"101101111111101","x":"101101010101101","y":"101101010010010","z":"111001010100111",
"_":"000000000000111","-":"000000111000000",":":"000010000010000"}.items()}

def blit(canvas,width,x0,y0,tile,tw,th,scale=1):
    for y in range(th):
        for x in range(tw):
            color=tile[y*tw+x]
            for sy in range(scale):
                start=(y0+y*scale+sy)*width+x0+x*scale
                canvas[start:start+scale]=[color]*scale

def draw_text(canvas,width,x,y,label):
    for char in label.lower():
        bits=FONT.get(char,"000000000000000")
        for gy in range(5):
            for gx in range(3):
                if bits[gy*3+gx]=="1": canvas[(y+gy)*width+x+gx]=(235,235,228,255)
        x+=4

def build_contact_sheet(output,seed,count,local_seeds=None):
    # Each cell contains the source tile at 2x and an 8x8 repeat at native scale.
    label_w=72; cell_w=168; cell_h=136; width=label_w+cell_w*count; height=cell_h*len(NAMES)
    canvas=[(25,27,29,255)]*(width*height)
    candidate_seeds=[]
    for col in range(count): candidate_seeds.append(seed if col==0 else mix64(seed+col*0x9e37)&0x7fffffff)
    for row,name in enumerate(NAMES):
        y0=row*cell_h; draw_text(canvas,width,3,y0+4,name[:17])
        for col,candidate in enumerate(candidate_seeds):
            chosen=(local_seeds or {}).get(name,candidate) if col==0 else candidate
            pixels=generate_texture(name,chosen); x0=label_w+col*cell_w
            draw_text(canvas,width,x0,y0+2,str(chosen)); blit(canvas,width,x0,y0+10,pixels,SIZE,SIZE,2)
            for ty in range(8):
                for tx in range(8): blit(canvas,width,x0+38+tx*SIZE,y0+8+ty*SIZE,pixels,SIZE,SIZE)
    write_png(output/"contact_sheet.png",width,height,canvas)
    (output/"contact_sheet.json").write_text(json.dumps({"base_seed":seed,"candidate_seeds":candidate_seeds,
        "local_seeds":local_seeds or {},"note":"development preview; excluded from atlas"},indent=2,sort_keys=True)+"\n")

def parse_local_seeds(values):
    result={}
    for value in values:
        if "=" not in value: raise ValueError(f"invalid --local-seed '{value}', expected material=seed")
        name,raw=value.split("=",1)
        if name not in NAMES: raise ValueError(f"unknown --local-seed material '{name}'")
        result[name]=int(raw,0)
    return result

def parse_args(argv=None):
    parser=argparse.ArgumentParser(); parser.add_argument("--generate",action="store_true")
    parser.add_argument("--validate",action="store_true"); parser.add_argument("--build-atlas",action="store_true")
    parser.add_argument("--seed",type=int,default=DEFAULT_SEED); parser.add_argument("--output",type=Path,default=Path("assets/textures/generated"))
    parser.add_argument("--candidate-count",type=int,default=1); parser.add_argument("--contact-sheet",action="store_true")
    parser.add_argument("--local-seed",action="append",default=[],metavar="MATERIAL=SEED")
    return parser.parse_args(argv)

def main(argv=None):
    args=parse_args(argv)
    if not (args.generate or args.validate or args.build_atlas or args.contact_sheet): raise SystemExit("select --generate, --validate, --build-atlas, or --contact-sheet")
    try:
        if args.candidate_count<1: raise ValueError("--candidate-count must be at least 1")
        local_seeds=parse_local_seeds(args.local_seed)
        if args.generate: generate(args.output,args.seed,local_seeds)
        if args.validate: validate(args.output)
        if args.build_atlas: build_atlas(args.output,args.seed,local_seeds)
        if args.contact_sheet: build_contact_sheet(args.output,args.seed,args.candidate_count,local_seeds)
    except (OSError,ValueError) as error: print(error,file=sys.stderr); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
