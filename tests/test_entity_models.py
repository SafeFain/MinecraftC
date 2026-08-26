#!/usr/bin/env python3
import json, pathlib, struct, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL_DIR = ROOT / "assets/models/entities"
PLAYER_DIR = ROOT / "assets/models/player"
NAMES = {"cow", "pig", "sheep", "chicken", "zombie", "skeleton", "spider",
         "blastling", "villager", "zombie_villager"}

def document(path):
    data = path.read_bytes()
    assert data[:4] == b"glTF" and struct.unpack_from("<I", data, 4)[0] == 2
    json_length, chunk_type = struct.unpack_from("<II", data, 12)
    assert chunk_type == 0x4E4F534A
    doc = json.loads(data[20:20 + json_length].decode("utf-8"))
    binary_offset = 20 + json_length + 8
    return doc, data[binary_offset:]

def float_accessor(doc, binary, index):
    accessor = doc["accessors"][index]
    view = doc["bufferViews"][accessor["bufferView"]]
    width = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}[accessor["type"]]
    offset = view.get("byteOffset",0) + accessor.get("byteOffset",0)
    return struct.unpack_from("<%df" % (accessor["count"]*width), binary, offset)

def vec2_accessor(doc,binary,index):
    accessor=doc["accessors"][index];view=doc["bufferViews"][accessor["bufferView"]]
    offset=view.get("byteOffset",0)+accessor.get("byteOffset",0)
    stride=view.get("byteStride",8)
    return tuple(struct.unpack_from("<2f",binary,offset+i*stride)
                 for i in range(accessor["count"]))

def vec3_accessor(doc,binary,index):
    accessor=doc["accessors"][index];view=doc["bufferViews"][accessor["bufferView"]]
    offset=view.get("byteOffset",0)+accessor.get("byteOffset",0)
    stride=view.get("byteStride",12)
    return tuple(struct.unpack_from("<3f",binary,offset+i*stride)
                 for i in range(accessor["count"]))

def index_accessor(doc,binary,index):
    accessor=doc["accessors"][index];view=doc["bufferViews"][accessor["bufferView"]]
    offset=view.get("byteOffset",0)+accessor.get("byteOffset",0)
    fmt={5121:"B",5123:"H",5125:"I"}[accessor["componentType"]]
    return struct.unpack_from("<%d%s"%(accessor["count"],fmt),binary,offset)

def u16vec4_accessor(doc,binary,index):
    accessor=doc["accessors"][index];view=doc["bufferViews"][accessor["bufferView"]]
    offset=view.get("byteOffset",0)+accessor.get("byteOffset",0)
    stride=view.get("byteStride",8)
    return tuple(struct.unpack_from("<4H",binary,offset+i*stride)
                 for i in range(accessor["count"]))

def png_dimensions(data):
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    return struct.unpack_from(">II",data,16)

def verify_joint_pivots(doc,binary,name):
    attributes=doc["meshes"][0]["primitives"][0]["attributes"]
    positions=vec3_accessor(doc,binary,attributes["POSITION"])
    vertex_joints=u16vec4_accessor(doc,binary,attributes["JOINTS_0"])
    skin=doc["skins"][0];nodes=doc["nodes"]
    inverse_bind=float_accessor(doc,binary,skin["inverseBindMatrices"])
    for joint_index,node_index in enumerate(skin["joints"]):
        part=nodes[node_index]["name"]
        pivot=nodes[node_index].get("translation",[0,0,0])
        inverse_translation=inverse_bind[joint_index*16+12:joint_index*16+15]
        assert all(abs(inverse_translation[axis]+pivot[axis])<1e-6 for axis in range(3)), \
            f"{name} {part} inverse bind matrix does not preserve its joint pivot"
        if not (part=="head" or part.startswith("arm_") or
                part.startswith("leg_") or part.startswith("wing_")):
            continue
        part_positions=[position for position,joints in zip(positions,vertex_joints)
                        if joints[0]==joint_index]
        assert part_positions, f"{name} {part} has no rigidly weighted vertices"
        if (name=="spider" and part.startswith("leg_")) or part.startswith("wing_"):
            proximal=max(p[0] for p in part_positions) if "_l" in part else \
                     min(p[0] for p in part_positions)
            assert abs(pivot[0]-proximal)<1e-6, \
                f"{name} {part} does not pivot at its inner attachment"
        elif part=="head" and name in {"cow","pig","sheep","chicken","spider"}:
            assert abs(pivot[2]-max(p[2] for p in part_positions))<1e-6, \
                f"{name} head does not pivot at its neck attachment"
        elif part=="head":
            assert abs(pivot[1]-min(p[1] for p in part_positions))<1e-6, \
                f"{name} head does not pivot at its neck attachment"
        else:
            assert abs(pivot[1]-max(p[1] for p in part_positions))<1e-6, \
                f"{name} {part} does not pivot at its upper joint"

