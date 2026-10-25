#!/usr/bin/env python3
"""Reproduce the original Natural Pixels visual package (Python standard library).

Shared semantic drawing/PNG/map helpers are used without writing default resources.
All palettes, sequences and pattern choices belong to this package's authoring.json.
"""
import argparse
import copy
import hashlib
import json
import math
import shutil
from functools import lru_cache
from pathlib import Path

import texture_generator as tg
import texture_recipes as recipes

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'assets/plugins/official_natural_textures'
DRAW_BASE = tg.generate_texture
SPRITE_BASE = tg.generate_item_sprite


def save_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def write_tile(path,pixels):
    # Stored DEFLATE keeps byte inventories independent of zlib vs zlib-ng's
    # compressor choices. Tiny 16x16 resources still total only a few MiB.
    tg.write_png(path,16,16,pixels,compression_level=0)


def noise(seed, name, x, y):
    return tg.sample(seed, 'natural/' + name, x % 16, y % 16)


@lru_cache(maxsize=4096)
def facets(seed, name):
    # Irregular toroidal kernels avoid a visible sine-wave lattice on large walls.
    broad=tg.macro_field(seed,'natural/'+name,17)
    fine=tg.macro_field(seed^0x361f,'natural/detail/'+name,29)
    return tuple(3.4*a+1.2*b for a,b in zip(broad,fine))


def field(seed, name, x, y):
    return facets(seed,name)[(y%16)*16+x%16]


