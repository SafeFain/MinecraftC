"""Validate identical official provider resources in source/install/archive layouts."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import zipfile

MANIFEST = "resources.sha256.json"


def inventory(package):
    result = {}
    for path in sorted(package.rglob("*")):
        if path.is_symlink():
            raise ValueError("package contains symlink")
        if path.is_file() and path.name != MANIFEST:
            result[path.relative_to(package).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def refresh(package):
    (package / MANIFEST).write_text(json.dumps({"version": 1, "sha256": inventory(package)},
                                             indent=2) + "\n", encoding="utf-8")


def check(target, expected, package_id="official_heaven"):
    def verify(names, read, digests=None):
        manifests = [name for name in names if name.endswith("plugins/" + package_id + "/" + MANIFEST)]
        if len(manifests) != 1:
            raise ValueError(f"expected one official package, found {len(manifests)}")
        prefix = manifests[0][:-len(MANIFEST)]
        manifest = json.loads(read(manifests[0]))
        if manifest != expected:
            raise ValueError("packaged manifest differs from source")
        files = {name[len(prefix):] for name in names if name.startswith(prefix) and not name.endswith("/")}
        if files != set(expected["sha256"]) | {MANIFEST}:
            raise ValueError("missing or unexpected official resource")
        for name, digest in expected["sha256"].items():
            path = PurePosixPath(name)
            actual = digests.get(prefix + name) if digests is not None else hashlib.sha256(read(prefix + name)).hexdigest()
            if path.is_absolute() or ".." in path.parts or actual != digest:
                raise ValueError(f"invalid official resource: {name}")
        print(f"Official provider verified: {len(expected['sha256'])} resources in {target}")

    if target.is_dir():
        files = {path.relative_to(target).as_posix(): path for path in target.rglob("*") if path.is_file()}
        verify(files, lambda name: files[name].read_bytes())
    elif zipfile.is_zipfile(target):
        with zipfile.ZipFile(target) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise ValueError("archive contains duplicate paths")
            verify(names, archive.read)
    else:
        # Seeking backwards in gzip restarts decompression. Manifest order need
        # not match tar order (notably macOS bundles), so hash in archive order.
        # Retain only digests and manifests, never the unpacked resource payloads.
        files, manifests, digests = set(), {}, {}
        marker = "plugins/" + package_id + "/"
        with tarfile.open(target, "r|*") as archive:
            for entry in archive:
                if not entry.isfile():
                    continue
                if entry.name in files:
                    raise ValueError("archive contains duplicate paths")
                files.add(entry.name)
                if marker not in entry.name:
                    continue
                with archive.extractfile(entry) as source:
                    if entry.name.endswith(marker + MANIFEST):
                        manifests[entry.name] = source.read()
                    else:
                        digest = hashlib.sha256()
                        for block in iter(lambda: source.read(1024 * 1024), b""):
                            digest.update(block)
                        digests[entry.name] = digest.hexdigest()
        verify(files, manifests.__getitem__, digests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=Path)
    parser.add_argument("--refresh", action="store_true", help="refresh source manifest after authorized asset edits")
    args = parser.parse_args()
    package = Path(__file__).resolve().parents[1] / "assets/plugins/official_heaven"
    try:
        if args.refresh:
            refresh(package)
        for identifier in ("official_heaven", "official_natural_textures"):
            source = package.parent / identifier
            expected = json.loads((source / MANIFEST).read_text(encoding="utf-8"))
            if expected != {"version": 1, "sha256": inventory(source)}:
                raise ValueError(identifier + ": source resources changed without manifest refresh")
            check(args.target, expected, identifier)
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
