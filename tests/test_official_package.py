import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from check_official_package import check, inventory, MANIFEST


class OfficialPackageTests(unittest.TestCase):
    def test_source_manifest(self):
        root=Path(__file__).resolve().parents[1]/"assets"
        manifest=json.loads((root/"plugins/official_heaven"/MANIFEST).read_text())
        self.assertEqual(manifest,{"version":1,"sha256":inventory(root/"plugins/official_heaven")})
        check(root,manifest)

    def test_installed_and_archive_bytes(self):
        data=b"provider-owned resource"
        manifest={"version":1,"sha256":{"content.json":hashlib.sha256(data).hexdigest()}}
        for prefix in ("release/assets/", "Payload/MinecraftC.app/", "assets/"):
            files={prefix+"plugins/official_heaven/"+MANIFEST:json.dumps(manifest).encode(),
                   prefix+"plugins/official_heaven/content.json":data}
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                for name,value in files.items():
                    destination=root/name;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(value)
                check(root,manifest)
                for extension in ("zip","tar.gz"):
                    archive=root/("package."+extension)
                    if extension=="zip":
                        with zipfile.ZipFile(archive,"w") as output:
                            for name,value in files.items():output.writestr(name,value)
                    else:
                        with tarfile.open(archive,"w:gz") as output:
                            for name,value in files.items():
                                entry=tarfile.TarInfo(name);entry.size=len(value);output.addfile(entry,io.BytesIO(value))
                    check(archive,manifest)
                destination.write_bytes(b"modified")
                with self.assertRaisesRegex(ValueError,"invalid official resource"):check(root,manifest)
                destination.unlink()
                with self.assertRaisesRegex(ValueError,"missing or unexpected"):check(root,manifest)


if __name__=="__main__":unittest.main()
