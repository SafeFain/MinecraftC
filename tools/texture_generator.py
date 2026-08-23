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
GENERATOR_CATEGORIES = ("block_texture", "item_sprite", "block_item_icon")
ENTITY_NAMES = ("cow", "pig", "sheep", "chicken", "zombie", "skeleton",
                "spider", "blastling", "item")
ENTITY_SKIN_NAMES = ENTITY_NAMES[:-1] + ("player",)
ENTITY_SKIN_SIZE = 64
ENTITY_SKIN_LAYOUT = {
    "head_front": 0, "head_back": 1, "head_left": 2, "head_right": 3,
    "head_top": 4, "head_bottom": 5,
    "body_front": 6, "body_back": 7, "body_left": 8, "body_right": 9,
    "body_top": 10, "body_bottom": 11,
    "limb_primary": 12, "limb_secondary": 13, "detail": 14,
    "fallback": 15,
}
ENTITY_PALETTES = {
    "cow": ((61,39,25,255),(82,49,29,255),(111,67,38,255),
            (151,105,68,255),(213,199,171,255),(238,229,207,255)),
    "pig": ((147,67,78,255),(180,89,101,255),(207,112,124,255),
            (226,139,149,255),(239,166,174,255),(248,192,198,255)),
    "sheep": ((133,126,112,255),(160,153,139,255),(185,181,168,255),
              (207,204,193,255),(226,224,215,255),(241,240,232,255)),
    "chicken": ((157,47,38,255),(211,66,43,255),(209,139,31,255),
                (232,184,64,255),(218,216,199,255),(245,244,229,255)),
    "zombie": ((32,67,44,255),(46,88,54,255),(64,108,66,255),
               (55,94,96,255),(65,116,123,255),(91,137,130,255)),
    "skeleton": ((92,84,69,255),(126,116,95,255),(159,149,124,255),
                 (190,181,154,255),(218,209,181,255),(239,233,207,255)),
    "spider": ((32,22,20,255),(48,30,26,255),(68,40,33,255),
               (92,52,39,255),(137,25,24,255),(213,42,31,255)),
    "blastling": ((29,69,30,255),(39,94,37,255),(53,119,43,255),
                  (70,146,48,255),(97,174,58,255),(132,204,76,255)),
    "item": ((91,55,21,255),(122,76,25,255),(158,104,31,255),
             (192,137,39,255),(223,174,58,255),(244,207,91,255)),
    "player": ((34,52,76,255),(48,72,101,255),(61,93,126,255),
               (88,126,154,255),(181,126,91,255),(229,177,132,255)),
}
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
    "snow_layer", "fire", "glass", "tnt", "obsidian", "dandelion",
    "blue_orchid", "allium", "oxeye_daisy", "sunflower_bottom",
    "sunflower_top", "cloud", "limestone", "basalt", "tuff",
    "coarse_dirt", "mud", "packed_ice", "black_sand", "granite",
    "aether_grass_top", "aether_grass_side", "aether_soil", "cloudstone",
    "sunstone", "skyroot_log", "skyroot_log_top", "skyroot_leaves",
    "star_crystal", "starflower", "copper_ore",
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
    "glass":(180,214,224), "tnt":(184,45,36), "obsidian":(39,27,55),
    "dandelion":(225,184,34), "blue_orchid":(58,151,196),
    "allium":(151,86,180), "oxeye_daisy":(221,220,195),
    "sunflower_bottom":(82,133,46), "sunflower_top":(224,169,28),
    "cloud":(220,225,229),
    "limestone":(184,181,163), "basalt":(48,50,53), "tuff":(79,91,83),
    "coarse_dirt":(104,72,42), "mud":(61,57,54),
    "packed_ice":(91,153,195), "black_sand":(49,46,49),
    "granite":(142,91,77),
    "aether_grass_top":(90,175,104), "aether_grass_side":(74,132,73),
    "aether_soil":(116,82,52), "cloudstone":(156,183,194),
    "sunstone":(214,166,72), "skyroot_log":(132,96,56),
    "skyroot_log_top":(171,132,76), "skyroot_leaves":(90,164,82),
    "star_crystal":(65,184,222), "starflower":(150,117,226),
}
TRANSPARENT = {"tall_grass","flower","reeds","torch","wheat_young","wheat_middle",
               "wheat_mature","oak_sapling","birch_sapling","spruce_sapling",
               "jungle_sapling","acacia_sapling","fire","glass","dandelion",
               "blue_orchid","allium","oxeye_daisy","sunflower_bottom",
               "sunflower_top", "starflower"}

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
           "red_sand","terracotta","podzol_top","moss","snow","snow_layer","cobblestone",
           "cloud","limestone","basalt","tuff","coarse_dirt","mud",
           "packed_ice","black_sand","granite", "aether_grass_top",
           "aether_grass_side", "aether_soil", "cloudstone", "sunstone",
           "skyroot_log", "star_crystal"}
