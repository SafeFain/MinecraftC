"""Versioned, dependency-free structural material recipes and physical sequences."""
import hashlib
import copy
import json
import math
import struct
from pathlib import Path

MAX_VARIANTS = 4
MAX_FRAMES = 16
MAX_ATLAS_SIZE = 1024
OPS = {'base', 'texture', 'cracks', 'veins', 'cover'}


def load_recipes(tg, path=None, document=None):
    data = document if document is not None else getattr(tg,"RECIPE_DOCUMENT",None)
    data = data if data is not None else tg._read_definition(path or tg.DEFINITION_ROOT/'recipes.json')
    if not isinstance(data,dict):raise ValueError('recipe document must be an object')
    if not all(isinstance(data.get(key,{}),dict) for key in ('recipes','examples','materials')):
        raise ValueError('recipe collections must be objects')
    if data.get('version') != 1 or data.get('generator_version') != tg.GENERATOR_VERSION:
        raise ValueError('recipe version mismatch')
    recipes = dict(data.get('recipes', {})); recipes.update(data.get('examples', {}))
    resolved = {}
    def resolve(name, stack=()):
        if name in resolved: return resolved[name]
        if name in stack: raise ValueError('cyclic recipe inheritance: '+name)
        if name not in recipes or not isinstance(recipes[name], dict): raise ValueError('unknown recipe: '+name)
        spec = recipes[name]
        if not isinstance(spec.get('layers',[]),list) or not isinstance(spec.get('overrides',{}),dict):
            raise ValueError('recipe layers/overrides have invalid types')
        layers = copy.deepcopy(resolve(spec['parent'], stack+(name,))) if 'parent' in spec else []
        layers += copy.deepcopy(spec.get('layers', []))
        for target,patch in spec.get('overrides',{}).items():
            matches=[i for i,layer in enumerate(layers) if str(i)==target or layer.get('mask')==target]
            if len(matches)!=1 or not isinstance(patch,dict):raise ValueError('unknown/ambiguous recipe override: '+target)
            layers[matches[0]].update(patch)
        if not layers or not all(isinstance(layer,dict) for layer in layers) or layers[0].get('op') != 'base': raise ValueError('recipe must start with base')
        for index, layer in enumerate(layers):
            if not isinstance(layer, dict) or layer.get('op') not in OPS: raise ValueError('unknown recipe operation')
            if layer['op'] == 'base' and index: raise ValueError('base must be first')
            for key, default, low, high in (('density', .2, 0, 1), ('scale', 1, .25, 16)):
                value = layer.get(key, default)
                if isinstance(value, bool) or not isinstance(value, (int,float)) or not math.isfinite(value) or not low<=value<=high:
                    raise ValueError('invalid layer '+key)
            for key in ('seed','role'):
                value=layer.get(key,0 if key=='seed' else 2)
                if isinstance(value,bool) or not isinstance(value,int) or (key=='role' and not 0<=value<=15):
                    raise ValueError('invalid layer '+key)
            if not isinstance(layer.get('properties',{}),dict):raise ValueError('layer properties must be an object')
            if not isinstance(layer.get('mask','mask'),str):raise ValueError('layer mask must be a name')
            for key,value in layer.get('properties',{}).items():
                if key not in {'roughness','metallic','emission','height'} or isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or not 0<=value<=1:
                    raise ValueError('invalid layer property')
            if 'color' in layer and (len(layer['color'])!=4 or any(type(c)!=int or not 0<=c<=255 for c in layer['color']) or layer['color'][3] not in (0,255)):
                raise ValueError('invalid layer RGBA')
        resolved[name]=layers
        return layers
    for name in recipes: resolve(name)
    materials=data.get('materials',{})
    if set(materials)!=set(tg.NAMES): raise ValueError('recipes must cover every logical material')
    for name,spec in materials.items():
        if not isinstance(spec,dict) or not isinstance(spec.get('animation',{}),dict):
            raise ValueError('material/animation must be an object')
        resolve(spec.get('recipe',''))
        if 'palette' in spec and (len(spec['palette'])<len(tg.STRUCTURE_PALETTES[name]) or any(len(c)!=4 or any(type(v)!=int or not 0<=v<=255 for v in c) for c in spec['palette'])):
            raise ValueError('invalid recipe palette: '+name)
        count=spec.get('variants',1); animation=spec.get('animation',{})
        frames=animation.get('frames',1); fps=animation.get('fps',0)
        preferred=spec.get('preferred_variant',0)
        if type(preferred)!=int or type(count)!=int or not 0<=preferred<count:
            raise ValueError('invalid preferred variant: '+name)
        if type(count)!=int or not 1<=count<=MAX_VARIANTS or type(frames)!=int or not 1<=frames<=MAX_FRAMES or count*frames>64:
            raise ValueError('material sequence exceeds budget: '+name)
        if type(fps) not in (int,float) or not math.isfinite(fps) or not 0<=fps<=60 or (frames>1 and fps==0) or animation.get('loop','repeat')!='repeat':
            raise ValueError('invalid animation: '+name)
    return data,resolved


