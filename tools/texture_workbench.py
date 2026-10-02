#!/usr/bin/env python3
"""Local material authoring workbench. No third-party dependencies or remote assets."""
import argparse
import copy
import hashlib
import json
import shutil
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse

import texture_generator as tg
from texture_recipes import generate_material, load_recipes, material_maps

ROOT=Path(__file__).resolve().parents[1]
STATIC=Path(__file__).resolve().parent/'texture_workbench'


class Workbench:
    def __init__(self,drafts):
        self.drafts=Path(drafts);self.drafts.mkdir(parents=True,exist_ok=True)
        self.document=load_recipes(tg)[0]
        self.seed=tg.DEFAULT_SEED;self.locked={}
        path=self.drafts/'draft.json'
        if path.exists():self.restore(json.loads(path.read_text()))

    def restore(self,state):
        document=state.get('document',self.document);load_recipes(tg,document=document)
        seed=int(state.get('seed',self.seed));locked=state.get('locked',{})
        if not isinstance(locked,dict) or set(locked)-set(tg.NAMES):raise ValueError('invalid locked materials')
        locked={name:int(value) for name,value in locked.items()}
        self.document=copy.deepcopy(document);self.seed=seed;self.locked=locked

    def state(self):
        return {'document':self.document,'seed':self.seed,'locked':self.locked,'materials':tg.NAMES,
                'palettes':tg.PALETTES,'style':tg.STYLE_ID,'generator_version':5}

    def preview(self,request):
        name=request['material']
        if name not in tg.NAMES:raise ValueError('unknown material')
        document=request.get('document',self.document);load_recipes(tg,document=document)
        seed=int(request.get('seed',self.seed));variant=int(request.get('variant',0))
        frames=document['materials'][name].get('animation',{}).get('frames',1)
        samples=[]
        for frame in range(frames):
            result=generate_material(tg,name,seed,self.locked,variant,frame,document)
            normal,properties,height=material_maps(tg,name,result,tg.load_material_profiles()[name])
            samples.append({'pixels':result['pixels'],'normal':normal,'property':properties,'height':height,
                            'masks':result['masks'],'roles':result['roles'],'recipe_sha256':result['recipe_sha256']})
        variants=[samples if v==variant else self.preview({**request,'variant':v,'single':True})['frames'] for v in range(document['materials'][name].get('variants',1))] if not request.get('single') else []
        return {'variants':variants,'frames':samples,'fps':document['materials'][name].get('animation',{}).get('fps',0),
                'effective_seed':str(generate_material(tg,name,seed,self.locked,variant,0,document)['effective_seed'])}

    def save(self,request):
        self.restore(request)
        path=self.drafts/'draft.json';temporary=path.with_suffix('.tmp')
        temporary.write_text(json.dumps({'document':self.document,'seed':self.seed,'locked':self.locked},indent=2)+'\n')
        temporary.replace(path)
        return {'saved':str(path)}

    def export(self,request):
        self.save(request)
        identity=hashlib.sha256(json.dumps({'document':self.document,'seed':self.seed,'locked':self.locked},sort_keys=True).encode()).hexdigest()[:16]
        output=self.drafts/'exports'/identity;output.mkdir(parents=True,exist_ok=True)
        (output/'recipes.json').write_text(json.dumps(self.document,indent=2)+'\n')
        # The same Python interface is used by CLI generation, no separate JS generator.
        previous=getattr(tg,'RECIPE_DOCUMENT',None);tg.RECIPE_DOCUMENT=self.document
        try:
            tg.generate(output,self.seed,self.locked);tg.build_atlas(output,self.seed,self.locked)
            tg.build_items_atlas(output,self.seed,ROOT/'assets/textures/definitions/item_icons.json',ROOT/'assets/textures/definitions/blocks.json')
        finally:tg.RECIPE_DOCUMENT=previous
        (output/'export.json').write_text(json.dumps({'version':1,'seed':self.seed,'locked':self.locked,'recipe_sha256':identity,'license':'CC0-1.0 original procedural assets'},indent=2)+'\n')
        if request.get('apply') is True:
            # Fixed destinations only; arbitrary browser-supplied paths are never accepted.
            target=ROOT/'assets/textures/generated'
            for path in output.iterdir():
                if path.name in {'recipes.json','export.json'}:continue
                if path.is_dir():shutil.copytree(path,target/path.name,dirs_exist_ok=True)
                else:shutil.copy2(path,target/path.name)
            (ROOT/'assets/textures/definitions/recipes.json').write_text(json.dumps(self.document,indent=2)+'\n')
        return {'exported':str(output),'applied':request.get('apply') is True}


def make_handler(workbench):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def send(self,status,data,kind='application/json'):
            body=json.dumps(data).encode() if kind=='application/json' else data
            self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)))
            self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(body)
        def allowed(self):
            host=self.headers.get('Host','');expected=f'127.0.0.1:{self.server.server_port}'
            if host not in (expected,f'localhost:{self.server.server_port}'):return False
            origin=self.headers.get('Origin')
            return origin is None or origin in ('http://'+expected,f'http://localhost:{self.server.server_port}')
        def do_GET(self):
            if not self.allowed():return self.send(403,{'error':'local origin required'})
            path=urlparse(self.path).path
            if path=='/api/state':return self.send(200,workbench.state())
            filenames={'/':'index.html','/workbench.js':'workbench.js','/style.css':'style.css'}
            if path not in filenames:return self.send(404,{'error':'not found'})
            kinds={'/':'text/html; charset=utf-8','/workbench.js':'text/javascript; charset=utf-8','/style.css':'text/css; charset=utf-8'}
            self.send(200,(STATIC/filenames[path]).read_bytes(),kinds[path])
        def do_POST(self):
            if not self.allowed():return self.send(403,{'error':'local origin required'})
            if self.headers.get('Content-Type','').split(';')[0]!='application/json':return self.send(415,{'error':'JSON required'})
            try:
                size=int(self.headers.get('Content-Length','0'))
                if not 0<size<=1_000_000:raise ValueError('request size out of range')
                data=json.loads(self.rfile.read(size))
                if not isinstance(data,dict):raise ValueError('request must be an object')
                methods={'/api/preview':workbench.preview,'/api/save':workbench.save,'/api/export':workbench.export}
                if self.path not in methods:return self.send(404,{'error':'not found'})
                self.send(200,methods[self.path](data))
            except (ValueError,KeyError,TypeError,OSError) as error:self.send(400,{'error':str(error)})
    return Handler


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--drafts',type=Path,default=ROOT/'build-local/texture-workbench')
    args=parser.parse_args();workbench=Workbench(args.drafts)
    with HTTPServer(('127.0.0.1',args.port),make_handler(workbench)) as server:
        print(f'Material workbench: http://127.0.0.1:{server.server_port}',flush=True)
        try:server.serve_forever()
        except KeyboardInterrupt:pass


if __name__=='__main__':main()