DIRECTIONAL = {"oak_planks","oak_log","birch_log","spruce_log","jungle_log","acacia_log",
               "skyroot_log",
               "farmland","wet_farmland","cactus_side","reeds"}
LEAF_NAMES = {"leaves","birch_leaves","spruce_leaves","jungle_leaves","acacia_leaves",
              "skyroot_leaves"}

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
    if name in {"dirt","gravel","podzol_top","coarse_dirt","mud"}: return generate_dirt(name,seed)
    if name=="grass_top" or name=="moss": return generate_grass(seed^sample(seed,name,0,0))
    if name in {"stone","bedrock","deepslate","clay","terracotta","cobblestone",
                "limestone","basalt","tuff","granite"}: return generate_stone(name,seed)
    if name in {"sand","red_sand","black_sand","snow","snow_layer","packed_ice"}: return generate_sand(name,seed)
    if name.endswith("_ore"): return generate_ore(name,seed)
    if name in LEAF_NAMES: return generate_leaves(name,seed)
    if name.endswith("_log"): return generate_wood_side(name,seed)
    if name.endswith("_log_top"): return generate_log_top(name,seed)
    if name.endswith("_planks"): return generate_planks(name,seed)
    field=quantize(macro_field(seed,name,6))
    add_sparse_micro(field,seed,name,12)
    return field

PLANTS={"tall_grass","flower","reeds","torch","wheat_young","wheat_middle","wheat_mature",
        "oak_sapling","birch_sapling","spruce_sapling","jungle_sapling","acacia_sapling",
        "dandelion","blue_orchid","allium","oxeye_daisy","sunflower_bottom","sunflower_top",
        "starflower"}

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
        if name in {"flower","dandelion","blue_orchid","allium","oxeye_daisy","sunflower_top"}:
            flower_y=12 if name=="sunflower_top" else 13
            for dx,dy in ((0,0),(1,0),(-1,0),(0,1),(0,-1)):
                indices[wrap(flower_y+dy)*SIZE+wrap(center+dx)]=6
        if name=="sunflower_bottom":
            for y in range(15):
                for dx in (-1,0,1): indices[y*SIZE+wrap(center+dx)]=2+(y+dx)%4
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
    elif name=="glass":
        indices=[]
        for y in range(SIZE):
            for x in range(SIZE):
                border=x in (0,15) or y in (0,15)
                glint=(x-y) in (-1,0,1) and 3<=x<=7
                indices.append(3+(x+y)%3 if border else 2 if glint else 0)
    elif name=="tnt":
        indices=[]
        for y in range(SIZE):
            for x in range(SIZE):
                band=6<=y<=9
                fuse=(x in (7,8) and y<3)
                indices.append(5 if fuse else 1 if band else 2+(x+y)%4)
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

