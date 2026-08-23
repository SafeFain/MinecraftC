#!/usr/bin/env python3
"""Deterministically generate MinecraftC's original block-style entity GLBs."""
import argparse, json, pathlib, struct, zlib
import texture_generator

VERSION = 4
SEED = 0x4D43474C

# Quaternion components are frozen as decimal constants instead of calling
# libm.  sin/cos can differ in the last bit between operating systems, and a
# one-ulp animation difference changes the byte-for-byte glTF output.
_HALF_PI = 1.5707963267948966
_QUAT_HALF = {
    0.0: (0.0, 1.0),
    0.10: (0.04997916927067833, 0.9987502603949663),
    -0.10: (-0.04997916927067833, 0.9987502603949663),
    0.15: (0.07492970727274234, 0.9971888181122075),
    -0.15: (-0.07492970727274234, 0.9971888181122075),
    0.20: (0.09983341664682815, 0.9950041652780258),
    -0.20: (-0.09983341664682815, 0.9950041652780258),
    0.25: (0.12467473338522769, 0.992197667229329),
    -0.25: (-0.12467473338522769, 0.992197667229329),
    0.28: (0.1395431146442365, 0.9902159962126371),
    -0.28: (-0.1395431146442365, 0.9902159962126371),
    0.30: (0.14943813247359922, 0.9887710779360422),
    -0.30: (-0.14943813247359922, 0.9887710779360422),
    0.35: (0.17410813759359595, 0.9847265389049334),
    -0.35: (-0.17410813759359595, 0.9847265389049334),
    0.38: (0.18885889497650057, 0.9820042351172703),
    -0.38: (-0.18885889497650057, 0.9820042351172703),
    0.42: (0.20845989984609956, 0.9780309147241483),
    -0.42: (-0.20845989984609956, 0.9780309147241483),
    0.48: (0.23770262642713458, 0.9713379748520297),
    -0.48: (-0.23770262642713458, 0.9713379748520297),
    0.55: (0.27154693695611287, 0.962425197628238),
    -0.55: (-0.27154693695611287, 0.962425197628238),
    0.72: (0.35227423327508994, 0.9358968236779348),
    -0.72: (-0.35227423327508994, 0.9358968236779348),
    1.05: (0.5012130046737979, 0.8653239416229412),
    -1.05: (-0.5012130046737979, 0.8653239416229412),
    1.15: (0.5438347906836426, 0.8391923024206541),
    -1.15: (-0.5438347906836426, 0.8391923024206541),
    1.35: (0.6248973167276999, 0.7807069511324468),
    -1.35: (-0.6248973167276999, 0.7807069511324468),
    _HALF_PI: (0.7071067811865475, 0.7071067811865476),
}
MODELS = {
    "cow": ((0.90,1.20,1.30),(112,72,48,255)),
    "pig": ((0.86,0.95,1.15),(225,132,151,255)),
    "sheep": ((0.96,1.20,1.25),(224,222,207,255)),
    "chicken": ((0.46,0.70,0.48),(236,229,190,255)),
    "zombie": ((0.62,1.78,0.42),(78,139,104,255)),
    "skeleton": ((0.48,1.78,0.34),(196,192,174,255)),
    "spider": ((1.25,0.58,1.20),(64,48,55,255)),
    "blastling": ((0.72,1.35,0.72),(105,170,102,255)),
}