def main():
    vulkan_vertex_shader = (ROOT / "assets/shaders/vulkan/model.vert").read_text()
    assert "uv=inUv" in vulkan_vertex_shader, \
        "Vulkan model UV contract changed"
    files = list(MODEL_DIR.glob("*.glb"))
    assert {path.stem for path in files} == NAMES, "expected exactly ten entity GLBs"
    graphs = list(MODEL_DIR.glob("*.anim.json"))
    assert {path.name[:-len(".anim.json")] for path in graphs} == NAMES, \
        "expected exactly ten entity action graphs"
    for path in files:
        doc, binary = document(path)
        assert {a["name"] for a in doc["animations"]} >= {"idle", "walk", "hurt", "death"}
        animations = {a["name"]:a for a in doc["animations"]}
        walk = animations["walk"]
        assert len(walk["channels"]) >= 2, f"{path.name} walk has no articulated gait"
        for sampler in walk["samplers"]:
            times = float_accessor(doc,binary,sampler["input"])
            values = float_accessor(doc,binary,sampler["output"])
            width = len(values)//len(times)
            assert len(times) >= 3 and values[:width] == values[-width:], \
                f"{path.name} walk does not close its loop"
        assert doc["skins"] and max(len(s["joints"]) for s in doc["skins"]) <= 64
        attributes = doc["meshes"][0]["primitives"][0]["attributes"]
        assert {"POSITION","NORMAL","TEXCOORD_0","JOINTS_0","WEIGHTS_0"} <= set(attributes)
        assert doc["materials"][0].get("doubleSided") is True, \
            f"{path.name} must remain double-sided in both render backends"
        assert doc["images"][0].get("mimeType") == "image/png" and "bufferView" in doc["images"][0]
        image_view=doc["bufferViews"][doc["images"][0]["bufferView"]]
        image=binary[image_view.get("byteOffset",0):image_view.get("byteOffset",0)+image_view["byteLength"]]
        assert png_dimensions(image)==(64,64), f"{path.name} does not embed a 64x64 entity skin"
        skin_path=ROOT/"assets"/"textures"/"generated"/"entity_skins"/(path.stem+".png")
        assert image==skin_path.read_bytes(), f"{path.name} embedded skin differs from texture generator output"
        sampler = doc["samplers"][0]
        assert sampler["magFilter"] == 9728 and sampler["minFilter"] == 9728
        assert doc["accessors"][attributes["POSITION"]]["min"][1] == 0.0
        positions=vec3_accessor(doc,binary,attributes["POSITION"])
        normals=vec3_accessor(doc,binary,attributes["NORMAL"])
        mesh_indices=index_accessor(doc,binary,doc["meshes"][0]["primitives"][0]["indices"])
        for offset in range(0,len(mesh_indices),3):
            ia,ib,ic=mesh_indices[offset:offset+3]
            a,b,c=positions[ia],positions[ib],positions[ic]
            ab=tuple(b[i]-a[i] for i in range(3));ac=tuple(c[i]-a[i] for i in range(3))
            winding=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
            assert sum(winding[i]*normals[ia][i] for i in range(3))>0, \
                f"{path.name} triangle winding faces inward and will be culled"
        all_uvs=vec2_accessor(doc,binary,attributes["TEXCOORD_0"])
        uv_pairs=set(all_uvs)
        assert len(uv_pairs)>16, f"{path.name} does not use face-specific atlas regions"
        assert min(v for pair in uv_pairs for v in pair)>0 and \
               max(v for pair in uv_pairs for v in pair)<1, \
               f"{path.name} UVs lack atlas inset"
        # Parts are emitted body then head; each cuboid contributes 24 vertices.
        def expected_tile(index):
            tx,ty=index%4,index//4
            u0,u1=(tx*16+.5)/64,(tx*16+15.5)/64
            v0,v1=(ty*16+.5)/64,(ty*16+15.5)/64
            return ((u0,v1),(u1,v1),(u1,v0),(u0,v0))
        assert all(abs(a-b)<1e-6 for pair,want in zip(all_uvs[:4],expected_tile(6))
                   for a,b in zip(pair,want)), f"{path.name} body front UV is incorrect"
        assert all(abs(a-b)<1e-6 for pair,want in zip(all_uvs[24:28],expected_tile(0))
                   for a,b in zip(pair,want)), f"{path.name} head front UV is incorrect or vertically flipped"
        verify_joint_pivots(doc,binary,path.stem)
        graph = json.loads((MODEL_DIR/(path.stem+".anim.json")).read_text())
        assert graph["version"] == 1 and {"idle","walk","hurt","death"} <= set(graph["actions"])
        if path.stem in {"zombie","skeleton","spider","blastling","zombie_villager"}:
            assert "attack" in animations and graph["actions"]["attack"]["events"]
    player_path = PLAYER_DIR / "player.glb"
    player_doc, player_binary = document(player_path)
    player_clips = {animation["name"] for animation in player_doc["animations"]}
    assert {"idle","walk","run","jump","fall","swing","hurt","death"} <= player_clips
    player_nodes = {node["name"] for node in player_doc["nodes"]}
    assert {"head","arm_r","arm_l","leg_r","leg_l"} <= player_nodes
    player_graph = json.loads((PLAYER_DIR / "player.anim.json").read_text())
    assert {"run","jump","fall","swing"} <= set(player_graph["actions"])
    assert player_doc["skins"] and len(player_doc["skins"][0]["joints"]) <= 64
    verify_joint_pivots(player_doc,player_binary,"player")
    player_image_view = player_doc["bufferViews"][player_doc["images"][0]["bufferView"]]
    player_data = player_path.read_bytes()
    json_length = struct.unpack_from("<I", player_data, 12)[0]
    binary_offset = 20 + json_length + 8
    embedded = player_data[binary_offset + player_image_view.get("byteOffset",0):
                           binary_offset + player_image_view.get("byteOffset",0) +
                           player_image_view["byteLength"]]
    assert embedded == (ROOT / "assets/textures/generated/entity_skins/player.png").read_bytes()
    with tempfile.TemporaryDirectory() as directory:
        generated = pathlib.Path(directory)
        generated_player = generated / "player"
        subprocess.run([sys.executable, str(ROOT / "tools/generate_entity_models.py"),
                        "--output", str(generated),
                        "--player-output", str(generated_player)], check=True)
        for path in files:
            assert path.read_bytes() == (generated / path.name).read_bytes(), \
                f"{path.name} was not reproduced byte-for-byte"
            graph_name = path.stem+".anim.json"
            assert (MODEL_DIR/graph_name).read_bytes() == (generated/graph_name).read_bytes(), \
                f"{graph_name} was not reproduced byte-for-byte"
        assert player_path.read_bytes() == (generated_player / "player.glb").read_bytes()
        assert (PLAYER_DIR / "player.anim.json").read_bytes() == \
               (generated_player / "player.anim.json").read_bytes()
    print("entity model asset contract passed")

if __name__ == "__main__":
    try: main()
    except (AssertionError, OSError, KeyError, ValueError) as error:
        print(f"FAILED: {error}", file=sys.stderr); raise SystemExit(1)