def generate_entity_texture(name,seed):
    """Generate a wrapping material swatch, not a face portrait."""
    if name not in ENTITY_PALETTES: raise ValueError(f"unknown entity material '{name}'")
    values=[v*.55 for v in macro_field(seed,name,6)]
    for i in range(7):
        h=sample(seed,name,i,131)
        blob=grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),3+(h>>16)%8,i)
        tone=(-.55,.38,.66)[i%3]
        for x,y in blob: values[y*SIZE+x]+=tone
    indices=quantize(values)
    if name=="cow":
        for i in range(3):
            h=sample(seed,name,i,137)
            for x,y in grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),10+(h>>16)%10,20+i):
                indices[y*SIZE+x]=4+(x+y+i)%2
    elif name=="sheep":
        indices=[3+((x+y+indices[y*SIZE+x])%3) for y in range(SIZE) for x in range(SIZE)]
        for i in range(9):
            h=sample(seed,name,i,141)
            indices[((h>>8)%SIZE)*SIZE+h%SIZE]=2
    elif name=="chicken":
        indices=[4+(indices[y*SIZE+x]&1) for y in range(SIZE) for x in range(SIZE)]
        for i in range(8):
            h=sample(seed,name,i,139); x=h%SIZE; y=(h>>8)%SIZE
            indices[y*SIZE+x]=i%4
    elif name=="zombie":
        for y in range(6,10):
            for x in range(SIZE):
                if (x+y)%5: indices[y*SIZE+x]=3+(indices[y*SIZE+x]&1)
    elif name=="skeleton":
        for i in range(6):
            h=sample(seed,name,i,149); x=h%SIZE; y=(h>>8)%SIZE
            for step in range(2+(h>>16)%3):
                indices[wrap(y+step)*SIZE+wrap(x+step//2)]=step%2
    elif name=="spider":
        indices=[min(3,i) for i in indices]
        for i in range(5):
            h=sample(seed,name,i,151)
            indices[((h>>8)%SIZE)*SIZE+h%SIZE]=4+i%2
    palette=ENTITY_PALETTES[name]
    return [palette[max(0,min(5,index))] for index in indices]

def _entity_part_palette(name, part):
    base=ENTITY_PALETTES[name]
    overrides={
        ("cow","head"): ((47,31,23,255),(68,43,29,255),(92,57,35,255),
                          (126,82,52,255),(190,154,112,255),(232,211,178,255)),
        ("sheep","head"): ((62,55,48,255),(84,76,65,255),(111,101,86,255),
                            (143,132,113,255),(181,172,151,255),(218,211,190,255)),
        ("zombie","body"): ((25,67,70,255),(31,82,85,255),(38,98,99,255),
                             (47,116,113,255),(61,135,126,255),(79,153,140,255)),
        ("zombie","secondary"): ((42,32,63,255),(55,41,82,255),(68,51,101,255),
                                  (82,63,119,255),(99,78,138,255),(119,97,157,255)),
        ("chicken","primary"): ((111,70,17,255),(145,91,20,255),(180,114,24,255),
                                 (211,143,31,255),(235,174,48,255),(247,202,78,255)),
        ("chicken","head"): ((169,166,151,255),(187,185,170,255),(205,203,188,255),
                              (220,218,204,255),(235,233,220,255),(248,247,237,255)),
        ("chicken","body"): ((166,164,150,255),(184,182,168,255),(202,200,186,255),
                              (218,216,202,255),(234,232,219,255),(247,246,236,255)),
        ("chicken","secondary"): ((158,157,145,255),(178,177,164,255),(198,197,184,255),
                                   (216,215,202,255),(233,232,220,255),(247,246,237,255)),
    }
    return overrides.get((name,part),base)

def _cube_coordinate(face,x,y):
    a=2*x-15; b=15-2*y
    return {
        "front":(a,b,-15), "back":(-a,b,15),
        "left":(-15,b,-a), "right":(15,b,a),
        "top":(a,15,-b), "bottom":(a,-15,b),
    }[face]

def _entity_surface_indices(name,part,face,seed):
    """Sample a continuous cube-space field so adjacent cuboid faces agree."""
    salt=sample(seed,name,len(part),sum(ord(c) for c in part))
    p0=(salt&255)/19.0; p1=((salt>>8)&255)/23.0; p2=((salt>>16)&255)/29.0
    result=[]
    for y in range(SIZE):
        for x in range(SIZE):
            cx,cy,cz=_cube_coordinate(face,x,y)
            value=(math.sin((cx+p0)/8.2)+math.sin((cy+p1)/9.7)+
                   math.sin((cz+p2)/7.6)+.55*math.sin((cx+cy-cz+p0)/12.4))
            result.append(max(0,min(5,int(round(2.5+value*.78)))))
    return result

def _entity_wrapping_indices(name,part,seed):
    values=macro_field(seed,name+"_"+part,7)
    # Quantize a smoothed field; this avoids the old checkerboard transitions.
    indices=quantize(values)
    return [max(0,min(5,index)) for index in indices]

def _paint_rect(tile,x0,y0,x1,y1,color):
    for y in range(max(0,y0),min(SIZE,y1)):
        for x in range(max(0,x0),min(SIZE,x1)): tile[y*SIZE+x]=color

def _paint_entity_face(name,tile):
    dark=(20,18,18,255); blackish=(14,16,14,255)
    if name=="cow":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,4,8,12,14,(216,181,139,255))
        _paint_rect(tile,5,10,7,12,(66,43,34,255)); _paint_rect(tile,9,10,11,12,(66,43,34,255))
    elif name=="pig":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,4,8,12,14,(239,166,174,255))
        _paint_rect(tile,5,10,7,12,(116,54,67,255)); _paint_rect(tile,9,10,11,12,(116,54,67,255))
    elif name=="sheep":
        _paint_rect(tile,3,5,5,7,dark); _paint_rect(tile,11,5,13,7,dark)
        _paint_rect(tile,6,10,10,12,(66,57,49,255))
    elif name=="chicken":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,5,7,11,10,(232,166,47,255)); _paint_rect(tile,6,10,10,13,(190,50,38,255))
    elif name=="zombie":
        _paint_rect(tile,3,4,6,7,(26,40,29,255)); _paint_rect(tile,10,4,13,7,(26,40,29,255))
        _paint_rect(tile,5,10,11,12,(31,55,38,255)); _paint_rect(tile,7,9,9,10,(43,73,48,255))
    elif name=="skeleton":
        _paint_rect(tile,2,3,6,8,blackish); _paint_rect(tile,10,3,14,8,blackish)
        _paint_rect(tile,7,7,9,10,(48,44,37,255)); _paint_rect(tile,5,12,11,13,(72,65,54,255))
    elif name=="spider":
        red=(202,38,31,255); bright=(239,65,49,255)
        for x,y in ((3,5),(6,4),(10,4),(13,5),(5,8),(11,8)):
            _paint_rect(tile,x-1,y-1,x+1,y+1,red)
            tile[(y-1)*SIZE+x-1]=bright
    elif name=="blastling":
        _paint_rect(tile,3,3,6,8,blackish); _paint_rect(tile,10,3,13,8,blackish)
        _paint_rect(tile,6,8,10,11,blackish); _paint_rect(tile,4,10,7,14,blackish)
        _paint_rect(tile,9,10,12,14,blackish); _paint_rect(tile,7,12,9,15,blackish)
    elif name=="player":
        _paint_rect(tile,3,5,5,7,(34,30,26,255)); _paint_rect(tile,11,5,13,7,(34,30,26,255))
        _paint_rect(tile,6,10,10,11,(128,72,54,255))