def png(color, accent):
    width=height=16; raw=bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(accent if ((x//4+y//4)&1) else color)
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(bytes(raw),0))+chunk(b"IEND",b"")

class Buffer:
    def __init__(self): self.data=bytearray(); self.views=[]; self.accessors=[]
    def add(self,data,target=None):
        while len(self.data)%4:self.data.append(0)
        offset=len(self.data);self.data.extend(data)
        view={"buffer":0,"byteOffset":offset,"byteLength":len(data)}
        if target:view["target"]=target
        self.views.append(view);return len(self.views)-1
    def accessor(self,view,component,count,kind,offset=0,minimum=None,maximum=None,normalized=False):
        a={"bufferView":view,"byteOffset":offset,"componentType":component,"count":count,"type":kind}
        if minimum is not None:a["min"]=minimum
        if maximum is not None:a["max"]=maximum
        if normalized:a["normalized"]=True
        self.accessors.append(a);return len(self.accessors)-1

def build(name,size,color):
    sx,sy,sz=size; x=sx/2; z=sz/2
    faces=[((0,0,-1),[(-x,0,-z),(x,0,-z),(x,sy,-z),(-x,sy,-z)]),
           ((0,0,1),[(x,0,z),(-x,0,z),(-x,sy,z),(x,sy,z)]),
           ((-1,0,0),[(-x,0,z),(-x,0,-z),(-x,sy,-z),(-x,sy,z)]),
           ((1,0,0),[(x,0,-z),(x,0,z),(x,sy,z),(x,sy,-z)]),
           ((0,1,0),[(-x,sy,-z),(x,sy,-z),(x,sy,z),(-x,sy,z)]),
           ((0,-1,0),[(-x,0,z),(x,0,z),(x,0,-z),(-x,0,-z)])]
    vertices=bytearray();indices=[];uv=((0,0),(1,0),(1,1),(0,1))
    for normal,points in faces:
        base=len(indices)//6*4
        for point,tex in zip(points,uv):
            vertices.extend(struct.pack("<3f3f2f4H4f",*point,*normal,*tex,0,0,0,0,1,0,0,0))
        indices += [base,base+1,base+2,base,base+2,base+3]
    buf=Buffer(); vv=buf.add(vertices,34962);iv=buf.add(struct.pack("<36H",*indices),34963)
    pos=buf.accessor(vv,5126,24,"VEC3",0,[-x,0,-z],[x,sy,z]); normal=buf.accessor(vv,5126,24,"VEC3",12)
    tex=buf.accessor(vv,5126,24,"VEC2",24); joints=buf.accessor(vv,5123,24,"VEC4",32)
    weights=buf.accessor(vv,5126,24,"VEC4",40); inds=buf.accessor(iv,5123,36,"SCALAR")
    # Interleaved vertex attributes share one 64-byte-stride view.
    buf.views[vv]["byteStride"]=64
    inverse=buf.add(struct.pack("<16f",1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)); iba=buf.accessor(inverse,5126,1,"MAT4")
    times=buf.add(struct.pack("<2f",0,1)); time_a=buf.accessor(times,5126,2,"SCALAR",0,[0],[1])
    animations=[]
    values={"idle":((0,0,0),(0,.025,0)),"walk":((-.04,0,0),(.04,.035,0)),
            "hurt":((0,0,0),(0,.12,.10)),"death":((0,0,0),(0,-sy*.55,.25))}
    for clip,(a,b) in values.items():
        view=buf.add(struct.pack("<6f",*a,*b)); output=buf.accessor(view,5126,2,"VEC3")
        animations.append({"name":clip,"samplers":[{"input":time_a,"output":output,"interpolation":"LINEAR"}],
                           "channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]})
    accent=tuple(max(0,min(255,c+(22 if i<3 else 0))) for i,c in enumerate(color))
    image=png(color,accent); image_view=buf.add(image)
    doc={"asset":{"version":"2.0","generator":f"MinecraftC entity generator v{VERSION} seed {SEED}"},
         "scene":0,"scenes":[{"nodes":[0,1]}],
         "nodes":[{"name":"root_bone"},{"name":name,"mesh":0,"skin":0}],
         "skins":[{"name":name+"_skin","joints":[0],"skeleton":0,"inverseBindMatrices":iba}],
         "meshes":[{"name":name+"_block","primitives":[{"attributes":{"POSITION":pos,"NORMAL":normal,"TEXCOORD_0":tex,"JOINTS_0":joints,"WEIGHTS_0":weights},"indices":inds,"material":0}]}],
         "materials":[{"name":name+"_pixels","doubleSided":True,"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0,"roughnessFactor":1}}],
         "textures":[{"sampler":0,"source":0}],"samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}],
         "images":[{"name":name+"_texture","mimeType":"image/png","bufferView":image_view}],
         "animations":animations,"accessors":buf.accessors,"bufferViews":buf.views,"buffers":[{"byteLength":len(buf.data)}]}
    encoded=json.dumps(doc,sort_keys=True,separators=(",",":"),ensure_ascii=True).encode()
    encoded+=b" "*((-len(encoded))%4); binary=bytes(buf.data)+b"\0"*((-len(buf.data))%4)
    total=12+8+len(encoded)+8+len(binary)
    return struct.pack("<4sII",b"glTF",2,total)+struct.pack("<II",len(encoded),0x4E4F534A)+encoded+struct.pack("<II",len(binary),0x004E4942)+binary

def parts_for(name):
    if name in {"cow","pig","sheep"}:
        return [("body",(0,.78,.10),(.9,.62,1.05)),("head",(0,.92,-.62),(.62,.55,.42)),
                ("leg_fl",(-.28,.26,-.30),(.18,.52,.18)),("leg_fr",(.28,.26,-.30),(.18,.52,.18)),
                ("leg_bl",(-.28,.26,.40),(.18,.52,.18)),("leg_br",(.28,.26,.40),(.18,.52,.18))]
    if name=="chicken":
        return [("body",(0,.38,0),(.46,.38,.46)),("head",(0,.68,-.18),(.34,.34,.34)),
                ("wing_l",(-.28,.40,0),(.10,.28,.32)),("wing_r",(.28,.40,0),(.10,.28,.32)),
                ("leg_l",(-.10,.12,0),(.08,.24,.08)),("leg_r",(.10,.12,0),(.08,.24,.08))]
    if name in {"zombie","skeleton","player"}:
        thin=.14 if name=="skeleton" else .22
        return [("body",(0,1.03,0),(.52,.68,.34)),("head",(0,1.55,0),(.50,.40,.50)),
                ("arm_l",(-.36,1.03,0),(thin,.72,thin)),("arm_r",(.36,1.03,0),(thin,.72,thin)),
                ("leg_l",(-.15,.38,0),(thin,.76,thin)),("leg_r",(.15,.38,0),(thin,.76,thin))]
    if name=="spider":
        result=[("body",(0,.34,.15),(.80,.32,.72)),("head",(0,.32,-.40),(.58,.30,.34))]
        for i,z in enumerate((-.38,-.12,.14,.40)):
            result += [(f"leg_l{i}",(-.55,.05,z),(.46,.10,.12)),(f"leg_r{i}",(.55,.05,z),(.46,.10,.12))]
        return result
    return [("body",(0,.80,0),(.64,.70,.62)),("head",(0,1.28,0),(.58,.42,.56)),
            ("leg_fl",(-.22,.27,-.20),(.18,.54,.18)),("leg_fr",(.22,.27,-.20),(.18,.54,.18)),
            ("leg_bl",(-.22,.27,.20),(.18,.54,.18)),("leg_br",(.22,.27,.20),(.18,.54,.18))]

def build_v2(name,size,color):
    del size
    parts=parts_for(name);buf=Buffer();vertices=bytearray();indices=[]
    normals=((0,0,-1),(0,0,1),(-1,0,0),(1,0,0),(0,1,0),(0,-1,0))
    face_names=("front","back","left","right","top","bottom")
    def semantic(part,face):
        if part=="head": return "head_"+face
        if part=="body": return "body_"+face
        if part.startswith("wing_"): return "limb_secondary"
        if name in {"zombie","skeleton","player"} and part.startswith("leg_"): return "limb_secondary"
        return "limb_primary"
    def tile_uv(slot):
        index=texture_generator.ENTITY_SKIN_LAYOUT[slot];tx,ty=index%4,index//4
        u0=(tx*16+.5)/64;u1=(tx*16+15.5)/64
        v0=(ty*16+.5)/64;v1=(ty*16+15.5)/64
        # PNG row zero is the visual top; bottom vertices therefore use v1.
        return ((u0,v1),(u1,v1),(u1,v0),(u0,v0))
    for joint,(part,center,dims) in enumerate(parts,1):
        cx,cy,cz=center;dx,dy,dz=(v/2 for v in dims)
        corners=[(cx-dx,cy-dy,cz-dz),(cx+dx,cy-dy,cz-dz),(cx+dx,cy+dy,cz-dz),(cx-dx,cy+dy,cz-dz),
                 (cx-dx,cy-dy,cz+dz),(cx+dx,cy-dy,cz+dz),(cx+dx,cy+dy,cz+dz),(cx-dx,cy+dy,cz+dz)]
        faces=((0,1,2,3),(5,4,7,6),(4,0,3,7),(1,5,6,2),(3,2,6,7),(4,5,1,0))
        for normal,face,face_name in zip(normals,faces,face_names):
            base=len(vertices)//64
            for corner,tex in zip(face,tile_uv(semantic(part,face_name))):vertices.extend(struct.pack("<3f3f2f4H8x4f",*corners[corner],*normal,*tex,joint,0,0,0,1,0,0,0))
            # glTF/OpenGL use counter-clockwise front faces. The cuboid corner
            # lists above are clockwise when viewed from the outward normal.
            indices += [base,base+2,base+1,base,base+3,base+2]
    vv=buf.add(vertices,34962);buf.views[vv]["byteStride"]=64
    iv=buf.add(struct.pack("<%dH"%len(indices),*indices),34963);count=len(vertices)//64
    all_points=[(c[0]+sx*d[0]/2,c[1]+sy*d[1]/2,c[2]+sz*d[2]/2) for _,c,d in parts for sx in (-1,1) for sy in (-1,1) for sz in (-1,1)]
    minimum=[min(p[i] for p in all_points) for i in range(3)];maximum=[max(p[i] for p in all_points) for i in range(3)]
    attrs={"POSITION":buf.accessor(vv,5126,count,"VEC3",0,minimum,maximum),"NORMAL":buf.accessor(vv,5126,count,"VEC3",12),
           "TEXCOORD_0":buf.accessor(vv,5126,count,"VEC2",24),"JOINTS_0":buf.accessor(vv,5123,count,"VEC4",32),"WEIGHTS_0":buf.accessor(vv,5126,count,"VEC4",48)}
    inds=buf.accessor(iv,5123,len(indices),"SCALAR")
    matrices=[]
    for _,center,_ in [("root",(0,0,0),(0,0,0))]+parts:
        x,y,z=center;matrices += [1,0,0,0,0,1,0,0,0,0,1,0,-x,-y,-z,1]
    ibv=buf.add(struct.pack("<%df"%len(matrices),*matrices));iba=buf.accessor(ibv,5126,len(parts)+1,"MAT4")
    nodes=[{"name":"root","children":list(range(1,len(parts)+1))}]
    nodes += [{"name":part,"translation":list(center)} for part,center,_ in parts]
    mesh_node=len(nodes);nodes.append({"name":name,"mesh":0,"skin":0})
    animations=[]
    def animation(clip,duration,channels):
        frame_count=len(channels[0][2]); times=[duration*i/(frame_count-1) for i in range(frame_count)]
        tv=buf.add(struct.pack("<%df"%frame_count,*times));ta=buf.accessor(tv,5126,frame_count,"SCALAR",0,[0],[duration])
        samplers=[];outputs=[]
        for node,path,values in channels:
            assert len(values)==frame_count
            view=buf.add(struct.pack("<%df"%sum(len(v) for v in values),*(x for v in values for x in v)))
            output=buf.accessor(view,5126,frame_count,"VEC4" if path=="rotation" else "VEC3")
            samplers.append({"input":ta,"output":output,"interpolation":"LINEAR"});outputs.append({"sampler":len(samplers)-1,"target":{"node":node,"path":path}})
        animations.append({"name":clip,"samplers":samplers,"channels":outputs})
    def qx(angle):
        sine,cosine=_QUAT_HALF[angle]; return (sine,0,0,cosine)
    def qy(angle):
        sine,cosine=_QUAT_HALF[angle]; return (0,sine,0,cosine)
    def qz(angle):
        sine,cosine=_QUAT_HALF[angle]; return (0,0,sine,cosine)
    node={part:index for index,(part,_,_) in enumerate(parts,1)}
    animation("idle",1.6,[(0,"translation",((0,0,0),(0,.025,0),(0,0,0)))])
    walk=[]
    if name in {"cow","pig","sheep","blastling"}:
        for part in ("leg_fl","leg_br"):
            walk.append((node[part],"rotation",(qx(.38),qx(-.38),qx(.38))))
        for part in ("leg_fr","leg_bl"):
            walk.append((node[part],"rotation",(qx(-.38),qx(.38),qx(-.38))))
        walk.append((0,"translation",((0,0,0),(0,.035,0),(0,0,0))))
    elif name in {"zombie","skeleton","player"}:
        for part,phase in (("leg_l",1),("leg_r",-1),("arm_l",-1),("arm_r",1)):
            walk.append((node[part],"rotation",(qx(.48*phase),qx(-.48*phase),qx(.48*phase))))
    elif name=="chicken":
        walk += [(node["leg_l"],"rotation",(qx(.42),qx(-.42),qx(.42))),
                 (node["leg_r"],"rotation",(qx(-.42),qx(.42),qx(-.42))),
                 (node["wing_l"],"rotation",(qz(-.10),qz(-.28),qz(-.10))),
                 (node["wing_r"],"rotation",(qz(.10),qz(.28),qz(.10))),
                 (0,"translation",((0,0,0),(0,.045,0),(0,0,0)))]
    else:
        for i in range(4):
            phase=1 if i%2==0 else -1
            walk += [(node[f"leg_l{i}"],"rotation",(qy(.30*phase),qy(-.30*phase),qy(.30*phase))),
                     (node[f"leg_r{i}"],"rotation",(qy(-.30*phase),qy(.30*phase),qy(-.30*phase)))]
        walk.append((0,"translation",((0,0,0),(0,.025,0),(0,0,0))))
    animation("walk",1.0,walk)
    if name=="player":
        run=[]
        for part,phase in (("leg_l",1),("leg_r",-1),("arm_l",-1),("arm_r",1)):
            run.append((node[part],"rotation",(qx(.72*phase),qx(-.72*phase),qx(.72*phase))))
        animation("run",.72,run)
        animation("jump",.45,[(node["leg_l"],"rotation",(qx(0),qx(-.55),qx(-.55))),
                               (node["leg_r"],"rotation",(qx(0),qx(.25),qx(.25))),
                               (node["arm_l"],"rotation",(qx(0),qx(-.35),qx(-.35)))])
        animation("fall",.45,[(node["leg_l"],"rotation",(qx(-.55),qx(.15),qx(.15))),
                               (node["leg_r"],"rotation",(qx(.25),qx(-.25),qx(-.25))),
                               (node["arm_l"],"rotation",(qx(-.35),qx(.2),qx(.2)))])
        animation("swing",.32,[(node["arm_r"],"rotation",(qx(0),qx(-1.35),qx(0)))])
    animation("hurt",.35,[(0,"translation",((0,0,0),(0,.12,.10),(0,0,0)))])
    animation("death",1.0,[(0,"rotation",(qz(0),qz(_HALF_PI),qz(_HALF_PI)))])
    if name=="zombie":
        animation("attack",.55,[(node["arm_l"],"rotation",(qx(-.2),qx(-1.15),qx(.35))),
                                 (node["arm_r"],"rotation",(qx(-.2),qx(-1.15),qx(.35)))])
    elif name=="skeleton":
        animation("attack",.75,[(node["arm_l"],"rotation",(qx(-.25),qx(-1.05),qx(-.15))),
                                 (node["arm_r"],"rotation",(qy(-.20),qy(.55),qy(0)))])
    elif name=="spider":
        animation("attack",.50,[(0,"translation",((0,0,0),(0,.12,-.28),(0,0,0))),
                                 (node["leg_l0"],"rotation",(qy(0),qy(.55),qy(0))),
                                 (node["leg_r0"],"rotation",(qy(0),qy(-.55),qy(0)))])
    elif name=="blastling":
        animation("attack",1.20,[(0,"scale",((1,1,1),(1.18,1.18,1.18),(1,1,1))),
                                  (0,"translation",((0,0,0),(0,.08,0),(0,0,0)))])
    del color
    skin=texture_generator.generate_entity_skin(name,texture_generator.DEFAULT_SEED)
    image_view=buf.add(texture_generator.png_bytes(64,64,skin,0))
    doc={"asset":{"version":"2.0","generator":f"MinecraftC entity generator v{VERSION} seed {SEED}"},"scene":0,"scenes":[{"nodes":[0,mesh_node]}],"nodes":nodes,
         "skins":[{"name":name+"_skin","joints":list(range(len(parts)+1)),"skeleton":0,"inverseBindMatrices":iba}],
         "meshes":[{"name":name+"_blocks","primitives":[{"attributes":attrs,"indices":inds,"material":0}]}],
         "materials":[{"name":name+"_pixels","doubleSided":True,"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0,"roughnessFactor":1}}],
         "textures":[{"sampler":0,"source":0}],"samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}],"images":[{"mimeType":"image/png","bufferView":image_view}],
         "animations":animations,"accessors":buf.accessors,"bufferViews":buf.views,"buffers":[{"byteLength":len(buf.data)}]}
    encoded=json.dumps(doc,sort_keys=True,separators=(",",":")).encode();encoded+=b" "*((-len(encoded))%4);binary=bytes(buf.data)+b"\0"*((-len(buf.data))%4);total=28+len(encoded)+len(binary)
    return struct.pack("<4sII",b"glTF",2,total)+struct.pack("<II",len(encoded),0x4E4F534A)+encoded+struct.pack("<II",len(binary),0x004E4942)+binary

def write_action_graph(path,name):
    action_nodes={
        "zombie":{"body":1,"head":1,"arm_l":1,"arm_r":1},
        "skeleton":{"body":1,"head":1,"arm_l":1,"arm_r":1},
        "player":{"arm_r":1},
    }
    layers=[{"name":"base","order":0,"blend":"override"},
            {"name":"action","order":100,"blend":"override"},
            {"name":"reaction","order":200,"blend":"additive"},
            {"name":"death","order":300,"blend":"override"}]
    if name in action_nodes:
        layers[1]["mask"]={"nodes":action_nodes[name],"include_descendants":True}
    actions={
        "idle":{"clip":"idle","layer":"base","loop":True,"fade_in":.15,"fade_out":.15},
        "walk":{"clip":"walk","layer":"base","loop":True,"fade_in":.15,"fade_out":.15},
        "hurt":{"clip":"hurt","layer":"reaction","loop":False,"priority":200,"fade_in":.04,"fade_out":.10},
        "death":{"clip":"death","layer":"death","loop":False,"priority":300,"fade_in":.08,"fade_out":0},
    }
    if name=="player":
        actions.update({
            "run":{"clip":"run","layer":"base","loop":True,"fade_in":.10,"fade_out":.10},
            "jump":{"clip":"jump","layer":"base","loop":True,"fade_in":.08,"fade_out":.10},
            "fall":{"clip":"fall","layer":"base","loop":True,"fade_in":.08,"fade_out":.10},
            "swing":{"clip":"swing","layer":"action","loop":False,"priority":100,"fade_in":.03,"fade_out":.06},
        })
    attack={
        "zombie":(.55,.30,"melee"),"spider":(.50,.30,"melee"),
        "skeleton":(.75,.45,"shoot"),"blastling":(1.20,1.00,"explode")}
    if name in attack:
        duration,event_time,event=attack[name]
        actions["attack"]={"clip":"attack","layer":"action","loop":False,
                           "duration":duration,"priority":100,"fade_in":.08,
                           "fade_out":.12,"events":[{"name":event,"time":event_time}]}
    graph={"version":1,"bindings":{key:key for key in actions},
           "layers":layers,"actions":actions}
    path.write_text(json.dumps(graph,sort_keys=True,indent=2)+"\n",encoding="utf-8")

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--output",type=pathlib.Path,required=True);parser.add_argument("--fixtures",type=pathlib.Path);parser.add_argument("--player-output",type=pathlib.Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    for name,(size,color) in MODELS.items():
        (args.output/(name+".glb")).write_bytes(build_v2(name,size,color))
        write_action_graph(args.output/(name+".anim.json"),name)
    if args.player_output:
        args.player_output.mkdir(parents=True,exist_ok=True)
        (args.player_output/"player.glb").write_bytes(build_v2("player",(.62,1.8,.42),(61,93,126,255)))
        write_action_graph(args.player_output/"player.anim.json","player")

if __name__=="__main__":main()
