#!/usr/bin/env python3
import json, pathlib, struct, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODEL_DIR = ROOT / "assets/models/entities"
NAMES = {"cow", "pig", "sheep", "chicken", "zombie", "skeleton", "spider", "blastling"}

def document(path):
    data = path.read_bytes()
    assert data[:4] == b"glTF" and struct.unpack_from("<I", data, 4)[0] == 2
    json_length, chunk_type = struct.unpack_from("<II", data, 12)
    assert chunk_type == 0x4E4F534A
    return json.loads(data[20:20 + json_length].decode("utf-8"))

def main():
    files = list(MODEL_DIR.glob("*.glb"))
    assert {path.stem for path in files} == NAMES, "expected exactly eight entity GLBs"
    for path in files:
        doc = document(path)
        assert {a["name"] for a in doc["animations"]} >= {"idle", "walk", "hurt", "death"}
        assert doc["skins"] and max(len(s["joints"]) for s in doc["skins"]) <= 64
        attributes = doc["meshes"][0]["primitives"][0]["attributes"]
        assert {"POSITION","NORMAL","TEXCOORD_0","JOINTS_0","WEIGHTS_0"} <= set(attributes)
        assert doc["images"][0].get("mimeType") == "image/png" and "bufferView" in doc["images"][0]
        sampler = doc["samplers"][0]
        assert sampler["magFilter"] == 9728 and sampler["minFilter"] == 9728
        assert doc["accessors"][attributes["POSITION"]]["min"][1] == 0.0
    with tempfile.TemporaryDirectory() as directory:
        generated = pathlib.Path(directory)
        subprocess.run([sys.executable, str(ROOT / "tools/generate_entity_models.py"),
                        "--output", str(generated)], check=True)
        for path in files:
            assert path.read_bytes() == (generated / path.name).read_bytes(), \
                f"{path.name} was not reproduced byte-for-byte"
    print("entity model asset contract passed")

if __name__ == "__main__":
    try: main()
    except (AssertionError, OSError, KeyError, ValueError) as error:
        print(f"FAILED: {error}", file=sys.stderr); raise SystemExit(1)
