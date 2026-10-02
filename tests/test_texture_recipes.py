import copy
import json
import math
import struct
import subprocess
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import texture_generator as tg
import texture_recipes as tr
from texture_workbench import Workbench


class MaterialRecipeTests(unittest.TestCase):
    def test_every_drawing_carries_structure_and_color_changes_cannot_move_it(self):
        for name in tg.NAMES:
            original=tr.generate_material(tg,name,213785369)
            recolored=tr.generate_material(tg,name,213785369,palette=[(77,77,77,p[3]) for p in tg.PALETTES[name]])
            self.assertEqual(original['pixels'],tg.generate_texture(name,213785369),name)
            self.assertEqual(original['roles'],recolored['roles'],name)
            self.assertEqual(original['masks'],recolored['masks'],name)
            profile=tg.load_material_profiles()[name]
            self.assertEqual(tr.material_maps(tg,name,original,profile),tr.material_maps(tg,name,recolored,profile),name)

    def test_inheritance_layers_properties_and_invalid_documents(self):
        d,_=tr.load_recipes(tg)
        d=copy.deepcopy(d);d['materials']['stone']['recipe']='glowing_crystal'
        r=tr.generate_material(tg,'stone',11,document=d)
        mask=r['masks']['crystal'];self.assertTrue(any(mask));self.assertTrue(any(not x for x in mask))
        props=tr.material_maps(tg,'stone',r,tg.load_material_profiles()['stone'])[1]
        for i,p in enumerate(props):self.assertEqual(p[2],255 if mask[i] else 0)
        inherited=copy.deepcopy(d);inherited['recipes']['inherited']={'parent':'glowing_crystal','overrides':{'crystal':{'density':.5}}}
        _,resolved=tr.load_recipes(tg,document=inherited)
        self.assertEqual(resolved['inherited'][1]['density'],.5)
        self.assertEqual(resolved['glowing_crystal'][1]['density'],.20)
        self.assertEqual(inherited['examples']['glowing_crystal']['layers'][0]['density'],.20)
        changed=copy.deepcopy(d);changed['examples']['glowing_crystal']['layers'][0]['properties']['height']=.9
        r2=tr.generate_material(tg,'stone',11,document=changed)
        self.assertNotEqual(tr.material_maps(tg,'stone',r,tg.load_material_profiles()['stone']),tr.material_maps(tg,'stone',r2,tg.load_material_profiles()['stone']))
        for mutation in ('cycle','operation','density','budget','frames','fps','palette','layers','animation'):
            bad=copy.deepcopy(d)
            if mutation=='cycle':bad['recipes']['base']['parent']='glowing_crystal'
            if mutation=='operation':bad['examples']['glowing_crystal']['layers'][0]['op']='unknown'
            if mutation=='density':bad['examples']['glowing_crystal']['layers'][0]['density']=float('nan')
            if mutation=='budget':bad['materials']['stone']['variants']=5
            if mutation=='frames':bad['materials']['stone']['animation']={'frames':17,'fps':6}
            if mutation=='fps':bad['materials']['stone']['animation']={'frames':16,'fps':0}
            if mutation=='layers':bad['recipes']['base']['layers']=[None]
            if mutation=='animation':bad['materials']['stone']['animation']=[]
            if mutation=='palette':bad['materials']['stone']['palette']=[[1,2,3,255]]
            with self.assertRaises(ValueError,msg=mutation):tr.load_recipes(tg,document=bad)

    def test_variants_frames_reproducible_distinct_and_keep_all_tile_rules(self):
        d,_=tr.load_recipes(tg)
        with tempfile.TemporaryDirectory() as directory:
            for seed in (tg.DEFAULT_SEED,7,-1):
                for name,spec in d['materials'].items():
                    fingerprints=set();frames=spec.get('animation',{}).get('frames',1)
                    base=tr.generate_material(tg,name,seed)
                    for variant in range(spec['variants']):
                        for frame in range(frames):
                            r=tr.generate_material(tg,name,seed,variant=variant,frame=frame)
                            self.assertEqual(r,tr.generate_material(tg,name,seed,variant=variant,frame=frame))
                            path=Path(directory)/(name+'.png');tg.write_png(path,16,16,r['pixels'])
                            self.assertFalse(tg.validate_texture(path),(seed,name,variant,frame))
                            fingerprints.add(tuple(r['pixels']))
                            if name in {'star_crystal','resonant_crystal'}:
                                self.assertEqual(sorted(r['pixels']),sorted(base['pixels']))
                                self.assertEqual(r['masks']['coverage'],base['masks']['coverage'])
                    self.assertGreaterEqual(len(fingerprints),spec['variants'],name)
                    if frames>1:self.assertGreater(len(fingerprints),1,name)
                    self.assertEqual(base,tr.generate_material(tg,name,seed,frame=frames))

    def test_separate_processes_and_available_python_versions_choose_same_origins(self):
        script="import sys,json;sys.path.insert(0,'tools');import texture_generator as t;print(json.dumps({n:[list(p) for p in t.generate_texture(n,t.DEFAULT_SEED)] for n in t.NAMES},sort_keys=True))"
        root=Path(__file__).resolve().parents[1]
        baseline=subprocess.check_output([sys.executable,'-c',script],cwd=root)
        interpreters={sys.executable}
        for name in ('python3','python3.11','python3.12','python3.14'):
            found=shutil.which(name)
            if found:interpreters.add(found)
        for executable in sorted(interpreters):
            self.assertEqual(subprocess.check_output([executable,'-c',script],cwd=root),baseline,executable)

    def test_custom_leaf_variants_keep_silhouette_and_visible_palette_roles(self):
        document,_=tr.load_recipes(tg);document=copy.deepcopy(document)
        document['materials']['leaves']['variants']=4
        base=tr.generate_material(tg,'leaves',7,document=document)
        for variant in range(4):
            result=tr.generate_material(tg,'leaves',7,variant=variant,document=document)
            self.assertEqual(result['masks']['coverage'],base['masks']['coverage'])
            self.assertFalse(tg.validate_texture(Path('leaves.png'),result['pixels']))

    def test_preferred_candidate_becomes_export_foundation_without_changing_art(self):
        document,_=tr.load_recipes(tg);document=copy.deepcopy(document)
        for name in ('stone','cobblestone'):
            candidate=tr.generate_material(tg,name,7,variant=2)
            document['materials'][name]['preferred_variant']=2
            foundation=tr.generate_material(tg,name,7,document=document)
            self.assertEqual(candidate['pixels'],foundation['pixels'])
            self.assertEqual(candidate['roles'],foundation['roles'])

    def test_cross_variant_boundaries_keep_original_perceptual_seam_gate(self):
        document,_=tr.load_recipes(tg)
        for seed in (tg.DEFAULT_SEED,7,-1):
            for name,spec in document['materials'].items():
                if spec['variants']==1:continue
                tiles=[tr.generate_material(tg,name,seed,variant=v)['pixels'] for v in range(4)]
                metrics=[tg.seam_metrics(tile) for tile in tiles]
                for ia,a in enumerate(tiles):
                    for ib,b in enumerate(tiles):
                        for axis in ('x','y'):
                            pairs=[(a[y*16+15],b[y*16]) for y in range(16)] if axis=='x' else [(a[240+x],b[x]) for x in range(16)]
                            inside=(metrics[ia]['perceptual_'+axis]['interior']+metrics[ib]['perceptual_'+axis]['interior'])/2
                            ratio=sum(tg.perceptual_distance(*pair) for pair in pairs)/16/max(.005,inside)
                            self.assertLessEqual(ratio,2.60,(seed,name,ia,ib,axis))

    def test_locked_seed_isolated_and_wire_slots_budget_and_mips(self):
        self.assertEqual(tr.generate_material(tg,'sand',1,{'stone':123}),tr.generate_material(tg,'sand',1))
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);tg.generate(out,1,{'stone':123});metadata=tg.build_atlas(out,1,{'stone':123})
            data=(out/'atlas_sequences.bin').read_bytes();self.assertEqual(data[:8],b'MCTSEQ5\0')
            self.assertEqual(len(data),12+len(tg.NAMES)*272);self.assertEqual(struct.unpack_from('<I',data,8)[0],len(tg.NAMES))
            count=len(metadata['physical_tiles']);self.assertEqual(metadata['grid_size'],math.ceil(math.sqrt(count)))
            self.assertLessEqual(metadata['grid_size']*16,1024)
            width,_,atlas=tg.read_generated_png(out/'atlas.png')
            for index,name in enumerate(tg.NAMES):
                e=metadata['textures'][name];params=struct.unpack_from('<68I',data,12+index*272)
                self.assertEqual(params[:4],(e['variants'],e['frames'],round(e['fps']*1000),e['selection_seed']))
                self.assertEqual(params[4:4+len(e['slots'])],tuple(e['slots']))
                self.assertEqual(e['slots'][0],index)
                for slot in e['slots']:
                    pixels=[atlas[(slot//metadata['grid_size']*16+y)*width+slot%metadata['grid_size']*16+x] for y in range(16) for x in range(16)]
                    self.assertEqual([size for size,_ in tg.tile_mip_chain(pixels)],[16,8,4,2,1])

    def test_workbench_save_restore_lock_preview_export_share_generator(self):
        with tempfile.TemporaryDirectory() as directory:
            work=Workbench(directory);state=work.state();state['locked']={'stone':'77'};state['seed']='99'
            work.save(state);restored=Workbench(directory)
            self.assertEqual(restored.seed,99);self.assertEqual(restored.locked,{'stone':77})
            preview=restored.preview({'material':'stone','variant':2})
            self.assertEqual(len(preview['variants']),4)
            self.assertEqual(preview['frames'][0]['pixels'],tr.generate_material(tg,'stone',99,{'stone':77},variant=2)['pixels'])
            exported=restored.export(restored.state());out=Path(exported['exported'])
            self.assertFalse(exported['applied']);self.assertTrue((out/'atlas_sequences.bin').exists())
            self.assertEqual(tg.read_generated_png(out/'stone.png')[2],tr.generate_material(tg,'stone',99,{'stone':77})['pixels'])
            self.assertEqual(json.loads((out/'recipes.json').read_text()),restored.document)
            self.assertTrue((out/'items_atlas.png').exists())
            with self.assertRaises(ValueError):restored.preview({'material':'../../invalid'})


if __name__=='__main__':unittest.main()