def generate_material(tg, name, seed, local_seeds=None, variant=0, frame=0,
                      document=None, recipe=None, palette=None):
    data,resolved=load_recipes(tg,document=document)
    spec=data['materials'][name]; frames=spec.get('animation',{}).get('frames',1)
    if not 0<=variant<spec.get('variants',1): raise ValueError('variant out of range')
    frame%=frames
    effective=tg.resolve_seed(seed,name,local_seeds)
    actual_variant=(variant+spec.get('preferred_variant',0))%spec.get('variants',1)
    candidate=effective if actual_variant==0 else tg.mix64(effective ^ tg.name_hash('variant/'+str(actual_variant)))
    # Drawing operates on canonical role-tagged colors. User color changes are
    # applied only after structural decisions, including periodic cut selection.
    original=tg.PALETTES[name]
    tg.PALETTES[name]=tg.STRUCTURE_PALETTES[name]
    try: structural=tg.generate_texture(name,seed,{name:effective})
    finally: tg.PALETTES[name]=original
    roles=[getattr(pixel,'role',0 if pixel[3]==0 else None) for pixel in structural]
    if any(role is None for role in roles): raise ValueError('drawing lost a semantic role: '+name)
    alpha=[p[3] for p in structural]
    if frame:
        phase=frame*math.tau/frames
        if name=='fire':
            source=roles[:];source_alpha=alpha[:]
            for y in range(16):
                shift=round(math.sin(phase+y*.4)*(15-y)/12)
                for x in range(16):
                    sx=x-shift;i=y*16+x
                    roles[i]=source[y*16+sx] if 0<=sx<16 else 0
                    alpha[i]=source_alpha[y*16+sx] if 0<=sx<16 else 0
        elif name=='lava':
            # A looping toroidal advection, all property maps follow the same field.
            sx=round(2*math.sin(phase));sy=round(2*(1-math.cos(phase)))
            roles=[roles[((y+sy)%16)*16+(x+sx)%16] for y in range(16) for x in range(16)]
        else:
            # Permute only the visible roles: silhouette and total radiance stay fixed.
            visible=[i for i,a in enumerate(alpha) if a and 1<i%16<14 and 1<i//16<14 and roles[i]>=(5 if name=='resonant_crystal' else 2)]
            values=[roles[i] for i in visible];offset=round(len(values)*frame/frames)
            for j,i in enumerate(visible): roles[i]=values[(j+offset)%len(values)]
    if actual_variant:
        # One shared macro structure is modulated by independently seeded
        # interior detail. The analytical envelope vanishes at the tile domain
        # boundary; no edge pixels are copied or painted after generation.
        field=tg.macro_field(candidate,name,6)
        for y in range(16):
            for x in range(16):
                i=y*16+x
                envelope=math.sin(math.pi*x/15)*math.sin(math.pi*y/15)
                change=field[i]*envelope*2.0
                delta=1 if change>.25 else -1 if change<-.25 else 0
                role=roles[i]
                low,high=(3,len(original)-1) if name.endswith('_ore') and role>=3 else (0,2) if name.endswith('_ore') else (1 if original[0][3]==0 else 0,len(original)-1)
                if alpha[i]:roles[i]=max(low,min(high,role+delta))
        # A sparse interior anchor keeps exceptionally quiet fields distinct.
        x=3+(actual_variant*3+effective%5)%10
        y=3+(actual_variant*5+effective%7)%10
        i=y*16+x;role=roles[i]
        low,high=(3,len(original)-1) if name.endswith('_ore') and role>=3 else (0,2) if name.endswith('_ore') else (1 if original[0][3]==0 else 0,len(original)-1)
        if alpha[i]:roles[i]=role+1 if role<high else max(low,role-1)
    colors=palette if palette is not None else spec.get("palette",original)
    if len(colors)<len(tg.STRUCTURE_PALETTES[name]) or any(len(c)!=4 or any(type(v)!=int or not 0<=v<=255 for v in c) for c in colors):
        raise ValueError('palette must cover structural roles with RGBA bytes')
    pixels=[tuple(colors[role][:3])+(a,) for role,a in zip(roles,alpha)]
    masks={'coverage':[int(a>0) for a in alpha], 'mineral':[int(r>=3 and a>0 and name.endswith('_ore')) for r,a in zip(roles,alpha)],
           'cavity':[int(r==0 and a>0) for r,a in zip(roles,alpha)]}
    for role in range(len(colors)):
        masks['role_'+str(role)]=[int(r==role and a>0) for r,a in zip(roles,alpha)]
    properties=[]
    for layer_index,layer in enumerate(resolved[recipe or spec['recipe']]):
        op=layer['op']
        if op=='base': continue
        layer_seed=tg.mix64(effective ^ tg.name_hash('layer/'+str(layer_index)) ^ layer.get('seed',0))
        density=layer.get('density',.2);scale=layer.get('scale',1); role=layer.get('role',2)
        if role>=len(colors): raise ValueError('layer role outside palette')
        mask=[]
        for i in range(256):
            x=i%16;y=i//16
            # Exact periodic domain at every scale; no non-periodic resampling.
            frequency=max(1,min(8,round(4/scale)))
            field=.5+.5*math.sin(math.tau*frequency*x/16+math.sin(math.tau*frequency*y/16)+layer_seed%31)
            grain=(tg.sample(layer_seed,'recipe',int(x/scale),int(y/scale))&65535)/65535
            if op=='cracks': selected=abs(math.sin(math.tau*frequency*(x+y)/16+layer_seed%11))<density
            elif op=='veins': selected=field>1-density
            else: selected=grain<density
            selected=bool(selected and alpha[i]);mask.append(int(selected))
            if selected: roles[i]=role;pixels[i]=tuple(layer.get('color',colors[role]))
        masks[layer.get('mask',op+'_'+str(layer_index))]=mask
        properties.append((mask,layer.get('properties',{})))
    masks['coverage']=[int(p[3]>0) for p in pixels]
    for role in range(len(colors)):
        masks['role_'+str(role)]=[int(r==role and p[3]>0) for r,p in zip(roles,pixels)]
    digest=hashlib.sha256(json.dumps(resolved[recipe or spec['recipe']],sort_keys=True).encode()).hexdigest()
    return {'pixels':pixels,'roles':roles,'masks':masks,'properties':properties,
            'recipe_sha256':digest,'effective_seed':candidate,'variant':variant,'frame':frame}


def material_maps(tg,name,result,profile):
    return tg.generate_material_maps(name,result['pixels'],profile,result['roles'],result)


def build_sequence_atlas(tg,output,seed,local_seeds=None):
    tg.validate(output)
    data,_=load_recipes(tg);manifest=tg._read_definition(output/'generation.json') if (output/'generation.json').exists() else {}
    if manifest and manifest.get('seed')!=seed: raise ValueError('atlas seed does not match generated tile provenance')
    profiles=tg.load_material_profiles();logical=len(tg.NAMES);physical=[];entries={}
    # Foundation slots retain all existing indices, appended frames are contiguous per sequence.
    bases=[]
    for name in tg.NAMES:
        pixels=tg.read_generated_png(output/(name+'.png'))[2]
        semantic=output/(name+'.semantic.json')
        if semantic.exists():
            result=json.loads(semantic.read_text());result['pixels']=pixels
            if result.get('pixel_sha256')!=hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest():
                raise ValueError('semantic sidecar does not match source pixels: '+name)
        else:
            result=generate_material(tg,name,seed,local_seeds)
            if result['pixels']!=pixels: raise ValueError('authored tile needs explicit semantic sidecar: '+name)
        bases.append(result)
        physical.append((name,result))
    for index,name in enumerate(tg.NAMES):
        spec=data['materials'][name];variants=spec.get('variants',1);anim=spec.get('animation',{})
        frames=anim.get('frames',1);slots=[index]
        for variant in range(variants):
            for frame in range(frames):
                if variant==0 and frame==0:continue
                result=generate_material(tg,name,seed,local_seeds,variant,frame)
                slots.append(len(physical));physical.append((name,result))
        entries[name]={'index':index,'x':0,'y':0,'source':name+'.png','family':tg.TEXTURE_FAMILIES[name],
            'sha256':hashlib.sha256(bytes(c for p in bases[index]['pixels'] for c in p)).hexdigest(),
            'generated':bases[index]['pixels']==generate_material(tg,name,seed,local_seeds)['pixels'],
            'effective_seed':bases[index]['effective_seed'],'recipe_sha256':bases[index]['recipe_sha256'],
            'variants':variants,'frames':frames,'fps':anim.get('fps',0),'loop':'repeat','slots':slots,
            'selection_seed':tg.name_hash(name)&0xffffffff,**profiles[name]}
        if local_seeds and name in local_seeds:entries[name]['local_seed']=local_seeds[name]
    grid=math.ceil(math.sqrt(len(physical)));width=grid*16
    if width>MAX_ATLAS_SIZE:raise ValueError('physical atlas exceeds 1024px budget')
    atlases=[[(0,0,0,0)]*(width*width),[(128,128,255,255)]*(width*width),[(255,0,0,128)]*(width*width),[(128,128,128,255)]*(width*width)]
    physical_meta=[]
    for slot,(name,result) in enumerate(physical):
        errors=tg.validate_texture(output/(name+'.png'),result['pixels'])
        if errors:raise ValueError('invalid physical tile '+str(slot)+': '+'; '.join(errors))
        maps=material_maps(tg,name,result,profiles[name])
        for atlas,tile in zip(atlases,(result['pixels'],)+maps):
            for y in range(16):
                start=(slot//grid*16+y)*width+slot%grid*16;atlas[start:start+16]=tile[y*16:y*16+16]
        physical_meta.append({'slot':slot,'material':name,'variant':result['variant'],'frame':result['frame']})
    for name,entry in entries.items():entry['x']=entry['index']%grid;entry['y']=entry['index']//grid
    for filename,pixels in zip(('atlas.png','atlas_normal.png','atlas_property.png','atlas_height.png'),atlases):tg.write_png(output/filename,width,width,pixels)
    # Portable little-endian wire representation; std430 entry = 4 uints + 64 uints.
    blob=bytearray(b'MCTSEQ5\0')+struct.pack('<I',logical)
    for name in tg.NAMES:
        e=entries[name];blob+=struct.pack('<68I',e['variants'],e['frames'],round(e['fps']*1000),e['selection_seed'],*(e['slots']+[e['index']]*(64-len(e['slots']))))
    (output/'atlas_sequences.bin').write_bytes(blob)
    metadata={'version':2,'generator_version':tg.GENERATOR_VERSION,'style':tg.STYLE_ID,'tile_size':16,'grid_size':grid,'filter':'nearest','seed':seed,
        'textures':entries,'physical_tiles':physical_meta,'sequence_blob':'atlas_sequences.bin',
        'material_maps':{'normal':'atlas_normal.png','property':'atlas_property.png','height':'atlas_height.png'},'property_channels':['roughness','metallic','emission','height']}
    (output/'atlas.json').write_text(json.dumps(metadata,indent=2,sort_keys=True)+'\n')
    return metadata


def build_sequence_preview(tg,output):
    metadata=json.loads((output/'atlas.json').read_text());grid=metadata['grid_size']
    width,_,pixels=tg.read_generated_png(output/'atlas.png')
    def tile(slot):
        return [pixels[(slot//grid*16+y)*width+slot%grid*16+x] for y in range(16) for x in range(16)]
    names=[n for n in tg.NAMES if metadata['textures'][n]['frames']>1]
    canvas=[(25,36,39,255)]*(640*len(names)*76)
    for row,name in enumerate(names):
        entry=metadata['textures'][name];tg.draw_text(canvas,640,4,row*76+4,name)
        for frame in range(entry['frames']):
            tg.blit(canvas,640,frame*40+4,row*76+20,tile(entry['slots'][frame]),16,16,2)
    tg.write_png(output/'animation_preview.png',640,len(names)*76,canvas)
    names=[n for n in tg.NAMES if metadata['textures'][n]['variants']>1]
    for page,start in enumerate(range(0,len(names),12)):
        chosen=names[start:start+12];canvas=[(25,36,39,255)]*(640*len(chosen)*96)
        for row,name in enumerate(chosen):
            e=metadata['textures'][name];tg.draw_text(canvas,640,4,row*96+4,name)
            for variant in range(e['variants']):tg.blit(canvas,640,variant*70+4,row*96+24,tile(e['slots'][variant*e['frames']]),16,16,4)
            for y in range(4):
                for x in range(8):
                    h=e['selection_seed']
                    for coordinate in (x,0,y,0):h=((h^coordinate)*16777619)&0xffffffff
                    h^=h>>16;h=(h*2246822519)&0xffffffff;h^=h>>13
                    tg.blit(canvas,640,350+x*16,row*96+24+y*16,tile(e['slots'][(h%e['variants'])*e['frames']]),16,16,1)
        tg.write_png(output/f'variant_preview_{page}.png',640,len(chosen)*96,canvas)