def _paint_entity_body(name,tile):
    if name=="skeleton":
        bone=(205,197,171,255); shadow=(82,75,62,255)
        _paint_rect(tile,7,2,9,14,bone)
        for y in (4,7,10):
            _paint_rect(tile,3,y,7,y+1,shadow); _paint_rect(tile,9,y,13,y+1,shadow)
    elif name=="zombie":
        _paint_rect(tile,6,1,10,3,(28,70,72,255))
    elif name=="player":
        _paint_rect(tile,1,1,15,15,(42,78,118,255))
        _paint_rect(tile,1,11,15,15,(31,44,67,255))

def generate_entity_skin(name,seed):
    """Generate one original 64x64 semantic skin atlas for a runtime mob."""
    if name not in ENTITY_SKIN_NAMES: raise ValueError(f"unknown entity skin '{name}'")
    atlas=[(0,0,0,255)]*(ENTITY_SKIN_SIZE*ENTITY_SKIN_SIZE)
    for semantic,index in ENTITY_SKIN_LAYOUT.items():
        part,_,face=semantic.partition("_")
        if semantic.startswith("limb_primary"):
            part="primary"; indices=_entity_wrapping_indices(name,part,seed)
        elif semantic.startswith("limb_secondary"):
            part="secondary"; indices=_entity_wrapping_indices(name,part,seed)
        elif semantic in {"detail","fallback"}:
            part=semantic; indices=_entity_wrapping_indices(name,part,seed)
        else:
            indices=_entity_surface_indices(name,part,face,seed)
        palette=_entity_part_palette(name,part)
        tile=[palette[i] for i in indices]
        if semantic=="head_front": _paint_entity_face(name,tile)
        elif semantic=="body_front": _paint_entity_body(name,tile)
        tx,ty=index%4,index//4
        for y in range(SIZE):
            start=(ty*SIZE+y)*ENTITY_SKIN_SIZE+tx*SIZE
            atlas[start:start+SIZE]=tile[y*SIZE:(y+1)*SIZE]
    return atlas