def natural_tile(name, seed, local_seeds=None):
    local = tg.resolve_seed(seed, name, local_seeds)
    palette = tg.PALETTES[name]
    base = DRAW_BASE(name, seed, local_seeds)
    roles = [p.role for p in base]
    pattern = AUTHORING['patterns'][name]
    maximum = len(palette) - 1
    if pattern == 'ore':
        # Three separated mineral pockets with stepped highlights and inset shadows.
        roles = [max(0, min(2, round(1 + field(local, name, x, y) * .55)))
                 for y in range(16) for x in range(16)]
        for k, (cx, cy) in enumerate(((3, 3), (11, 7), (5, 12))):
            offset = noise(local, name, k, 2) % 2
            for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1), (2, 1), (1, 2)):
                x, y = (cx + dx + offset) % 16, (cy + dy) % 16
                roles[y * 16 + x] = min(maximum, 5 if dx + dy < 2 else 4 if dx else 3)
    elif pattern == 'soil':
        roles=[3]*256
        for k in range(22):
            h=noise(local,name,k,7);blob=tg.grow_blob(local,name,(h%16,(h>>8)%16),4+(h>>16)%9,k)
            for x,y in blob:
                roles[y*16+x]=4 if (x,(y-1)%16) not in blob else 1 if (x,(y+1)%16) not in blob else 2 if k%3==0 else 3
    elif pattern == 'turf':
        roles=tg.generate_grass(local)
        for k in range(26):
            h=noise(local,name,k,9);x=h%16;y=(h>>8)%16
            roles[y*16+x]=4;roles[((y+1)%16)*16+(x+1)%16]=2
            if h%3==0:roles[((y+2)%16)*16+(x+1)%16]=1
    elif pattern in ('stone', 'grain', 'weave'):
        for y in range(16):
            for x in range(16):
                i = y * 16 + x
                f = field(local, name, x, y)
                role = 3 + (1 if f > .55 else -1 if f < -.65 else 0)
                if pattern == 'stone':
                    if any(s in name for s in ('deepslate', 'basalt', 'blackstone')):
                        role = 2 if y % 4 == 0 else 4 if y % 4 == 1 and f > -.3 else role
                    if f < -1.1 and noise(local, name, x // 2, y) % 3 == 0: role = 1
                    if f > .7 and field(local, name, x, (y - 1) % 16) < .7: role = 5
                elif pattern == 'grain':
                    role = 3 + (1 if f > .9 else -1 if f < -1 else 0)
                    if noise(local, name, x, y) % 17 == 0: role = 4 if (x+y) % 2 else 2
                else:
                    role = 2 if (x + y) % 4 == 0 else 4 if (x + y) % 4 == 1 else 3
                roles[i] = max(0, min(maximum, role))
    elif pattern == 'cobble':
        centers = ((2, 2), (9, 1), (14, 5), (5, 7), (10, 11), (1, 13))
        for y in range(16):
            for x in range(16):
                distances = sorted((tg.torus_delta(x, cx)**2 + tg.torus_delta(y, cy)**2, k)
                                   for k, (cx, cy) in enumerate(centers))
                gap = distances[1][0] - distances[0][0]
                roles[y*16+x] = 1 if gap < 5 else 4 if gap < 12 else 2 if y % 5 == 4 else 3
    elif pattern == 'planks':
        for y in range(16):
            for x in range(16):
                row = (y + 2) // 4; joint = (x + 3 + (row % 2) * 4) % 8
                role = 1 if (y+2) % 4 == 3 or joint == 0 else 4 if (y+2) % 4 == 0 else 3
                if role == 3 and noise(local, name, x // 3, y) % 4 == 0: role = 2
                if role == 3 and noise(local, name, x // 4, y) % 7 == 1: role = 4
                roles[y*16+x] = role
    elif pattern == 'bark':
        roles=tg.generate_wood_side(name,local)
        for y in range(16):
            for x in range(16):
                i=y*16+x
                if 'birch' not in name and field(local,name,x,y)<-.5:roles[i]=max(0,roles[i]-1)
        for k in range(12 if 'birch' not in name else 6):
            h=noise(local,name,k,11);x=h%16;y=(h>>8)%16
            for step in range(2+(h>>16)%4):
                xx=(x+step//3)%16;yy=(y+step)%16
                roles[yy*16+xx]=1 if 'birch' not in name else 0
                if 'birch' not in name:roles[yy*16+(xx+1)%16]=4
    elif pattern == 'rings':
        cx=7+(noise(local,name,0,3)%3)*.4;cy=7+(noise(local,name,1,3)%3)*.4
        for y in range(16):
            for x in range(16):
                radius=math.hypot(x-cx,(y-cy)*1.1)+.3*field(local,name,x,y)
                roles[y*16+x] = 1 if x in (0,15) or y in (0,15) else 2 if radius%2.8<.8 else 4 if radius%2.8<1.5 else 3
    elif pattern == 'functional':
        root=name
        for suffix in ('_top','_side','_bottom'):
            if root.endswith(suffix):root=root[:-len(suffix)];break
        for y in range(16):
            for x in range(16):
                rim=x in (0,15) or y in (0,15)
                r=1 if rim else 3+(noise(local,name,x//3,y)%9==0)-(noise(local,name,x//4,y)%7==0)
                if root in ('furnace','blast_furnace','smoker'):
                    r=2 if rim else 3
                    if y in (2,13) and x%5<3:r=4
                    if not name.endswith(('_top','_bottom','_side')):
                        if 3<=x<=12 and (4<=y<=6 or 9<=y<=12):r=6
                        elif y in (3,8) and 3<=x<=12:r=1
                        elif y in (7,13) and 3<=x<=12:r=4
                elif root=='crafting_table':
                    if name.endswith('_top'):
                        if 3<=x<=12 and 3<=y<=12:r=1 if x in (3,6,9,12) or y in (3,6,9,12) else 4
                    elif not name.endswith('_bottom'):
                        if x in (1,2,13,14) or y in (2,12):r=1
                        if 4<=x<=11 and 4<=y<=10:
                            if x in (5,10):r=2
                            if (y==5 and 4<=x<=7) or (x==10 and y in (4,5,6)):r=6
                elif root=='chest':
                    if y in (4,5):r=0
                    if x in (1,14):r=1
                    if not name.endswith(('_top','_bottom','_side')) and 7<=x<=8 and 4<=y<=8:r=6 if y<8 else 1
                elif root=='barrel':
                    if y in (3,4,11,12):r=1
                    elif x%4==0:r=2
                else:
                    if 3<=x<=12 and 3<=y<=12:r=4 if x%2 else 1
                roles[y*16+x]=min(maximum,int(r))
    elif pattern == 'leaves':
        # Preserve clustered alpha holes, draw dense overlapping 3px leaves.
        for y in range(16):
            for x in range(16):
                i = y*16+x
                if base[i][3]:
                    tip = (x + 2 * (y // 3)) % 4
                    roles[i] = min(maximum, 5 if y % 3 == 0 and tip < 2 else 2 if tip == 3 else 4 if tip == 0 else 3)
    elif pattern == 'fluid':
        for y in range(16):
            for x in range(16):
                wave = math.sin(y*math.tau/8 + .8*math.sin(x*math.tau/16))
                roles[y*16+x] = 4 if wave > .65 else 1 if wave < -.75 else 2 if wave < -.2 else 3
    else:
        # Semantic authored faces/plants retain their recognizable construction.
        # Add edge-following bevels and coherent interior facets, not uniform noise.
        source = roles[:]
        for y in range(16):
            for x in range(16):
                i=y*16+x; r=source[i]
                if not base[i][3] or r == 0: continue
                up=source[((y-1)%16)*16+x]; left=source[y*16+(x-1)%16]
                if r > up and r > left and r < maximum: roles[i] = r+1
                elif r == 3 and field(local, name, x, y) < -.8: roles[i] = 2
                elif r == 3 and y % 4 == 1 and noise(local,name,x//3,y)%3 == 0: roles[i] = 4
    pixels=[palette[max(0, min(maximum, r))] if base[i][3] else palette[0]
            for i, r in enumerate(roles)]
    if name.startswith('polished_') or name=='smooth_sandstone':
        pixels=tg.center_periodic_tile(pixels)
    return pixels


def natural_sprite(template, material, definitions):
    shades = [tuple(c) + (255,) for c in definitions['materials'][material]]
    handle = [tuple(c) + (255,) for c in definitions['handle_palette']]
    image = SPRITE_BASE(template, material, definitions)
    if template in ('potion','splash_potion','empty_bottle'):
        image=[(0,0,0,0)]*256
        glass=((69,101,110,255),(121,163,168,255),(187,216,211,255),(226,238,225,255))
        neck=8 if template!='splash_potion' else 10
        for y in range(2,6):
            for x in range(neck-1,neck+2):tg._put(image,x,y,(155,113,62,255) if y==2 else glass[2 if x<neck else 0])
        for y,left,right in ((6,6,10),(7,5,11),(8,4,12),(9,4,12),(10,4,12),(11,4,12),(12,5,11),(13,6,10)):
            for x in range(left,right+1):
                color=glass[0 if x==right else 2 if x==left else 1]
                if template!='empty_bottle' and y>=9 and left<x<right:color=shades[2 if x<9 else 1]
                if x==left+1 and y in (8,9,10):color=glass[3]
                tg._put(image,x,y,color)
    elif template in tg.TOOL_TEMPLATES:
        image = [(0,0,0,0)] * 256
        # Long diagonal grip, separate forged head and collar; no black outline.
        tg._line(image, 3, 14, 10, 7, handle[0], 3)
        tg._line(image, 3, 13, 9, 7, handle[2], 1)
        if template == 'sword':
            tg._line(image, 7, 9, 13, 3, shades[0], 4)
            tg._line(image, 7, 8, 13, 2, shades[2], 2)
            tg._line(image, 9, 6, 13, 2, shades[3], 1)
            tg._line(image, 5, 7, 9, 11, handle[1], 2)
        elif template == 'pickaxe':
            for x,y in ((5,3),(6,3),(7,3),(8,3),(9,4),(10,4),(11,5),(12,6),(12,7),(13,8)):
                tg._put(image,x,y,shades[0]);tg._put(image,x,y-1,shades[2])
            tg._line(image,6,2,9,3,shades[3],1)
        elif template == 'axe':
            for y,left,right in ((2,8,11),(3,7,12),(4,6,12),(5,6,11),(6,7,10),(7,9,10)):
                for x in range(left,right+1):tg._put(image,x,y,shades[0 if x==right else 3 if x==left else 2])
        elif template == 'shovel':
            for y,left,right in ((2,11,12),(3,10,13),(4,9,13),(5,9,12),(6,10,11)):
                for x in range(left,right+1):tg._put(image,x,y,shades[3 if x==left else 0 if x==right else 2])
        else:
            tg._line(image,6,3,11,5,shades[2],2);tg._line(image,11,5,12,8,shades[0],2)
            tg._line(image,6,2,10,3,shades[3],1)
    elif template in ('ingot','gem','coal'):
        image=[(0,0,0,0)]*256
        rows = {'ingot':((5,6,11),(6,4,12),(7,3,12),(8,3,11),(9,4,10),(10,6,9)),
                'gem':((2,7,9),(3,5,11),(4,4,12),(5,3,12),(6,3,12),(7,4,11),(8,5,10),(9,6,9),(10,7,8)),
                'coal':((3,8,10),(4,6,11),(5,5,12),(6,4,12),(7,3,11),(8,3,11),(9,4,10),(10,5,9),(11,7,8))}[template]
        for y,left,right in rows:
            for x in range(left,right+1):
                tg._put(image,x,y,shades[0 if x==right or y==rows[-1][0] else 3 if y==rows[1][0] or x==left+1 else 2 if x+y<16 else 1])
    else:
        # Sculpt the existing semantic masks: recessed lower edge and smaller
        # top-left facets follow each object's shape and material parts.
        source=image[:]
        for y in range(1,15):
            for x in range(1,15):
                i=y*16+x
                if not source[i][3]:continue
                if source[i] in shades:
                    if not source[(y+1)*16+x][3]:image[i]=shades[0]
                    elif not source[(y-1)*16+x][3]:image[i]=shades[3 if x<9 else 2]
                    elif source[i]==shades[2] and (x+2*y)%7==0:image[i]=shades[1]
    return image


def configure():
    global AUTHORING
    AUTHORING=json.loads((PACKAGE/'authoring.json').read_text(encoding='utf-8'))
    tg.STYLE_ID='natural-pixels'
    tg.STYLE_DEFINITION=AUTHORING['style']
    tg.RECIPE_DOCUMENT=AUTHORING['recipes']
    for name in tg.NAMES:
        colors=AUTHORING['palettes'][name]
        tg.PALETTES[name]=[tg.SemanticColor(c,i) for i,c in enumerate(colors)]
    tg.STRUCTURE_PALETTES=dict(tg.PALETTES)
    tg.generate_texture=natural_tile
    tg.generate_item_sprite=natural_sprite


def generate(package, review):
    configure();seed=AUTHORING['seed']
    package.mkdir(parents=True,exist_ok=True)
    if package.resolve()!=PACKAGE.resolve():
        for name in ('mod.json','authoring.json','LICENSE.md'):
            shutil.copyfile(PACKAGE/name,package/name)
    tiles={};manifest={}
    for name in tg.NAMES:
        spec=AUTHORING['recipes']['materials'][name];variants=spec.get('variants',1)
        anim=spec.get('animation',{});frames=anim.get('frames',1)
        paths={kind:[] for kind in ('images','normals','properties')}
        for variant in range(variants):
            for frame in range(frames):
                result=recipes.generate_material(tg,name,seed,variant=variant,frame=frame)
                if variant==0 and frame==0:tiles[name]=result['pixels']
                profile=AUTHORING['profiles'][name]
                normal,properties,_=recipes.material_maps(tg,name,result,profile)
                errors=tg.validate_texture(Path(name+'.png'),result['pixels'])
                if errors:raise ValueError('; '.join(errors))
                for kind,pixels in zip(paths,(result['pixels'],normal,properties)):
                    path=f'textures/materials/{name}-{variant*frames+frame}-{kind}.png'
                    dest=package/path;dest.parent.mkdir(parents=True,exist_ok=True)
                    write_tile(dest,pixels);paths[kind].append(path)
        sequence=dict(variants=variants,frames=frames,fps_milli=round(anim.get('fps',0)*1000),
                      seed=tg.name_hash(name)&0xffffffff,**paths)
        if name not in AUTHORING['non_runtime_materials']:
            save_json(package/f'materials/{name}.json',dict(key=name,replace=True,sequence=sequence))
    definitions=copy.deepcopy(AUTHORING['items'])
    blocks=tg._read_definition(tg.DEFINITION_ROOT/'blocks.json')['blocks']
    for name,spec in definitions['items'].items():
        if spec['generator']=='block_item_icon':
            b=blocks[spec['block']];top=b.get('top',b.get('all'));side=b.get('side',b.get('all',top))
            pixels=tiles[top] if top in tg.PLANTS else tg.generate_block_item_icon(tiles[top],tiles[side],tiles[b.get('front',side)])
        else:pixels=natural_sprite(spec['template'],spec['material'],definitions)
        errors=tg.validate_item_sprite(pixels,name)
        if errors:raise ValueError('; '.join(errors))
        path=f'textures/items/{name}.png';(package/path).parent.mkdir(parents=True,exist_ok=True)
        write_tile(package/path,pixels)
        # Logical command aliases are stable across appends to the serialized enum.
        key=AUTHORING['item_keys'][name]
        save_json(package/f'item_icons/{name}.json',dict(item=key,image=path))
    # The same byte inventory is used by packaged Title Storage on Android/iOS.
    from check_official_package import refresh
    refresh(package)
    for path in sorted(package.rglob('*')):
        if path.is_file():manifest[path.relative_to(package).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
    if review:
        review.mkdir(parents=True,exist_ok=True)
        # Review-only flat tiles feed existing comparison and mip tools.
        for name,pixels in tiles.items():tg.write_png(review/f'{name}.png',16,16,pixels)
        import texture_review
        texture_review.compare(ROOT/'assets/textures/generated',review,review/'comparison',True)
        tg.build_mip_preview(review)
        contact(package,review)
        save_json(review/'resource_hashes.json',manifest)
    return manifest


def contact(package,review):
    for group,names in (('blocks',tg.NAMES),('items',list(AUTHORING['items']['items']))):
        for page,start in enumerate(range(0,len(names),64)):
            subset=names[start:start+64];w=1024;h=math.ceil(len(subset)/8)*108
            canvas=[(30,33,36,255)]*(w*h)
            for i,name in enumerate(subset):
                path=package/(f'textures/items/{name}.png' if group=='items' else f'textures/materials/{name}-0-images.png')
                pixels=tg.read_generated_png(path)[2];x=i%8*128;y=i//8*108
                tg.draw_text(canvas,w,x+3,y+3,name[:29]);tg.blit(canvas,w,x+4,y+18,pixels,16,16,4)
                for yy in range(3):
                    for xx in range(3):tg.blit(canvas,w,x+74+xx*16,y+20+yy*16,pixels,16,16)
            tg.write_png(review/f'{group}-{page}.png',w,h,canvas)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=PACKAGE)
    parser.add_argument('--review',type=Path)
    args=parser.parse_args()
    generate(args.output,args.review)


if __name__=='__main__':main()
