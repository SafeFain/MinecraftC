"""Export package-owned tiles from the shared authoring atlas."""
import json
import shutil
from pathlib import Path


def export_packages(generator, output, packages):
    output = Path(output)
    atlas = json.loads((output / "atlas.json").read_text(encoding="utf-8"))
    maps = {kind: generator.read_generated_png(output / filename)
            for kind, filename in (("images", "atlas.png"),
                                   ("normals", "atlas_normal.png"),
                                   ("properties", "atlas_property.png"))}
    for package in sorted(Path(packages).iterdir()):
        manifest = package / "materials.json"
        if not manifest.is_file():
            continue
        materials = json.loads(manifest.read_text(encoding="utf-8"))
        for material in materials:
            definition = atlas["textures"][material["key"]]
            material.update(variants=definition["variants"], frames=definition["frames"],
                            fps_milli=round(definition["fps"] * 1000),
                            seed=definition["selection_seed"])
            for kind, (width, _, pixels) in maps.items():
                paths = []
                for index, slot in enumerate(definition["slots"]):
                    x, y = slot % atlas["grid_size"] * 16, slot // atlas["grid_size"] * 16
                    tile = [pixels[(y + row) * width + x + column]
                            for row in range(16) for column in range(16)]
                    relative = f"textures/materials/{material['key']}-{index}-{kind}.png"
                    destination = package / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    generator.write_png(destination, 16, 16, tile)
                    paths.append(relative)
                material[kind] = paths
        manifest.write_text(json.dumps(materials, indent=2) + "\n", encoding="utf-8")
        content = json.loads((package / "content.json").read_text(encoding="utf-8"))
        for item in content.get("items", []):
            if "icon" not in item:
                continue
            relative = Path(item["icon"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("package icon escapes package")
            destination = package / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(output / "items" / (item["command"] + ".png"), destination)

        from check_official_package import refresh
        refresh(package)
