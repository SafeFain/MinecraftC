"""Complete pack coverage, byte reproducibility and portable content publication."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import texture_generator as tg
import content_package
from check_official_package import inventory

PACK=ROOT/'assets/plugins/official_natural_textures'


class NaturalPixelsTests(unittest.TestCase):
    def test_full_coverage_and_sequences(self):
        author=json.loads((PACK/'authoring.json').read_text(encoding='utf-8'))
        self.assertEqual(set(tg.NAMES),set(author['palettes']))
        runtime=set(tg.NAMES)-set(author['non_runtime_materials'])
        self.assertEqual(runtime,{p.stem for p in (PACK/'materials').glob('*.json')})
        names=tg.load_item_icon_definitions(tg.DEFINITION_ROOT/'item_icons.json')['items']
        self.assertEqual(set(names),{p.stem for p in (PACK/'item_icons').glob('*.json')})
        changed=0;default_recipes=tg._read_definition(tg.DEFINITION_ROOT/'recipes.json')
        for name in tg.NAMES:
            if name not in runtime:continue
            definition=json.loads((PACK/f'materials/{name}.json').read_text())
            self.assertEqual(definition['key'],name);self.assertTrue(definition['replace'])
            sequence=definition['sequence'];base=default_recipes['materials'][name]
            self.assertEqual(sequence['variants'],base.get('variants',1))
            self.assertEqual(sequence['frames'],base.get('animation',{}).get('frames',1))
            count=sequence['variants']*sequence['frames']
            for kind in ('images','normals','properties'):
                self.assertEqual(len(sequence[kind]),count)
                for path in sequence[kind]:
                    self.assertNotIn('..',Path(path).parts)
                    w,h,pixels=tg.read_generated_png(PACK/path);self.assertEqual((w,h),(16,16))
                    if kind=='images':self.assertTrue(all(p[3] in (0,255) for p in pixels))
            old=tg.read_generated_png(ROOT/f'assets/textures/generated/{name}.png')[2]
            changed+=old!=tg.read_generated_png(PACK/sequence['images'][0])[2]
        self.assertGreater(changed,len(tg.NAMES)*.95)
        for name in names:
            entry=json.loads((PACK/f'item_icons/{name}.json').read_text())
            pixels=tg.read_generated_png(PACK/entry['image'])[2]
            self.assertFalse(tg.validate_item_sprite(pixels,name))
        self.assertEqual(json.loads((PACK/'resources.sha256.json').read_text())['sha256'],inventory(PACK))
        manifest=content_package.manifest(PACK)
        self.assertTrue(manifest['visual_only']);self.assertFalse(manifest['enabled_by_default'])
        self.assertEqual(manifest['after'],['official_heaven'])
        self.assertNotIn('native',manifest)
        self.assertFalse(any(p.name.startswith('.') for p in PACK.rglob('*')),
                         "mobile asset packaging drops hidden resources")

    def test_reproducible_and_publishable(self):
        with tempfile.TemporaryDirectory(prefix='minecraftc-natural-assets-') as temp:
            output=Path(temp)/'generated'
            result=subprocess.run([sys.executable,str(ROOT/'tools/natural_pixels.py'),'--output',str(output)],cwd=ROOT,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            expected=inventory(PACK);actual=inventory(output)
            self.assertEqual(set(expected),set(actual))
            self.assertEqual([p for p in expected if expected[p]!=actual[p]],[],"regenerated resource bytes differ")
            # Publication includes all sequence frames and the new icon category.
            staged=Path(temp)/'published'
            content_package.stage(PACK,staged,builtin=True)
            for directory in ('materials','item_icons','textures'):
                self.assertEqual({p.relative_to(PACK).as_posix() for p in (PACK/directory).rglob('*') if p.is_file()},
                                 {p.relative_to(staged).as_posix() for p in (staged/directory).rglob('*') if p.is_file()})
            for p in content_package.runtime_files(PACK):
                self.assertEqual(hashlib.sha256(p.read_bytes()).digest(),hashlib.sha256((staged/p.relative_to(PACK)).read_bytes()).digest())


if __name__=='__main__':unittest.main()
