"""Compare provider-published content against the pre-migration native export."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

reference=Path(__file__).resolve().parent/"reference/heaven_v9"
manifest=json.loads((reference/"manifest.json").read_text(encoding="utf-8"))
for source,entry in manifest["files"].items():
    actual=hashlib.sha256((reference/entry["snapshot"]).read_bytes()).hexdigest()
    if actual!=entry["sha256"]:
        raise SystemExit("Frozen source changed: "+source)
with tempfile.TemporaryDirectory(prefix="heaven-content-reference-") as directory:
    output=Path(directory)/"content.json"
    subprocess.run([sys.argv[1],"--export-heaven",str(output)],check=True)
    expected=json.loads((reference/"content.json").read_text(encoding="utf-8"))
    actual=json.loads(output.read_text(encoding="utf-8"))
    for section in expected:
        if actual.get(section)!=expected[section]:
            raise SystemExit("Provider content differs from frozen native export: "+section)
    if set(actual)!=set(expected):
        raise SystemExit("Provider export sections differ")
print("Frozen content, item, material, crafting and smelting equality passed")