def build_entity_skins(output,seed):
    skin_dir=output/"entity_skins"; skin_dir.mkdir(parents=True,exist_ok=True)
    metadata={"version":1,"width":ENTITY_SKIN_SIZE,"height":ENTITY_SKIN_SIZE,
              "tile_size":SIZE,"columns":4,"rows":4,"filter":"nearest",
              "seed":seed,"layout":ENTITY_SKIN_LAYOUT,"entities":{}}
    for name in ENTITY_SKIN_NAMES:
        pixels=generate_entity_skin(name,seed)
        path=skin_dir/f"{name}.png"; write_png(path,ENTITY_SKIN_SIZE,ENTITY_SKIN_SIZE,pixels)
        metadata["entities"][name]={"source":f"entity_skins/{name}.png"}
    (output/"entity_skins.json").write_text(
        json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

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

def load_item_icon_definitions(path):
    data=json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("version") != 1: raise ValueError("item icon definitions require version 1")
    for category in data.get("generator_categories",[]):
        if category not in GENERATOR_CATEGORIES: raise ValueError(f"unknown generator category '{category}'")
    required={"sword","pickaxe","axe","shovel","hoe","stick","ingot","gem","coal","torch"}
    missing=required-set(data.get("templates",{}))
    if missing: raise ValueError("missing item templates: "+", ".join(sorted(missing)))
    for name in ("wood","stone","copper","iron","gold"):
        shades=data.get("materials",{}).get(name)
        if not isinstance(shades,list) or len(shades)<4:
            raise ValueError(f"material '{name}' requires at least four colors")
        for color in shades:
            if len(color)!=3 or any(not isinstance(c,int) or c<0 or c>255 for c in color):
                raise ValueError(f"invalid RGB color in material '{name}'")
    return data

def _rgba(rgb): return tuple(rgb)+(255,)
def _put(canvas,x,y,color):
    if 0<=x<SIZE and 0<=y<SIZE: canvas[y*SIZE+x]=color
def _line(canvas,x0,y0,x1,y1,color,width=1):
    dx=abs(x1-x0); sx=1 if x0<x1 else -1; dy=-abs(y1-y0); sy=1 if y0<y1 else -1; err=dx+dy
    while True:
        for oy in range(-(width//2),width-width//2):
            for ox in range(-(width//2),width-width//2): _put(canvas,x0+ox,y0+oy,color)
        if x0==x1 and y0==y1: break
        twice=2*err
        if twice>=dy: err+=dy; x0+=sx
        if twice<=dx: err+=dx; y0+=sy

def generate_item_sprite(template,material,definitions):
    """Generate crisp binary-alpha sprites; coordinates use PNG top-left origin."""
    if template not in definitions["templates"]: raise ValueError(f"unknown item template '{template}'")
    if material not in definitions["materials"]: raise ValueError(f"unknown item material '{material}'")
    shades=[_rgba(c) for c in definitions["materials"][material]]
    outline=(max(16,shades[0][0]//2),max(14,shades[0][1]//2),max(12,shades[0][2]//2),255)
    handle=[_rgba(c) for c in definitions["handle_palette"]]
    image=[(0,0,0,0)]*(SIZE*SIZE)
    # Each tool is assembled in layer order: outline, handle, working part, highlight.
    if template in {"sword","pickaxe","axe","shovel","hoe"}:
        _line(image,4,13,11,6,outline,3); _line(image,4,13,11,6,handle[1],1)
        _line(image,3,14,6,11,outline,2); _line(image,3,14,5,12,handle[2],1)
        if template=="sword":
            _line(image,10,7,14,1,outline,4); _line(image,10,7,14,1,shades[2],2); _line(image,12,4,14,1,shades[3],1)
            _line(image,7,9,11,9,outline,3); _line(image,8,8,11,8,shades[1],1)
        elif template=="pickaxe":
            _line(image,7,5,14,2,outline,3); _line(image,7,5,14,2,shades[2],1); _line(image,8,4,13,2,shades[3],1)
        elif template=="axe":
            for y in range(2,7):
                for x in range(9+(y>4),14-(y==6)): _put(image,x,y,outline)
            for y in range(3,6):
                for x in range(10,13): _put(image,x,y,shades[2 if x<12 else 1])
            _line(image,10,3,12,3,shades[3],1)
        elif template=="shovel":
            for x,y in ((12,2),(13,2),(11,3),(12,3),(13,3),(11,4),(12,4)): _put(image,x,y,outline)
            for x,y in ((12,2),(12,3),(11,3)): _put(image,x,y,shades[2])
            _put(image,12,2,shades[3])
        else:
            _line(image,10,5,14,3,outline,3); _line(image,10,5,14,3,shades[2],1); _put(image,13,3,shades[3])
    elif template=="stick":
        _line(image,4,13,12,3,outline,3); _line(image,4,13,12,3,handle[1],1); _line(image,9,6,12,3,handle[2],1)
    elif template=="ingot":
        for y,left,right in ((5,6,10),(6,4,12),(7,3,12),(8,3,11),(9,4,10),(10,5,9)):
            for x in range(left,right+1): _put(image,x,y,outline)
        for y,left,right in ((6,6,10),(7,5,11),(8,4,10),(9,5,9)):
            for x in range(left,right+1): _put(image,x,y,shades[2 if x+y<15 else 1])
        _line(image,6,6,10,6,shades[3],1)
    elif template=="gem":
        rows=((3,7,8),(4,5,10),(5,4,11),(6,3,12),(7,3,12),(8,4,11),(9,5,10),(10,7,8))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[2 if x+y<15 else 1])
        _line(image,6,5,9,4,shades[3],1)
    elif template=="coal":
        rows=((4,6,9),(5,4,11),(6,3,12),(7,3,12),(8,4,12),(9,5,11),(10,6,9))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[1+(x+y)%2])
        _put(image,6,5,shades[3]); _put(image,5,6,shades[2])
    elif template=="torch":
        _line(image,6,13,9,5,outline,4); _line(image,6,13,9,5,handle[1],2)
        for x,y,c in ((8,5,shades[1]),(9,5,shades[2]),(8,4,shades[2]),(9,4,shades[3]),(10,4,shades[2]),(9,3,shades[3])): _put(image,x,y,c)
    elif template in {"string","bow"}:
        for y in range(2,14):
            x=4+abs(7-y)//2
            _put(image,x,y,shades[2]); _put(image,11,y,shades[3])
        if template=="bow": _line(image,5,2,11,8,handle[1],2); _line(image,11,8,5,13,handle[1],2)
    elif template in {"feather","bone","arrow"}:
        _line(image,3,13,12,3,outline,3); _line(image,3,13,12,3,shades[2],1)
        if template=="feather":
            for x,y in ((5,11),(4,10),(7,9),(6,8),(9,7),(8,6)): _put(image,x,y,shades[3])
        elif template=="bone":
            for x,y in ((2,12),(3,14),(11,2),(13,3)): _put(image,x,y,shades[3])
        else:
            _line(image,10,5,13,2,shades[3],2); _line(image,3,13,3,10,handle[2],1)
    elif template in {"seeds","wheat"}:
        _line(image,7,13,8,4,shades[1],1)
        for x,y in ((6,11),(9,10),(6,8),(9,7),(7,5)):
            _put(image,x,y,shades[2]); _put(image,x+(1 if x<8 else -1),y-1,shades[3])
        if template=="wheat": _line(image,8,8,11,5,shades[2],1)
    elif template in {"food","leather"}:
        rows=((4,6,9),(5,4,11),(6,3,12),(7,3,12),(8,3,11),(9,4,10),(10,5,9),(11,6,8))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[1+(x+y)%2])
        _line(image,5,5,9,4,shades[3],1)
    elif template=="spawn_egg":
        rows=((2,7,8),(3,5,10),(4,4,11),(5,3,12),(6,3,12),(7,3,12),
              (8,4,11),(9,4,11),(10,5,10),(11,6,9),(12,7,8))
        for y,left,right in rows:
            for x in range(left,right+1):
                _put(image,x,y,outline if x in (left,right) else shades[2])
        for x,y in ((6,4),(10,5),(5,7),(9,8),(7,10)):
            _put(image,x,y,shades[0]); _put(image,x+1,y,shades[1])
        _put(image,7,3,shades[3]); _put(image,8,3,shades[3])
    elif template=="shield":
        for y,left,right in ((2,5,10),(3,4,11),(4,4,11),(5,4,11),(6,4,11),(7,5,10),(8,5,10),(9,6,9),(10,7,8)):
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[1+(x>7)])
        _line(image,5,3,9,3,shades[3],1)
    elif template in {"helmet","chestplate","leggings","boots"}:
        if template=="helmet": rows=((4,5,10),(5,4,11),(6,4,11),(7,4,11),(8,4,6),(8,9,11))
        elif template=="chestplate": rows=((3,3,6),(3,9,12),(4,3,12),(5,4,11),(6,4,11),(7,4,11),(8,4,11),(9,5,10),(10,5,10),(11,5,10))
        elif template=="leggings": rows=((3,4,11),(4,4,11),(5,5,10),(6,5,10),(7,5,7),(7,9,11),(8,5,7),(8,9,11),(9,5,7),(9,9,11),(10,5,7),(10,9,11),(11,5,7),(11,9,11))
        else: rows=((7,4,6),(7,9,11),(8,4,6),(8,9,11),(9,4,7),(9,8,11),(10,4,7),(10,8,11),(11,4,7),(11,8,11))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[2])
        _line(image,5,4,9,4,shades[3],1)
    elif template=="flint":
        for x,y in ((8,3),(7,4),(8,4),(6,5),(7,5),(8,5),(5,6),(6,6),(7,6),(5,7),(6,7),(5,8),(4,9)):
            _put(image,x,y,shades[3] if x+y<12 else shades[1])
    elif template=="flint_and_steel":
        _line(image,4,12,10,5,outline,4); _line(image,4,12,10,5,shades[1],2)
        _line(image,9,5,13,8,shades[3],2); _line(image,13,8,10,11,shades[2],2)
        _put(image,5,11,handle[2]); _put(image,6,10,handle[3])
    elif template=="gunpowder":
        for x,y in ((5,6),(7,5),(9,6),(6,8),(8,8),(10,9),(5,10),(8,11)):
            _put(image,x,y,shades[1+(x+y)%3])
            if (x+y)%2: _put(image,x+1,y,shades[2])
    return image

def generate_block_item_icon(top_pixels,side_pixels):
    """Compose a small nearest-neighbor isometric cube from top and side tiles."""
    image=[(0,0,0,0)]*(SIZE*SIZE)
    for y in range(4):
        for x in range(8):
            color=top_pixels[min(15,y*4)*SIZE+min(15,x*2)]
            _put(image,4+x-y,2+x//2+y,color)
            _put(image,5+x-y,2+x//2+y,color)
    for y in range(8):
        for x in range(6):
            color=side_pixels[min(15,y*2)*SIZE+min(15,x*3)]
            _put(image,2+x,6+y+x//3,color)
            shade=(max(1,color[0]*3//4),max(1,color[1]*3//4),max(1,color[2]*3//4),color[3])
            _put(image,8+x,6+y-x//3,shade)
    return image

def validate_item_sprite(pixels,name="item"):
    errors=[]
    if len(pixels)!=SIZE*SIZE: errors.append(f"{name}: dimensions must be 16x16")
    if any(p[3] not in (0,255) for p in pixels): errors.append(f"{name}: alpha values must be 0 or 255")
    opaque=[p for p in pixels if p[3]]
    if not opaque: errors.append(f"{name}: icon is empty")
    if sum(p[:3]==(0,0,0) for p in opaque)>max(2,len(opaque)//12): errors.append(f"{name}: excessive pure-black outline")
    return errors

def build_items_atlas(output,seed,definitions_path,block_definitions_path,override_dir=None,legacy_dir=None):
    definitions=load_item_icon_definitions(definitions_path)
    blocks=json.loads(Path(block_definitions_path).read_text(encoding="utf-8"))["blocks"]
    item_dir=output/"items"; item_dir.mkdir(parents=True,exist_ok=True)
    resolved=[]
    for name,spec in definitions["items"].items():
        override=Path(override_dir)/f"{name}.png" if override_dir else None
        legacy=Path(legacy_dir)/f"{name}.png" if legacy_dir else None
        source_kind="generated"; source_path=item_dir/f"{name}.png"
        pixels=None
        if override and override.exists(): source_kind="override"; source_path=override; _,_,pixels=read_generated_png(override)
        if pixels is None:
            category=spec["generator"]
            if category=="item_sprite": pixels=generate_item_sprite(spec["template"],spec["material"],definitions)
            elif category=="block_item_icon":
                block=blocks[spec["block"]]; top=block.get("top",block.get("all")); side=block.get("side",block.get("all",top))
                _,_,top_pixels=read_generated_png(output/f"{top}.png"); _,_,side_pixels=read_generated_png(output/f"{side}.png")
                pixels=generate_block_item_icon(top_pixels,side_pixels)
            else: raise ValueError(f"item '{name}' has invalid generator '{category}'")
            errors=validate_item_sprite(pixels,name)
            if errors: raise ValueError("\n".join(errors))
            write_png(source_path,SIZE,SIZE,pixels)
        if pixels is None and legacy and legacy.exists(): source_kind="legacy"; source_path=legacy; _,_,pixels=read_generated_png(legacy)
        if pixels is None: source_kind="missing"; pixels=generate_item_sprite("coal","stone",definitions)
        resolved.append((name,spec,source_kind,source_path,pixels))
    columns=8; rows=max(1,math.ceil(len(resolved)/columns)); atlas=[(0,0,0,0)]*(columns*SIZE*rows*SIZE)
    metadata={"version":1,"tile_size":SIZE,"columns":columns,"rows":rows,"filter":"nearest","seed":seed,"priority":["override","generated","legacy","missing"],"items":{}}
    for index,(name,spec,kind,path,pixels) in enumerate(resolved):
        tx,ty=index%columns,index//columns
        for y in range(SIZE): atlas[(ty*SIZE+y)*columns*SIZE+tx*SIZE:(ty*SIZE+y)*columns*SIZE+(tx+1)*SIZE]=pixels[y*SIZE:(y+1)*SIZE]
        metadata["items"][name]={"index":index,"x":tx,"y":ty,"generator":spec["generator"],"source_kind":kind,"source":str(path)}
    write_png(output/"items_atlas.png",columns*SIZE,rows*SIZE,atlas)
    (output/"items_atlas.json").write_text(json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

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

def build_entity_atlas(output,seed):
    columns=3; rows=3; atlas=[(0,0,0,0)]*(columns*SIZE*rows*SIZE)
    metadata={"version":1,"tile_size":SIZE,"columns":columns,"rows":rows,
              "filter":"nearest","seed":seed,"entities":{}}
    entity_dir=output/"entities"; entity_dir.mkdir(parents=True,exist_ok=True)
    for index,name in enumerate(ENTITY_NAMES):
        pixels=generate_entity_texture(name,seed)
        write_png(entity_dir/f"{name}.png",SIZE,SIZE,pixels)
        tx,ty=index%columns,index//columns
        for y in range(SIZE):
            begin=(ty*SIZE+y)*columns*SIZE+tx*SIZE
            atlas[begin:begin+SIZE]=pixels[y*SIZE:(y+1)*SIZE]
        metadata["entities"][name]={"index":index,"x":tx,"y":ty,
                                    "source":f"entities/{name}.png"}
    write_png(output/"entity_atlas.png",columns*SIZE,rows*SIZE,atlas)
    (output/"entity_atlas.json").write_text(
        json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

def build_app_icon(output,seed):
    """Build an opaque, nearest-filtered grass cube application icon."""
    size=64
    canvas=[]
    for y in range(size):
        shade=27+(y*18)//(size-1)
        canvas.extend([(shade,shade+8,shade+14,255)]*size)
    top=generate_texture("grass_top",seed)
    side=generate_texture("grass_side",seed)
    dirt=generate_texture("dirt",seed)
    for y in range(20,57):
        inset=(y-20)//2
        for x in range(8+inset,33):
            u=((x-(8+inset))*15)//max(1,32-(8+inset))
            v=((y-20)*15)//36
            canvas[y*size+x]=side[v*SIZE+u]
        for x in range(33,57-inset):
            u=((x-33)*15)//max(1,(56-inset)-33)
            v=((y-20)*15)//36
            color=dirt[v*SIZE+u]
            canvas[y*size+x]=(max(1,color[0]-12),max(1,color[1]-12),
                              max(1,color[2]-12),255)
    for y in range(8,35):
        half_width=min(y-8,34-y)*2
        if half_width<0: continue
        left=32-half_width; right=32+half_width
        for x in range(left,right+1):
            u=((x-left)*15)//max(1,right-left)
            v=((y-8)*15)//26
            canvas[y*size+x]=top[v*SIZE+u]
    scale=16
    pixels=[]
    for color in canvas:
        pixels.extend([color]*scale)
    scaled=[]
    row=size*scale
    for y in range(size):
        source=pixels[y*row:(y+1)*row]
        for _ in range(scale): scaled.extend(source)
    output.parent.mkdir(parents=True,exist_ok=True)
    write_png(output,size*scale,size*scale,scaled)

def build_ios_app_icon(output,seed):
    build_app_icon(output,seed)

def resize_nearest(pixels,width,height,size):
    return [pixels[(y*height//size)*width+(x*width//size)]
            for y in range(size) for x in range(size)]

def build_desktop_app_icons(output,seed):
    """Build Linux PNG, Windows ICO, and macOS ICNS from one icon image."""
    output.mkdir(parents=True,exist_ok=True)
    source=output/"minecraftc.png"
    build_app_icon(source,seed)
    width,height,pixels=read_generated_png(source)

    icon256=png_bytes(256,256,resize_nearest(pixels,width,height,256))
    ico_header=struct.pack("<HHH",0,1,1)
    ico_entry=struct.pack("<BBBBHHII",0,0,0,0,1,32,len(icon256),22)
    (output/"minecraftc.ico").write_bytes(ico_header+ico_entry+icon256)

    icns_chunks=[]
    for kind,size in ((b"ic08",256),(b"ic09",512),(b"ic10",1024)):
        data=png_bytes(size,size,resize_nearest(pixels,width,height,size))
        icns_chunks.append(kind+struct.pack(">I",len(data)+8)+data)
    body=b"".join(icns_chunks)
    (output/"minecraftc.icns").write_bytes(b"icns"+struct.pack(">I",len(body)+8)+body)

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
    parser.add_argument("--build-items-atlas",action="store_true")
    parser.add_argument("--build-entity-atlas",action="store_true")
    parser.add_argument("--build-entity-skins",action="store_true")
    parser.add_argument("--build-ios-icon",action="store_true")
    parser.add_argument("--build-android-icon",action="store_true")
    parser.add_argument("--build-desktop-icons",action="store_true")
    parser.add_argument("--ios-icon-output",type=Path,default=Path(
        "ios/Assets.xcassets/AppIcon.appiconset/AppIcon.png"))
    parser.add_argument("--android-icon-output",type=Path,default=Path(
        "android/app/src/main/res/drawable-nodpi/app_icon.png"))
    parser.add_argument("--item-definitions",type=Path,default=Path("assets/textures/definitions/item_icons.json"))
    parser.add_argument("--block-definitions",type=Path,default=Path("assets/textures/definitions/blocks.json"))
    parser.add_argument("--item-overrides",type=Path,default=Path("assets/textures/source/items"))
    parser.add_argument("--legacy-items",type=Path,default=Path("assets/textures/legacy/items"))
    return parser.parse_args(argv)

def main(argv=None):
    args=parse_args(argv)
    if not (args.generate or args.validate or args.build_atlas or args.build_items_atlas or args.build_entity_atlas or args.build_entity_skins or args.build_ios_icon or args.build_android_icon or args.build_desktop_icons or args.contact_sheet): raise SystemExit("select a generation, validation, or atlas operation")
    try:
        if args.candidate_count<1: raise ValueError("--candidate-count must be at least 1")
        local_seeds=parse_local_seeds(args.local_seed)
        if args.generate: generate(args.output,args.seed,local_seeds)
        if args.validate: validate(args.output)
        if args.build_atlas: build_atlas(args.output,args.seed,local_seeds)
        if args.build_items_atlas: build_items_atlas(args.output,args.seed,args.item_definitions,args.block_definitions,args.item_overrides,args.legacy_items)
        if args.build_entity_atlas: build_entity_atlas(args.output,args.seed)
        if args.build_entity_skins: build_entity_skins(args.output,args.seed)
        if args.build_ios_icon: build_app_icon(args.ios_icon_output,args.seed)
        if args.build_android_icon: build_app_icon(args.android_icon_output,args.seed)
        if args.build_desktop_icons: build_desktop_app_icons(Path("packaging/icons"),args.seed)
        if args.contact_sheet: build_contact_sheet(args.output,args.seed,args.candidate_count,local_seeds)
    except (OSError,ValueError) as error: print(error,file=sys.stderr); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
